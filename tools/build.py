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
    "-Ikernel/mm",
    "-Ikernel/task",
    "-Ikernel/gui",
    "-Ikernel/fs",
    "-Ikernel/drivers",
    "-Ikernel/net",
]
USER_CFLAGS = [
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
    "-Werror",
    "-O2",
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


USER_PROGRAMS = ["init", "multi", "forktest", "guitest", "postest", "guitest2", "nettest2"]


def build_user():
    nasm, clang, lld, _ = tools()
    linker = ROOT / "user" / "user.ld"
    elf_paths = []
    for name in USER_PROGRAMS:
        source = ROOT / "user" / (name + ".c")
        object_path = BUILD / ("user_" + name + ".o")
        elf_path = BUILD / ("user_" + name + ".elf")
        run([clang] + USER_CFLAGS + ["-I", str(ROOT / "user"), "-c", str(source), "-o", str(object_path)])
        run([lld, "-m", "elf_x86_64", "-z", "noexecstack", "-T", str(linker),
             "-o", str(elf_path), str(object_path)])
        image = elf_path.read_bytes()
        if len(image) < 64 or image[0:4] != b"\x7fELF" or image[4] != 2 or image[5] != 1:
            raise SystemExit("user ELF header is invalid: " + name)
        if int.from_bytes(image[16:18], "little") != 2:
            raise SystemExit("user ELF is not an executable: " + name)
        if int.from_bytes(image[18:20], "little") != 62:
            raise SystemExit("user ELF is not x86_64: " + name)
        elf_paths.append(elf_path)
    embed_path = BUILD / "user_program.o"
    run([nasm, "-f", "elf64", "-I", str(BUILD) + os.sep, str(ROOT / "user" / "program.asm"),
         "-o", str(embed_path)])
    return embed_path, elf_paths


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
    stage1_bytes = stage1.read_bytes()
    stage2_bytes = stage2.read_bytes()
    user_obj, user_elfs = build_user()
    release = compile_kernel("release", [], [user_obj])
    test = compile_kernel("selftest", ["-DZ_SELFTEST"], [user_obj])
    image = make_image(release, stage1, stage2, "z-os.img")
    test_image = make_image(test, stage1, stage2, "z-os-test.img")
    manifest = {
        "kernel_size": release.stat().st_size,
        "kernel_sha256": hashlib.sha256(release.read_bytes()).hexdigest(),
        "test_kernel_size": test.stat().st_size,
        "stage1_size": len(stage1_bytes),
        "stage1_sha256": hashlib.sha256(stage1_bytes).hexdigest(),
        "stage2_size": len(stage2_bytes),
        "stage2_sha256": hashlib.sha256(stage2_bytes).hexdigest(),
        "image_size": image.stat().st_size,
        "image_sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        "test_image_sha256": hashlib.sha256(test_image.read_bytes()).hexdigest(),
        "user_programs": {},
        "user_object": str(user_obj.relative_to(ROOT)),
    }
    for elf in user_elfs:
        manifest["user_programs"][elf.stem] = {
            "size": elf.stat().st_size,
            "sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
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
