import argparse
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
QEMU_CANDIDATES = [
    os.environ.get("QEMU", ""),
    r"D:\QEMU\qemu-system-x86_64.exe",
]
QEMU_SUCCESS = (0x10 << 1) | 1
QEMU_FAILURE = (0x11 << 1) | 1


class CheckFailure(Exception):
    pass


def find_qemu():
    for candidate in QEMU_CANDIDATES:
        if candidate and Path(candidate).exists():
            return str(candidate)
    found = shutil.which("qemu-system-x86_64")
    if found:
        return found
    raise CheckFailure("qemu-system-x86_64 was not found")


def run_build():
    print("== build ==")
    result = subprocess.run([sys.executable, str(ROOT / "tools" / "build.py")], cwd=ROOT, text=True)
    if result.returncode != 0:
        raise CheckFailure("build failed")


def base_drive(image):
    return ["-drive", "format=raw,file=" + str(BUILD / image) + ",if=floppy", "-boot", "a", "-m", "256M", "-no-reboot"]


def check_log(log, markers, forbidden):
    for marker in markers:
        if marker not in log:
            raise CheckFailure("missing marker: " + marker)
    for marker in forbidden:
        if marker in log:
            raise CheckFailure("forbidden marker: " + marker)


def run_selftest():
    print("== kernel self-test ==")
    qemu = find_qemu()
    log_path = BUILD / "selftest.log"
    if log_path.exists():
        log_path.unlink()
    command = [qemu] + base_drive("z-os-test.img") + [
        "-display", "none",
        "-serial", "file:" + str(log_path),
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        exit_code = process.wait(timeout=40)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise CheckFailure("self-test timed out")
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    print(log)
    if exit_code != QEMU_SUCCESS:
        raise CheckFailure("self-test QEMU exit code " + str(exit_code) + " expected " + str(QEMU_SUCCESS))
    check_log(log, ["ZTEST BEGIN", "ZTEST SUMMARY passed=49 failed=0", "ZTEST RESULT PASS"], ["[FAIL]", "PANIC", "EXCEPTION"])
    print("self-test PASS")


def run_smoke():
    print("== boot smoke test ==")
    qemu = find_qemu()
    log_path = BUILD / "smoke.log"
    if log_path.exists():
        log_path.unlink()
    command = [qemu] + base_drive("z-os.img") + ["-display", "none", "-serial", "file:" + str(log_path)]
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline = time.time() + 20
    log = ""
    try:
        while time.time() < deadline:
            if process.poll() is not None:
                raise CheckFailure("normal boot exited unexpectedly")
            if log_path.exists():
                log = log_path.read_text(encoding="utf-8", errors="replace")
                if "Boot complete. Starting shell." in log and "z-os:" in log:
                    break
            time.sleep(0.2)
        else:
            raise CheckFailure("normal boot did not reach the shell prompt")
        check_log(log, ["Z-OS", "Boot complete. Starting shell.", "z-os:"], ["PANIC", "EXCEPTION"])
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    print(log)
    print("boot smoke PASS")


class SerialSession:
    def __init__(self, connection):
        self.connection = connection
        self.connection.settimeout(0.2)
        self.data = bytearray()
        self.lock = threading.Lock()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        while True:
            try:
                chunk = self.connection.recv(4096)
                if not chunk:
                    break
                with self.lock:
                    self.data.extend(chunk)
            except socket.timeout:
                continue
            except OSError:
                break

    def snapshot(self):
        with self.lock:
            return bytes(self.data)

    def wait_for(self, marker, timeout, start=0):
        deadline = time.time() + timeout
        marker_bytes = marker.encode() if isinstance(marker, str) else marker
        while time.time() < deadline:
            data = self.snapshot()
            position = data.find(marker_bytes, start)
            if position >= 0:
                return position
            time.sleep(0.02)
        raise CheckFailure("timed out waiting for serial marker: " + str(marker))

    def send_line(self, line, pacing=0.005):
        for byte in (line + "\n").encode():
            self.connection.sendall(bytes([byte]))
            time.sleep(pacing)

    def close(self):
        try:
            self.connection.close()
        except OSError:
            pass


def free_port():
    temporary = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    temporary.bind(("127.0.0.1", 0))
    port = temporary.getsockname()[1]
    temporary.close()
    return port


def run_keyboard():
    print("== PS/2 keyboard test ==")
    qemu = find_qemu()
    log_path = BUILD / "keyboard.log"
    if log_path.exists():
        log_path.unlink()
    monitor_port = free_port()
    command = [qemu] + base_drive("z-os.img") + [
        "-display", "none",
        "-serial", "file:" + str(log_path),
        "-monitor", "tcp:127.0.0.1:" + str(monitor_port) + ",server=on,wait=off",
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    monitor = None
    try:
        deadline = time.time() + 20
        log = ""
        while time.time() < deadline:
            if process.poll() is not None:
                raise CheckFailure("keyboard test QEMU exited early")
            if log_path.exists():
                log = log_path.read_text(encoding="utf-8", errors="replace")
                if "z-os:" in log:
                    break
            time.sleep(0.2)
        else:
            raise CheckFailure("keyboard test did not reach the shell prompt")
        deadline = time.time() + 5
        while time.time() < deadline:
            try:
                monitor = socket.create_connection(("127.0.0.1", monitor_port), timeout=0.5)
                break
            except OSError:
                time.sleep(0.2)
        if monitor is None:
            raise CheckFailure("could not connect to the QEMU monitor")
        monitor.settimeout(0.1)
        try:
            monitor.recv(8192)
        except OSError:
            pass
        for key in ["h", "e", "l", "p", "ret"]:
            monitor.sendall(("sendkey " + key + "\n").encode())
            time.sleep(0.1)
        deadline = time.time() + 10
        while time.time() < deadline:
            if log_path.exists():
                log = log_path.read_text(encoding="utf-8", errors="replace")
                if "Z-OS shell commands" in log:
                    break
            time.sleep(0.2)
        else:
            raise CheckFailure("keyboard input did not execute the help command")
        check_log(log, ["z-os:/# help", "Z-OS shell commands", "usertest"], ["PANIC", "EXCEPTION"])
        print("keyboard input PASS")
    finally:
        if monitor is not None:
            try:
                monitor.close()
            except OSError:
                pass
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def run_shell():
    print("== interactive shell test ==")
    qemu = find_qemu()
    port = free_port()
    command = [qemu] + base_drive("z-os.img") + [
        "-display", "none",
        "-serial", "tcp:127.0.0.1:" + str(port) + ",server=on,wait=off",
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    connection = None
    session = None
    try:
        deadline = time.time() + 10
        while time.time() < deadline:
            if process.poll() is not None:
                raise CheckFailure("shell test QEMU exited early")
            try:
                connection = socket.create_connection(("127.0.0.1", port), timeout=0.5)
                break
            except OSError:
                time.sleep(0.2)
        if connection is None:
            raise CheckFailure("could not connect to the QEMU serial port")
        session = SerialSession(connection)
        session.send_line("")
        prompt_position = session.wait_for("z-os:", 15)
        time.sleep(0.3)
        cursor = prompt_position + 5
        tests = [
            ("help", ["Z-OS shell commands", "usertest", "shutdown"]),
            ("mkdir /tmp/verify", []),
            ("write /tmp/verify/file hello world", []),
            ("cat /tmp/verify/file", ["hello world"]),
            ("append /tmp/verify/file second line", []),
            ("cat /tmp/verify/file", ["second line"]),
            ("mv /tmp/verify/file /tmp/verify/renamed", []),
            ("ls /tmp/verify", ["renamed"]),
            ("cp /tmp/verify/renamed /tmp/verify/copy", []),
            ("stat /tmp/verify/copy", ["name:  copy", "size:  24 bytes"]),
            ("hexdump /tmp/verify/copy", ["00000000", "hello world"]),
            ("rm /tmp/verify/copy", []),
            ("rm /tmp/verify/renamed", []),
            ("rmdir /tmp/verify", []),
            ("mem", ["physical:", "heap:", "ramfs:"]),
            ("df", ["ramfs nodes:", "heap free:"]),
            ("uptime", ["up 0 days"]),
            ("ps", ["shell", "init"]),
            ("rand 8", []),
            ("usertest", ["Hello from Z-OS ring 3", "C ELF64 user program checksum verified", "status 0"]),
            ("cat /README", ["Z-OS is a small 64-bit operating system."]),
        ]
        for command_line, expected in tests:
            start = len(session.snapshot())
            session.send_line(command_line)
            prompt = session.wait_for("z-os:", 8, start + len(command_line) + 1)
            data = session.snapshot().decode("utf-8", errors="replace")
            segment = data[start:prompt]
            for marker in expected:
                if marker not in segment:
                    raise CheckFailure("command '" + command_line + "' missing output marker: " + marker + "\n" + segment)
            cursor = prompt + 5
        full_log = session.snapshot().decode("utf-8", errors="replace")
        check_log(full_log, ["Z-OS shell commands", "Hello from Z-OS ring 3"], ["PANIC", "EXCEPTION", "unknown command"])
        print(full_log)
        session.send_line("shutdown")
        try:
            exit_code = process.wait(timeout=6)
        except subprocess.TimeoutExpired:
            raise CheckFailure("ACPI shutdown did not power off QEMU")
        if exit_code != 0:
            raise CheckFailure("QEMU exited with status " + str(exit_code) + " after shutdown")
        print("ACPI shutdown PASS")
        print("interactive shell PASS")
    finally:
        if session is not None:
            session.close()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["build", "test", "smoke", "keyboard", "shell", "all"])
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    if args.action == "build":
        run_build()
        return
    if not args.skip_build:
        run_build()
    if not (BUILD / "z-os.img").exists() or not (BUILD / "z-os-test.img").exists():
        raise CheckFailure("build artifacts are missing")
    if args.action in ("test", "all"):
        run_selftest()
    if args.action in ("smoke", "all"):
        run_smoke()
    if args.action in ("keyboard", "all"):
        run_keyboard()
    if args.action in ("shell", "all"):
        run_shell()
    print("VERIFICATION PASS")


if __name__ == "__main__":
    try:
        main()
    except CheckFailure as error:
        print("VERIFICATION FAIL: " + str(error))
        sys.exit(1)
