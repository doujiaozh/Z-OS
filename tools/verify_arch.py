import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
EMULATOR_CANDIDATES = [
    Path(os.environ["RV32EMU"]) if os.environ.get("RV32EMU") else None,
    ROOT.parent / "RISC-V-Emu" / "build" / "rv32emu.exe",
    ROOT.parent / "RISC-V-Emu" / "build" / "rv32emu",
    ROOT.parent / "rv32emu-zos" / "build" / "rv32emu.exe",
    ROOT.parent / "rv32emu-zos" / "build" / "rv32emu",
]
RISCV_BINARY = BUILD / "riscv" / "zos-riscv.bin"
RISCV_MANIFEST = BUILD / "riscv" / "riscv-manifest.json"
X86_MANIFEST = BUILD / "manifest.json"
MAX_STEPS = "5000000"
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


def locate_emulator():
    for candidate in EMULATOR_CANDIDATES:
        if candidate is not None and candidate.exists():
            return candidate
    return None


def check_x86_manifest():
    failures = 0
    if not X86_MANIFEST.exists():
        print("FAIL x86: missing manifest (run the default ARCH build first)")
        return 1
    manifest = json.loads(X86_MANIFEST.read_text(encoding="utf-8"))
    if manifest.get("arch") != "x86_64":
        print("FAIL x86: unexpected arch in manifest")
        failures += 1
    image = BUILD / "z-os.img"
    if not image.exists() or image.stat().st_size != manifest.get("image_size"):
        print("FAIL x86: boot image size mismatch")
        failures += 1
    else:
        print("PASS x86: boot image built (" + str(image.stat().st_size) + " bytes)")
    if manifest.get("kernel_size", 0) <= 0:
        print("FAIL x86: empty kernel image")
        failures += 1
    else:
        print("PASS x86: kernel image built (" + str(manifest["kernel_size"]) + " bytes)")
    return failures


def check_riscv(emulator):
    failures = 0
    if not RISCV_BINARY.exists():
        print("FAIL riscv: missing kernel binary (run ARCH=riscv build first)")
        return 1
    manifest = {}
    if RISCV_MANIFEST.exists():
        manifest = json.loads(RISCV_MANIFEST.read_text(encoding="utf-8"))
        if manifest.get("arch") != "riscv32":
            print("FAIL riscv: unexpected arch in manifest")
            failures += 1
        if manifest.get("binary_size") != RISCV_BINARY.stat().st_size:
            print("FAIL riscv: binary size mismatch")
            failures += 1
    print("PASS riscv: kernel binary built (" + str(RISCV_BINARY.stat().st_size) + " bytes)")
    if emulator is None:
        print("FAIL riscv: emulator binary not found")
        return failures + 1
    completed = subprocess.run([str(emulator), "--max-steps", MAX_STEPS, str(RISCV_BINARY)],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
    stdout = completed.stdout.decode(errors="replace")
    for marker in EXPECTED_MARKERS:
        if marker not in stdout:
            print("FAIL riscv: marker missing: " + marker)
            failures += 1
    if "FAIL" in stdout:
        print("FAIL riscv: kernel self-test reported a failure")
        failures += 1
    if completed.returncode != 0:
        print("FAIL riscv: emulator exit code " + str(completed.returncode))
        failures += 1
    if failures == 0:
        print("PASS riscv: kmain reached and boot banner printed")
        print("PASS riscv: kernel self-test suite passed")
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-riscv-run", action="store_true")
    parsed = parser.parse_args()
    failures = check_x86_manifest()
    if parsed.no_riscv_run:
        if not RISCV_BINARY.exists():
            print("FAIL riscv: missing kernel binary")
            failures += 1
        else:
            print("PASS riscv: kernel binary present")
    else:
        failures += check_riscv(locate_emulator())
    if failures != 0:
        print(str(failures) + " verification step(s) failed")
        return 1
    print("all z-os architecture checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
