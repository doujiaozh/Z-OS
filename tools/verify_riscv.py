import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / "build" / "riscv" / "zos-riscv.bin"
EMULATOR_CANDIDATES = [
    Path(os.environ["RV32EMU"]) if os.environ.get("RV32EMU") else None,
    ROOT.parent / "RISC-V-Emu" / "build" / "rv32emu.exe",
    ROOT.parent / "RISC-V-Emu" / "build" / "rv32emu",
    ROOT.parent / "rv32emu-zos" / "build" / "rv32emu.exe",
    ROOT.parent / "rv32emu-zos" / "build" / "rv32emu",
]
EXPECTED_MARKERS = [
    "Z-OS",
    "HAL: riscv32",
    "SV32 paging enabled",
    "kernel self-test: uart transmit ready PASS",
    "kernel self-test: sv32 mode and root page table PASS",
    "kernel self-test: sv32 kernel identity mapping PASS",
    "kernel self-test: sv32 user code and data mapping PASS",
    "kernel self-test: ram read and write PASS",
    "kernel self-test: context switch preserves state PASS",
    "kernel self-test: machine timer interrupt PASS",
    "kernel self-test: no unexpected traps PASS",
    "z-os: boot banner OK",
    "z-os: kernel self-test complete",
]
MAX_STEPS = "5000000"


def locate_emulator():
    for candidate in EMULATOR_CANDIDATES:
        if candidate is not None and candidate.exists():
            return candidate
    return None


def main():
    if not BINARY.exists():
        print("missing riscv kernel binary: " + str(BINARY))
        print("run: python tools/build.py build --arch riscv")
        return 2
    emulator = locate_emulator()
    if emulator is None:
        print("missing rv32 emulator; set RV32EMU to the emulator binary")
        return 2
    completed = subprocess.run([str(emulator), "--max-steps", MAX_STEPS, str(BINARY)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    stdout = completed.stdout.decode(errors="replace")
    stderr = completed.stderr.decode(errors="replace")
    print(stdout, end="")
    if stderr.strip():
        print(stderr, end="")
    failures = 0
    for marker in EXPECTED_MARKERS:
        if marker not in stdout:
            print("FAIL marker missing: " + marker)
            failures += 1
    if "FAIL" in stdout:
        print("FAIL kernel self-test reported a failure")
        failures += 1
    if completed.returncode != 0:
        print("FAIL emulator exit code " + str(completed.returncode))
        failures += 1
    if failures != 0:
        print(str(failures) + " riscv check(s) failed")
        return 1
    print("z-os riscv32 boot verification passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
