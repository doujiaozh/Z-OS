import socket
import subprocess
import sys
import time
import pathlib

ROOT = pathlib.Path(r"D:\Code\Z-OS-push")
QEMU = r"D:\QEMU\qemu-system-x86_64.exe"


def recv_until(sock, marker, deadline, sink):
    while time.time() < deadline:
        try:
            chunk = sock.recv(4096)
            if chunk:
                sink.append(chunk)
                if marker in b"".join(sink):
                    return True
        except socket.timeout:
            pass
        except OSError:
            return False
    return False


def key_name(character):
    if character == " ":
        return "spc"
    if character.isdigit():
        return character
    if character.isalpha():
        if character.isupper():
            return "shift-" + character.lower()
        return character
    return None


def send_text(sock, text):
    for character in text:
        name = key_name(character)
        if name is None:
            continue
        sock.sendall(f"sendkey {name}\n".encode())
        time.sleep(0.06)
    sock.sendall(b"sendkey ret\n")
    time.sleep(0.3)


def main():
    port = int(sys.argv[1])
    command = sys.argv[2]
    marker = sys.argv[3].encode()
    log = ROOT / "build" / "serial_capture.log"
    if log.exists():
        log.unlink()
    monitor_port = port + 1
    cmd = [QEMU,
           "-drive", f"format=raw,file={ROOT / 'disk' / 'hd.img'},if=ide,index=0,media=disk",
           "-boot", "c", "-m", "256M", "-cpu", "max", "-no-reboot",
           "-display", "none", "-serial", f"file:{log}",
           "-monitor", f"tcp:127.0.0.1:{monitor_port},server=on,wait=off"]
    process = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    monitor = None
    try:
        deadline = time.time() + 180
        while time.time() < deadline:
            if process.poll() is not None:
                print("QEMU_EXITED", process.returncode)
                return
            if log.exists() and "z-os:" in log.read_text(encoding="utf-8", errors="replace"):
                break
            time.sleep(1.0)
        print("BOOT_OK")
        for _ in range(40):
            try:
                monitor = socket.create_connection(("127.0.0.1", monitor_port), timeout=1.0)
                break
            except OSError:
                time.sleep(0.5)
        if monitor is None:
            print("NO_MONITOR")
            return
        monitor.settimeout(0.5)
        monitor.recv(4096)
        monitor.sendall(b"info status\n")
        time.sleep(0.5)
        send_text(monitor, command)
        print("COMMAND_SENT")
        deadline = time.time() + 2400
        while time.time() < deadline:
            if log.exists():
                text = log.read_text(encoding="utf-8", errors="replace")
                if marker.decode() in text:
                    time.sleep(1.0)
                    break
            if process.poll() is not None:
                break
            time.sleep(1.0)
        if log.exists():
            print(log.read_text(encoding="utf-8", errors="replace")[-3000:])
    finally:
        if monitor is not None:
            try:
                monitor.close()
            except OSError:
                pass
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == "__main__":
    main()
