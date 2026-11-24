import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
KERNEL_SECTORS = 640
STAGE2_SECTORS = 32
IMAGE_SIZE = 1474560
KERNEL_LIMIT = KERNEL_SECTORS * 512
CFLAGS = [
    "--target=x86_64-none-elf",
    "-std=c11",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-fno-pic",
    "-fno-pie",
    "-fno-omit-frame-pointer",
    "-fno-asynchronous-unwind-tables",
    "-mno-red-zone",
    "-mcmodel=small",
    "-mno-mmx",
    "-mno-sse",
    "-mno-sse2",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-O2",
    "-g",
    "-Ikernel/include",
]


def which_tool(name, candidates):
    found = shutil.which(name)
    if found:
        return found
    for candidate in candidates:
        path = Path(candidate)
        if path.exists():
            return str(path)
    raise SystemExit("missing required tool: " + name)


def tools():
    nasm = which_tool("nasm", [r"D:\NASM\nasm.exe"])
    clang = which_tool("clang", [r"D:\Lean4\lean-4.18.0-windows\bin\clang.exe"])
    lld = which_tool("ld.lld", [r"D:\Lean4\lean-4.18.0-windows\bin\ld.lld.exe"])
    objcopy = which_tool("objcopy", [r"D:\mingw64\bin\objcopy.exe"])
    return nasm, clang, lld, objcopy


def run(command):
    printable = " ".join(str(part) for part in command)
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        print(printable)
        print(result.stdout)
        raise SystemExit("command failed")
    if result.stdout.strip():
        print(result.stdout.strip())


def clean():
    if BUILD.exists():
        shutil.rmtree(BUILD)
    BUILD.mkdir(parents=True, exist_ok=True)


def compile_kernel(tag, defines, extra_objects):
    nasm, clang, lld, objcopy = tools()
    objects = list(extra_objects)
    for source in sorted((ROOT / "kernel").rglob("*.c")):
        relative = source.relative_to(ROOT)
        obj = BUILD / tag / "obj" / relative.with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        command = [clang] + CFLAGS + defines + ["-c", str(source), "-o", str(obj)]
        run(command)
        objects.append(obj)
    asm_sources = sorted((ROOT / "kernel").rglob("*.asm"))
    for source in asm_sources:
        relative = source.relative_to(ROOT)
        obj = BUILD / tag / "obj" / relative.with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        command = [nasm, "-f", "elf64", "-I", str(ROOT / "build") + os.sep, str(source), "-o", str(obj)]
        run(command)
        objects.append(obj)
    elf = BUILD / "z-os.elf" if tag == "release" else BUILD / ("z-os-" + tag + ".elf")
    flat = BUILD / "z-os.bin" if tag == "release" else BUILD / ("z-os-" + tag + ".bin")
    command = [lld, "-m", "elf_x86_64", "-z", "noexecstack", "-T", str(ROOT / "kernel" / "link.ld"), "-o", str(elf)]
    command += [str(obj) for obj in objects]
    run(command)
    command = [objcopy, "-O", "binary", str(elf), str(flat)]
    run(command)
    if flat.stat().st_size > KERNEL_LIMIT:
        raise SystemExit("kernel image exceeds loader limit")
    return flat


def build_boot():
    nasm, _, _, _ = tools()
    stage1 = BUILD / "stage1.bin"
    stage2 = BUILD / "stage2.bin"
    run([nasm, "-f", "bin", str(ROOT / "boot" / "stage1.asm"), "-o", str(stage1)])
    run([nasm, "-f", "bin", str(ROOT / "boot" / "stage2.asm"), "-o", str(stage2)])
    if stage1.stat().st_size != 512:
        raise SystemExit("stage1 size mismatch")
    if stage2.stat().st_size != STAGE2_SECTORS * 512:
        raise SystemExit("stage2 size mismatch")
    return stage1, stage2


def build_user():
    nasm, _, _, _ = tools()
    flat = BUILD / "user_hello.bin"
    obj = BUILD / "user_program.o"
    run([nasm, "-f", "bin", str(ROOT / "user" / "hello.asm"), "-o", str(flat)])
    run([nasm, "-f", "elf64", "-I", str(BUILD) + os.sep, str(ROOT / "user" / "program.asm"), "-o", str(obj)])
    return obj


def make_image(kernel_flat, stage1, stage2, name):
    data = bytearray(IMAGE_SIZE)
    data[0:512] = stage1.read_bytes()
    stage2_data = stage2.read_bytes()
    data[512:512 + len(stage2_data)] = stage2_data
    kernel_data = kernel_flat.read_bytes()
    offset = (1 + STAGE2_SECTORS) * 512
    if offset + len(kernel_data) > IMAGE_SIZE:
        raise SystemExit("kernel does not fit in image")
    data[offset:offset + len(kernel_data)] = kernel_data
    path = BUILD / name
    path.write_bytes(data)
    return path


def build():
    clean()
    stage1, stage2 = build_boot()
    user_obj = build_user()
    release = compile_kernel("release", [], [user_obj])
    test = compile_kernel("selftest", ["-DZ_SELFTEST"], [user_obj])
    image = make_image(release, stage1, stage2, "z-os.img")
    test_image = make_image(test, stage1, stage2, "z-os-test.img")
    manifest = {
        "kernel_size": release.stat().st_size,
        "test_kernel_size": test.stat().st_size,
        "image_size": image.stat().st_size,
        "image_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        "test_image_sha256": hashlib.sha256(test_image.read_bytes()).hexdigest(),
        "user_object": str(user_obj.relative_to(ROOT)),
    }
    (BUILD / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", nargs="?", default="build", choices=["build", "clean"])
    args = parser.parse_args()
    if args.action == "clean":
        clean()
        print("build directory cleaned")
        return
    build()


if __name__ == "__main__":
    main()
