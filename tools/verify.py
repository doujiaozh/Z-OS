import argparse
import hashlib
import json
import os
import re
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


def prepare_disk(name, size):
    path = BUILD / name
    path.parent.mkdir(parents=True, exist_ok=True)
    remaining = size
    with open(path, "wb") as handle:
        chunk = bytes(1024 * 1024)
        while remaining > 0:
            write_size = min(remaining, len(chunk))
            handle.write(chunk[:write_size])
            remaining -= write_size
    return path


def disk_drive(name):
    return ["-drive", "format=raw,file=" + str(BUILD / name) + ",if=ide,index=0"]


def run_selftest():
    print("== kernel self-test ==")
    qemu = find_qemu()
    prepare_disk("z-os-testdisk.img", 4 * 1024 * 1024)
    log_path = BUILD / "selftest.log"
    if log_path.exists():
        log_path.unlink()
    command = [qemu] + base_drive("z-os-test.img") + disk_drive("z-os-testdisk.img") + [
        "-display", "none",
        "-serial", "file:" + str(log_path),
        "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        exit_code = process.wait(timeout=150)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()
        raise CheckFailure("self-test timed out")
    log = log_path.read_text(encoding="utf-8", errors="replace") if log_path.exists() else ""
    print(log)
    if exit_code != QEMU_SUCCESS:
        raise CheckFailure("self-test QEMU exit code " + str(exit_code) + " expected " + str(QEMU_SUCCESS))
    check_log(log, ["ZTEST BEGIN", "ZTEST SUMMARY passed=267 failed=0", "ZTEST RESULT PASS"], ["[FAIL]", "PANIC", "EXCEPTION"])
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
            ("schedtest", ["multi: process started", "multi: process finished",
                           "schedtest: quantum=2 ticks", "preemptions="]),
            ("forktest", ["forktest: fork child resumed at parent code point",
                          "forktest: blocking wait returned child status",
                          "forktest: parent exit status 0"]),
            ("gfx test", ["gfx: framebuffer 800x600x32", "gfx: window manager composited 2 windows",
                          "gfx: composition verified", "gfx: test complete"]),
            ("postest", ["postest: posix primitives test started",
                         "postest: fd, credentials, signals, wait verified",
                         "postest: exit status 0"]),
            ("guitest2", ["guitest2: extended graphics test started", "guitest2: font text rendered",
                          "guitest2: mouse state reported", "guitest2: compositor surface verified",
                          "font pixels"]),
            ("cat /README", ["Z-OS is a small 64-bit operating system."]),
            ("diskinfo", ["ata present: no", "zfs mounted: no"]),
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


def monitor_read_vga_text(monitor_port, start_address, length):
    connection = None
    deadline = time.time() + 5
    while time.time() < deadline:
        try:
            connection = socket.create_connection(("127.0.0.1", monitor_port), timeout=0.5)
            break
        except OSError:
            time.sleep(0.2)
    if connection is None:
        raise CheckFailure("could not connect to the QEMU monitor")
    connection.settimeout(0.2)
    try:
        time.sleep(0.2)
        try:
            connection.recv(65536)
        except OSError:
            pass
        connection.sendall(("xp /%dxb %s\n" % (length, start_address)).encode())
        buffer = bytearray()
        end = time.time() + 4
        while time.time() < end:
            try:
                chunk = connection.recv(65536)
                if not chunk:
                    break
                buffer.extend(chunk)
                if b"(qemu)" in buffer:
                    break
            except socket.timeout:
                continue
            except OSError:
                break
    finally:
        try:
            connection.close()
        except OSError:
            pass
    dump = bytes(buffer).decode("utf-8", errors="replace")
    raw = bytearray()
    for line in dump.splitlines():
        match = re.match(r"^[0-9a-fA-F]{8,}:\s+(.*)$", line)
        if not match:
            continue
        for token in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)):
            raw.append(int(token, 16))
    return "".join(chr(raw[index]) for index in range(0, len(raw), 2))


def run_bootcheck():
    print("== boot fault check ==")
    qemu = find_qemu()
    manifest = json.loads((BUILD / "manifest.json").read_text(encoding="utf-8"))
    image = (BUILD / "z-os.img").read_bytes()
    stages = [
        ("stage1", image[0:manifest["stage1_size"]], manifest["stage1_sha256"]),
        ("stage2", image[512:512 + manifest["stage2_size"]], manifest["stage2_sha256"]),
        ("kernel", image[33 * 512:33 * 512 + manifest["kernel_size"]], manifest["kernel_sha256"]),
    ]
    for name, data, expected in stages:
        actual = hashlib.sha256(data).hexdigest()
        if actual != expected:
            raise CheckFailure("image " + name + " digest does not match a fresh build")
    print("image integrity PASS")

    targets = [
        ("z-os.img", ["Z-OS", "EFER.LME verified", "Boot complete. Starting shell.", "z-os:"], 25, True),
        ("z-os-test.img", ["Z-OS", "EFER.LME verified", "ZTEST BEGIN"], 40, False),
    ]
    for image_name, markers, deadline_seconds, expect_prompt in targets:
        serial_path = BUILD / (image_name.replace(".", "_") + ".serial.log")
        trace_path = BUILD / (image_name.replace(".", "_") + ".trace.log")
        for path in (serial_path, trace_path):
            if path.exists():
                path.unlink()
        monitor_port = free_port()
        command = [qemu] + base_drive(image_name) + [
            "-display", "none",
            "-serial", "file:" + str(serial_path),
            "-monitor", "tcp:127.0.0.1:" + str(monitor_port) + ",server=on,wait=off",
            "-d", "int,cpu_reset,guest_errors",
            "-D", str(trace_path),
        ]
        process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + deadline_seconds
        log = ""
        reached = False
        try:
            while time.time() < deadline:
                if process.poll() is not None:
                    break
                if serial_path.exists():
                    log = serial_path.read_text(encoding="utf-8", errors="replace")
                    if all(marker in log for marker in markers):
                        reached = True
                        break
                time.sleep(0.2)
            if serial_path.exists():
                log = serial_path.read_text(encoding="utf-8", errors="replace")
            trace = trace_path.read_text(encoding="utf-8", errors="replace") if trace_path.exists() else ""
            triple_faults = trace.count("Triple fault")
            if triple_faults != 0:
                raise CheckFailure(image_name + " reported " + str(triple_faults) + " triple faults")
            page_faults = trace.count("v=0e")
            if page_faults != 0:
                raise CheckFailure(image_name + " reported " + str(page_faults) + " page fault exceptions during boot")
            for marker in markers:
                if marker not in log:
                    raise CheckFailure(image_name + " missing boot marker: " + marker + "\n" + log)
            vga_text = monitor_read_vga_text(monitor_port, "0xb8000", 4000)
            for forbidden in ("PAGING ERROR", "LONG MODE ERROR"):
                if forbidden in vga_text:
                    raise CheckFailure(image_name + " vga screen shows loader error: " + forbidden)
            if expect_prompt:
                if "Z-OS" not in vga_text:
                    raise CheckFailure(image_name + " vga screen does not show the kernel banner")
                if "z-os" not in vga_text:
                    raise CheckFailure(image_name + " vga screen does not show the shell prompt")
                if "EFER.LME" not in vga_text:
                    raise CheckFailure(image_name + " vga screen does not show the long mode report")
            print(image_name + " boot trace clean, serial and vga markers found")
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    print("boot fault check PASS")


class MonitorSession:
    def __init__(self, port):
        self.connection = None
        deadline = time.time() + 8
        while time.time() < deadline:
            try:
                self.connection = socket.create_connection(("127.0.0.1", port), timeout=0.5)
                break
            except OSError:
                time.sleep(0.2)
        if self.connection is None:
            raise CheckFailure("could not connect to the QEMU monitor")
        self.connection.settimeout(0.2)
        time.sleep(0.2)
        try:
            self.connection.recv(65536)
        except OSError:
            pass

    def command(self, text, timeout=4):
        self.connection.sendall((text + "\n").encode())
        buffer = bytearray()
        end = time.time() + timeout
        while time.time() < end:
            try:
                chunk = self.connection.recv(65536)
                if not chunk:
                    break
                buffer.extend(chunk)
                if b"(qemu)" in buffer:
                    break
            except socket.timeout:
                continue
            except OSError:
                break
        return bytes(buffer).decode("utf-8", errors="replace")

    def sendkey(self, key):
        self.command("sendkey " + key)

    def screendump(self, path):
        target = os.path.abspath(str(path)).replace("\\", "/")
        self.command("screendump " + target, timeout=6)
        if not path.exists():
            raise CheckFailure("QEMU screendump did not produce " + str(path))

    def vga_text(self):
        dump = self.command("xp /4000xb 0xb8000")
        raw = bytearray()
        for line in dump.splitlines():
            match = re.match(r"^[0-9a-fA-F]{8,}:\s+(.*)$", line)
            if not match:
                continue
            for token in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)):
                raw.append(int(token, 16))
        return "".join(chr(raw[index]) for index in range(0, len(raw), 2))

    def close(self):
        try:
            self.connection.close()
        except OSError:
            pass


def attach_monitor(host_port):
    return MonitorSession(host_port)


def run_storage():
    print("== storage persistence test ==")
    qemu = find_qemu()
    prepare_disk("z-os-storage.img", 4 * 1024 * 1024)
    first_commands = [
        ("diskinfo", ["ata present: yes", "ata model: QEMU HARDDISK"]),
        ("format", ["format: zfs formatted"]),
        ("zmount", ["zmount: 0 files attached under /disk"]),
        ("write /disk/persist.txt persistent-disk-data", []),
        ("zsync", ["zsync: /disk written to zfs"]),
        ("zls", ["persist.txt", "zfs files: 1"]),
        ("cat /disk/persist.txt", ["persistent-disk-data"]),
    ]
    second_commands = [
        ("zmount", ["zmount: 1 files attached under /disk"]),
        ("zls", ["persist.txt", "zfs files: 1"]),
        ("cat /disk/persist.txt", ["persistent-disk-data"]),
    ]

    def boot_with_commands(commands, label):
        port = free_port()
        command = [qemu] + base_drive("z-os.img") + disk_drive("z-os-storage.img") + [
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
                    raise CheckFailure(label + " QEMU exited early")
                try:
                    connection = socket.create_connection(("127.0.0.1", port), timeout=0.5)
                    break
                except OSError:
                    time.sleep(0.2)
            if connection is None:
                raise CheckFailure("could not connect to the serial port for " + label)
            session = SerialSession(connection)
            session.send_line("")
            session.wait_for("z-os:", 20)
            for command_line, expected in commands:
                start = len(session.snapshot())
                session.send_line(command_line)
                prompt = session.wait_for("z-os:", 20, start + len(command_line) + 1)
                segment = session.snapshot().decode("utf-8", errors="replace")[start:prompt]
                for marker in expected:
                    if marker not in segment:
                        raise CheckFailure(label + " command '" + command_line +
                                           "' missing marker: " + marker + "\n" + segment)
            session.send_line("shutdown")
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                raise CheckFailure(label + " did not power off")
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

    boot_with_commands(first_commands, "storage-first-boot")
    print("first boot wrote and flushed persistent data")
    boot_with_commands(second_commands, "storage-second-boot")
    print("second boot recovered the data from disk")
    print("storage persistence PASS")


def read_ppm(path):
    data = Path(path).read_bytes()
    if not data.startswith(b"P6"):
        raise CheckFailure("screendump is not a binary PPM file: " + str(path))
    fields = []
    index = 2
    while len(fields) < 3:
        while index < len(data) and data[index:index + 1].isspace():
            index += 1
        if data[index:index + 1] == b"#":
            while index < len(data) and data[index:index + 1] != b"\n":
                index += 1
            continue
        start = index
        while index < len(data) and not data[index:index + 1].isspace():
            index += 1
        fields.append(int(data[start:index]))
    index += 1
    width, height, maximum = fields
    if maximum != 255:
        raise CheckFailure("screendump maximum channel value is not 255")
    pixels = data[index:index + width * height * 3]
    if len(pixels) != width * height * 3:
        raise CheckFailure("screendump pixel payload is truncated")
    return width, height, pixels


def ppm_pixel(ppm, x, y):
    width, height, pixels = ppm
    if x < 0 or y < 0 or x >= width or y >= height:
        raise CheckFailure("pixel coordinate outside the screendump")
    offset = (y * width + x) * 3
    return (pixels[offset], pixels[offset + 1], pixels[offset + 2])


def ppm_count_color(ppm, color):
    width, height, pixels = ppm
    target = bytes(color)
    count = 0
    total = width * height
    for index in range(total):
        offset = index * 3
        if pixels[offset:offset + 3] == target:
            count += 1
    return count


def run_gui():
    print("== gui desktop test ==")
    qemu = find_qemu()
    port = free_port()
    monitor_port = free_port()
    command = [qemu] + base_drive("z-os.img") + [
        "-display", "none",
        "-serial", "tcp:127.0.0.1:" + str(port) + ",server=on,wait=off",
        "-monitor", "tcp:127.0.0.1:" + str(monitor_port) + ",server=on,wait=off",
    ]
    process = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    connection = None
    session = None
    monitor = None
    try:
        deadline = time.time() + 10
        while time.time() < deadline:
            if process.poll() is not None:
                raise CheckFailure("gui QEMU exited early")
            try:
                connection = socket.create_connection(("127.0.0.1", port), timeout=0.5)
                break
            except OSError:
                time.sleep(0.2)
        if connection is None:
            raise CheckFailure("could not connect to the gui serial port")
        session = SerialSession(connection)
        session.send_line("")
        session.wait_for("z-os:", 20)
        session.send_line("gui")
        session.wait_for("gui: desktop active", 20)
        monitor = MonitorSession(monitor_port)
        time.sleep(0.5)
        monitor.command("mouse_move 0 0")
        time.sleep(0.8)
        shot = BUILD / "gui-desktop.ppm"
        monitor.screendump(shot)
        ppm = read_ppm(shot)
        if (ppm[0], ppm[1]) != (800, 600):
            raise CheckFailure("gui desktop resolution is " + str((ppm[0], ppm[1])))
        status_pixel = ppm_pixel(ppm, 5, 5)
        taskbar_pixel = ppm_pixel(ppm, 5, 590)
        wallpaper_pixel = ppm_pixel(ppm, 400, 300)
        icon_pixel = ppm_pixel(ppm, 30, 55)
        if status_pixel != (0x20, 0x30, 0x40):
            raise CheckFailure("gui status bar pixel mismatch: " + str(status_pixel))
        if taskbar_pixel != (0x1B, 0x24, 0x30):
            raise CheckFailure("gui taskbar pixel mismatch: " + str(taskbar_pixel))
        if wallpaper_pixel != (0x10, 0x18, 0x20):
            raise CheckFailure("gui wallpaper pixel mismatch: " + str(wallpaper_pixel))
        if icon_pixel != (0x2F, 0x41, 0x52):
            raise CheckFailure("gui desktop icon pixel mismatch: " + str(icon_pixel))
        def last_pointer_position():
            matches = re.findall(r"gui: mouse move x=(-?\d+) y=(-?\d+)", session.snapshot().decode("utf-8", errors="replace"))
            if not matches:
                return None
            return int(matches[-1][0]), int(matches[-1][1])

        def clamp_delta(value):
            if value > 120:
                return 120
            if value < -120:
                return -120
            return value

        def move_pointer(target_x, target_y, attempts=16):
            for attempt in range(attempts):
                position = last_pointer_position()
                if position is None:
                    raise CheckFailure("mouse movement did not reach the desktop")
                current_x, current_y = position
                if abs(current_x - target_x) <= 2 and abs(current_y - target_y) <= 2:
                    return current_x, current_y
                start = len(session.snapshot())
                monitor.command("mouse_move %d %d" % (clamp_delta(target_x - current_x),
                                                      clamp_delta(target_y - current_y)), timeout=1)
                deadline = time.time() + 6
                while time.time() < deadline:
                    new_position = last_pointer_position()
                    if new_position is not None and new_position != position:
                        break
                    time.sleep(0.05)
            position = last_pointer_position()
            if position is None or abs(position[0] - target_x) > 4 or abs(position[1] - target_y) > 4:
                raise CheckFailure("mouse pointer did not reach the requested target: " + str(position))
            return position

        def cursor_color_counts(ppm, x, y, size=16):
            black = 0
            white = 0
            for row in range(size):
                for column in range(size):
                    pixel = ppm_pixel(ppm, x + column, y + row)
                    if pixel == (0, 0, 0):
                        black += 1
                    elif pixel == (255, 255, 255):
                        white += 1
            return black, white

        move_pointer(500, 300)
        time.sleep(0.3)
        pointer_shot = BUILD / "gui-pointer.ppm"
        monitor.screendump(pointer_shot)
        cursor_ppm = read_ppm(pointer_shot)
        black, white = cursor_color_counts(cursor_ppm, 500, 300)
        if black < 30 or white < 40:
            raise CheckFailure("mouse cursor outline is not rendered: black=" +
                               str(black) + " white=" + str(white))
        move_pointer(640, 420)
        time.sleep(0.3)
        moved_shot = BUILD / "gui-pointer-moved.ppm"
        monitor.screendump(moved_shot)
        moved_ppm = read_ppm(moved_shot)
        old_black, old_white = cursor_color_counts(moved_ppm, 500, 300)
        new_black, new_white = cursor_color_counts(moved_ppm, 640, 420)
        if old_black != 0 or old_white != 0:
            raise CheckFailure("mouse cursor left a ghost at the previous position")
        if new_black < 30 or new_white < 40:
            raise CheckFailure("mouse cursor was not redrawn at the new position")
        print("gui cursor outline and ghost-free repaint PASS")

        hover_x, hover_y = move_pointer(60, 70)
        if hover_x != 60 or hover_y != 70:
            raise CheckFailure("mouse pointer tracking is inaccurate")
        monitor.command("mouse_button 1", timeout=1)
        time.sleep(0.2)
        monitor.command("mouse_button 0", timeout=1)
        session.wait_for("gui: opened Files", 15)
        monitor.sendkey("1")
        session.wait_for("gui: opened Files", 15)
        time.sleep(0.5)
        shot_window = BUILD / "gui-window.ppm"
        monitor.screendump(shot_window)
        window_ppm = read_ppm(shot_window)
        title_pixels = ppm_count_color(window_ppm, (0x2F, 0x6F, 0xB5))
        if title_pixels < 500:
            raise CheckFailure("gui window title bar is not rendered")
        move_pointer(250, 68)
        monitor.command("mouse_button 1", timeout=1)
        time.sleep(0.2)
        monitor.command("mouse_move 40 30", timeout=1)
        time.sleep(0.4)
        monitor.command("mouse_button 0", timeout=1)
        session.wait_for("gui: window dragged", 15)
        print("gui mouse click and drag events PASS")
        frame_match = None
        session.send_line("exit")
        session.wait_for("gui: exited desktop", 20)
        frame_matches = re.findall(r"gui: frames full=(\d+) dirty=(\d+) moves=(\d+) move_full=(\d+)",
                                   session.snapshot().decode("utf-8", errors="replace"))
        if not frame_matches:
            raise CheckFailure("gui did not report frame statistics")
        frame_match = [int(value) for value in frame_matches[-1]]
        full_frames, dirty_frames, move_events, move_full_frames = frame_match
        if move_events < 4:
            raise CheckFailure("gui did not report enough mouse movement frames")
        if move_full_frames != 0:
            raise CheckFailure("gui repainted the full screen during mouse movement")
        if dirty_frames < 1:
            raise CheckFailure("gui never used a dirty rectangle present")
        print("gui double buffer statistics PASS")
        session.wait_for("z-os:", 20)
        time.sleep(0.5)
        text = monitor.vga_text()
        if "Z-OS" not in text and "z-os" not in text:
            raise CheckFailure("gui exit did not restore the text console")
        text_shot = BUILD / "gui-textmode.ppm"
        monitor.screendump(text_shot)
        text_ppm = read_ppm(text_shot)
        text_width, text_height, text_pixels = text_ppm
        light_pixels = 0
        dark_pixels = 0
        white_line_rows = 0
        for row in range(text_height):
            row_light = 0
            for column in range(text_width):
                offset = (row * text_width + column) * 3
                red = text_pixels[offset]
                green = text_pixels[offset + 1]
                blue = text_pixels[offset + 2]
                if red > 200 and green > 200 and blue > 200:
                    row_light += 1
                if red < 40 and green < 40 and blue < 40:
                    dark_pixels += 1
            light_pixels += row_light
            if row_light > int(text_width * 0.8):
                white_line_rows += 1
        total_pixels = text_width * text_height
        if white_line_rows != 0:
            raise CheckFailure("gui exit left " + str(white_line_rows) + " white line rows on the text screen")
        if light_pixels > int(total_pixels * 0.1):
            raise CheckFailure("gui exit text screen is not mostly dark")
        if dark_pixels < int(total_pixels * 0.5):
            raise CheckFailure("gui exit text screen lost its dark background")
        print("gui exit text screen is a clean text console PASS")
        start = len(session.snapshot())
        session.send_line("cat /README")
        prompt = session.wait_for("z-os:", 10, start + 12)
        segment = session.snapshot().decode("utf-8", errors="replace")[start:prompt]
        if "Z-OS is a small 64-bit operating system." not in segment:
            raise CheckFailure("shell did not resume after gui exit")
        print("gui desktop pixels, window chrome, and mode switch PASS")
        session.send_line("shutdown")
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            raise CheckFailure("gui test did not power off")
    finally:
        if monitor is not None:
            monitor.close()
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
    parser.add_argument("action", choices=["build", "test", "smoke", "bootcheck", "storage", "gui", "keyboard", "shell", "all"])
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
    if args.action in ("bootcheck", "all"):
        run_bootcheck()
    if args.action in ("storage", "all"):
        run_storage()
    if args.action in ("gui", "all"):
        run_gui()
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
