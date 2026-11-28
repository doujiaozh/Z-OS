import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
KERNEL_SECTORS = 640
STAGE2_SECTORS = 32
IMAGE_SIZE = 1474560
KERNEL_LIMIT = KERNEL_SECTORS * 512
RISCV_MARCH = "rv32im_zicsr"
RISCV_MABI = "ilp32"
RISCV_TOOLCHAIN_CANDIDATES = [
    r"D:\Code\.toolchains\riscv32\mingw64\bin",
]
X86_CFLAGS = [
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
    "-fstack-protector-strong",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-O2",
    "-g",
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
RISCV_CFLAGS = [
    "-march=" + RISCV_MARCH,
    "-mabi=" + RISCV_MABI,
    "-std=c11",
    "-ffreestanding",
    "-fno-builtin",
    "-fno-stack-protector",
    "-fno-pic",
    "-fno-pie",
    "-fno-asynchronous-unwind-tables",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wall",
    "-Wextra",
    "-Wno-unused-parameter",
    "-O2",
    "-g",
]
KERNEL_INCLUDE_DIRS = [
    "-Ikernel/include",
    "-Ikernel/mm",
    "-Ikernel/task",
    "-Ikernel/gui",
    "-Ikernel/fs",
    "-Ikernel/drivers",
    "-Ikernel/net",
]
X86_ARCH_DIRS = {Path("kernel/arch/x86_64")}
RISCV_ARCH_DIRS = {Path("kernel/arch/riscv32")}


def which_tool(name, candidates):
    found = shutil.which(name)
    if found:
        return found
    for candidate in candidates:
        path = Path(candidate)
        if path.exists():
            return str(path)
    raise SystemExit("missing required tool: " + name)


def x86_tools():
    nasm = which_tool("nasm", [r"D:\NASM\nasm.exe"])
    clang = which_tool("clang", [r"D:\Lean4\lean-4.18.0-windows\bin\clang.exe"])
    lld = which_tool("ld.lld", [r"D:\Lean4\lean-4.18.0-windows\bin\ld.lld.exe"])
    objcopy = which_tool("objcopy", [r"D:\mingw64\bin\objcopy.exe"])
    return nasm, clang, lld, objcopy


def riscv_bin_dir():
    override = os.environ.get("RISCV_TOOLCHAIN_BIN")
    if override:
        return Path(override)
    for candidate in RISCV_TOOLCHAIN_CANDIDATES:
        path = Path(candidate)
        if (path / "riscv32-unknown-elf-gcc.exe").exists():
            return path
        if (path / "riscv32-unknown-elf-gcc").exists():
            return path
    found = shutil.which("riscv32-unknown-elf-gcc")
    if found:
        return Path(found).parent
    raise SystemExit("missing riscv32-unknown-elf toolchain; set RISCV_TOOLCHAIN_BIN")


def riscv_tools():
    bin_dir = riscv_bin_dir()
    suffix = ".exe" if os.name == "nt" else ""
    return {
        "gcc": str(bin_dir / ("riscv32-unknown-elf-gcc" + suffix)),
        "ld": str(bin_dir / ("riscv32-unknown-elf-ld" + suffix)),
        "objcopy": str(bin_dir / ("riscv32-unknown-elf-objcopy" + suffix)),
        "size": str(bin_dir / ("riscv32-unknown-elf-size" + suffix)),
        "objdump": str(bin_dir / ("riscv32-unknown-elf-objdump" + suffix)),
    }


def riscv_env(tools):
    env = dict(os.environ)
    bin_dir = str(Path(tools["gcc"]).parent)
    env["PATH"] = bin_dir + os.pathsep + env.get("PATH", "")
    return env


def run(command, env=None):
    printable = " ".join(str(part) for part in command)
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env)
    if result.returncode != 0:
        print(printable)
        print(result.stdout)
        raise SystemExit("command failed")
    if result.stdout.strip():
        print(result.stdout.strip())


def clean():
    if BUILD.exists():
        for entry in BUILD.iterdir():
            if entry.name == "riscv":
                continue
            if entry.is_dir():
                shutil.rmtree(entry)
            else:
                entry.unlink()
    BUILD.mkdir(parents=True, exist_ok=True)


def clean_riscv():
    riscv_build = BUILD / "riscv"
    if riscv_build.exists():
        shutil.rmtree(riscv_build)
    riscv_build.mkdir(parents=True, exist_ok=True)


def x86_sources(tree):
    sources = []
    for base in ("kernel", "user"):
        base_dir = ROOT / base
        if not base_dir.exists():
            continue
        for source in sorted(base_dir.rglob("*")):
            if not source.is_file():
                continue
            if source.suffix not in (".c", ".asm", ".S"):
                continue
            relative = source.relative_to(ROOT)
            if base == "kernel":
                parts = relative.parts
                if len(parts) > 2 and parts[1] == "arch":
                    arch_dir = Path(parts[0]) / Path(parts[1]) / Path(parts[2])
                    if arch_dir in RISCV_ARCH_DIRS:
                        continue
                if any(parent.name == "riscv32" for parent in source.parents):
                    continue
            sources.append(source)
    return sources


def riscv_sources(tree):
    sources = []
    for source in sorted((ROOT / "kernel").rglob("*")):
        if not source.is_file():
            continue
        if source.suffix not in (".c", ".S", ".s"):
            continue
        if "riscv32" not in [parent.name for parent in source.parents]:
            continue
        sources.append(source)
    common = [ROOT / "kernel" / "hal" / "hal.c"]
    common += [ROOT / "kernel" / "lib" / "printf.c"]
    common += [ROOT / "kernel" / "lib" / "string.c"]
    for path in common:
        if path.exists():
            sources.append(path)
    return sources


def riscv_libgcc(tools, env):
    result = subprocess.run([tools["gcc"], "-march=" + RISCV_MARCH, "-mabi=" + RISCV_MABI,
                             "-print-libgcc-file-name"],
                            cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env)
    if result.returncode != 0:
        raise SystemExit("cannot resolve riscv libgcc")
    return Path(result.stdout.strip())


def compile_riscv():
    tools = riscv_tools()
    env = riscv_env(tools)
    clean_riscv()
    obj_root = BUILD / "riscv" / "obj"
    obj_root.mkdir(parents=True, exist_ok=True)
    include_args = [arg for arg in KERNEL_INCLUDE_DIRS]
    include_args.append("-Ikernel/arch/riscv32")
    objects = []
    for source in riscv_sources(ROOT):
        relative = source.relative_to(ROOT)
        obj = obj_root / (str(relative).replace(os.sep, "_") + ".o")
        command = [tools["gcc"]] + RISCV_CFLAGS + include_args + ["-c", str(source), "-o", str(obj)]
        run(command, env=env)
        objects.append(obj)
    elf = BUILD / "riscv" / "zos-riscv.elf"
    flat = BUILD / "riscv" / "zos-riscv.bin"
    libgcc = riscv_libgcc(tools, env)
    link_command = [tools["ld"], "-m", "elf32lriscv", "--no-relax",
                    "-T", str(ROOT / "kernel" / "arch" / "riscv32" / "link.ld"),
                    "-o", str(elf)]
    link_command += [str(obj) for obj in objects]
    link_command += ["-L", str(libgcc.parent), "--start-group", "-lgcc", "--end-group"]
    result = subprocess.run(link_command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, env=env)
    if result.returncode != 0:
        print(" ".join(link_command))
        print(result.stdout)
        raise SystemExit("riscv link failed")
    run([tools["objcopy"], "-O", "binary", str(elf), str(flat)], env=env)
    size_output = subprocess.run([tools["size"], str(elf)], cwd=ROOT, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 env=env)
    if size_output.returncode != 0:
        raise SystemExit("riscv size failed")
    print(size_output.stdout.strip())
    manifest = {
        "arch": "riscv32",
        "march": RISCV_MARCH,
        "mabi": RISCV_MABI,
        "elf": str(elf.relative_to(ROOT)),
        "binary": str(flat.relative_to(ROOT)),
        "binary_size": flat.stat().st_size,
        "binary_sha256": hashlib.sha256(flat.read_bytes()).hexdigest(),
        "load_address": "0x00000000",
    }
    (BUILD / "riscv" / "riscv-manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps(manifest, indent=2))
    return flat


def compile_kernel(tag, defines, extra_objects):
    nasm, clang, lld, objcopy = x86_tools()
    objects = list(extra_objects)
    includes = [arg for arg in KERNEL_INCLUDE_DIRS]
    kernel_dir = ROOT / "kernel"
    for source in sorted(kernel_dir.rglob("*")):
        if not source.is_file():
            continue
        if source.suffix not in (".c", ".asm"):
            continue
        relative = source.relative_to(ROOT)
        parts = relative.parts
        if len(parts) > 3 and parts[1] == "arch" and parts[2] == "riscv32":
            continue
        obj = BUILD / tag / "obj" / relative.with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        if source.suffix == ".c":
            command = [clang] + X86_CFLAGS + includes + defines + ["-c", str(source), "-o", str(obj)]
        else:
            command = [nasm, "-f", "elf64", "-I", str(ROOT / "build") + os.sep, str(source), "-o", str(obj)]
        run(command)
        objects.append(obj)
    elf = BUILD / "z-os.elf" if tag == "release" else BUILD / ("z-os-" + tag + ".elf")
    flat = BUILD / "z-os.bin" if tag == "release" else BUILD / ("z-os-" + tag + ".bin")
    command = [lld, "-m", "elf_x86_64", "-z", "noexecstack", "-T", str(ROOT / "kernel" / "link.ld"), "-o", str(elf)]
    command += [str(obj) for obj in objects]
    run(command)
    run([objcopy, "-O", "binary", str(elf), str(flat)])
    if flat.stat().st_size > KERNEL_LIMIT:
        raise SystemExit("kernel image exceeds loader limit")
    return flat


def build_user():
    nasm, clang, lld, _ = x86_tools()
    linker = ROOT / "user" / "user.ld"
    elf_paths = []
    user_dir = ROOT / "user"
    if not user_dir.exists():
        return None, elf_paths
    for source in sorted(user_dir.glob("*.c")):
        object_path = BUILD / ("user_" + source.stem + ".o")
        elf_path = BUILD / ("user_" + source.stem + ".elf")
        run([clang] + USER_CFLAGS + ["-I", str(user_dir), "-c", str(source), "-o", str(object_path)])
        run([lld, "-m", "elf_x86_64", "-z", "noexecstack", "-T", str(linker),
             "-o", str(elf_path), str(object_path)])
        image = elf_path.read_bytes()
        if len(image) < 64 or image[0:4] != b"\x7fELF" or image[4] != 2 or image[5] != 1:
            raise SystemExit("user ELF header is invalid: " + source.stem)
        if int.from_bytes(image[18:20], "little") != 62:
            raise SystemExit("user ELF is not x86_64: " + source.stem)
        elf_paths.append(elf_path)
    embed = user_dir / "program.asm"
    if not embed.exists():
        return None, elf_paths
    embed_path = BUILD / "user_program.o"
    run([nasm, "-f", "elf64", "-I", str(BUILD) + os.sep, str(embed), "-o", str(embed_path)])
    return embed_path, elf_paths


def build_boot():
    nasm, _, _, _ = x86_tools()
    stage1 = BUILD / "stage1.bin"
    stage2 = BUILD / "stage2.bin"
    run([nasm, "-f", "bin", str(ROOT / "boot" / "stage1.asm"), "-o", str(stage1)])
    run([nasm, "-f", "bin", str(ROOT / "boot" / "stage2.asm"), "-o", str(stage2)])
    if stage1.stat().st_size != 512:
        raise SystemExit("stage1 size mismatch")
    if stage2.stat().st_size != STAGE2_SECTORS * 512:
        raise SystemExit("stage2 size mismatch")
    return stage1, stage2


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


def build_x86():
    clean()
    stage1, stage2 = build_boot()
    stage1_bytes = stage1.read_bytes()
    stage2_bytes = stage2.read_bytes()
    user_obj, user_elfs = build_user()
    extra = [user_obj] if user_obj is not None else []
    release = compile_kernel("release", [], extra)
    test = compile_kernel("selftest", ["-DZ_SELFTEST"], extra)
    image = make_image(release, stage1, stage2, "z-os.img")
    test_image = make_image(test, stage1, stage2, "z-os-test.img")
    manifest = {
        "arch": "x86_64",
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
        "user_object": str(user_obj.relative_to(ROOT)) if user_obj is not None else "",
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
    parser.add_argument("--arch", default=os.environ.get("ARCH", "x86_64"),
                        choices=["x86_64", "riscv", "riscv32"])
    parsed = parser.parse_args()
    arch = "riscv32" if parsed.arch.startswith("riscv") else "x86_64"
    if parsed.action == "clean":
        clean()
        print("build directory cleaned")
        return
    if arch == "riscv32":
        compile_riscv()
    else:
        build_x86()


if __name__ == "__main__":
    main()
