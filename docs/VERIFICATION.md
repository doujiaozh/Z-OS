# Verification

## Environment

| Item | Value |
| --- | --- |
| Date | 2026-11-25 |
| Version | 0.2.0 ELF |
| Host | Windows, x86_64 |
| Emulator | QEMU 7.0.0 |
| Machine | Default `pc`, SeaBIOS |
| Memory | 256 MiB |
| Build compiler | Clang 15.0.1, target x86_64-none-elf |
| Assembler | NASM 2.16.01 |
| Linker | LLD 15.0.1 |
| Binary conversion | GNU objcopy 2.30 |
| Automation | Python 3.11.16 |
| Guest console | VGA text and COM1 serial |

The image is booted as a 1.44 MiB floppy. No KVM acceleration is required for
the test suite.

## Commands

```bash
python tools/build.py
python tools/verify.py all
```

Individual stages:

```bash
python tools/verify.py test
python tools/verify.py smoke
python tools/verify.py keyboard
python tools/verify.py shell
```

## Build result

The build produces:

- `build/z-os.img`
- `build/z-os-test.img`
- `build/z-os.elf`
- `build/user_init.elf`
- `build/manifest.json`

The loader enforces a 320 KiB kernel slot. The 0.2.0 kernel occupies
approximately 59 KiB of the slot, leaving room for additional subsystems. The
image is zero-padded to 1,474,560 bytes. SHA-256 hashes for both images and the
user ELF are written to `build/manifest.json` after every build.

## Self-test result

The self-test image executes 49 checks and exits QEMU through the ISA debug
exit device. A successful run returns host exit status 33, which is
`(0x10 << 1) | 1`. A failure returns 35.

Verified output ends with:

```text
ZTEST SUMMARY passed=49 failed=0
ZTEST RESULT PASS
```

Test coverage by area:

| Area | Checks |
| --- | --- |
| Physical allocator | Alignment, distinct frames, free accounting, contiguous allocation |
| Kernel heap | Small and large allocations, isolation, realloc, coalescing, counters |
| Paging | Map, access, translate, unmap, unmapped state |
| VFS | Directory creation/removal, file create, write, read, append, stat, rename, remove |
| Devices | `/dev/null` write and read semantics |
| Formatting | Integers, hexadecimal, zero padding, truncation, trim |
| Time | RTC field sanity, timer progress across a sleep |
| Scheduler | Thread creation, preemptive completion, atomic counter correctness |
| ELF64 | Embedded image validation, entry and segments, bad magic, truncation, W^X rejection, address range rejection |
| User mode | Ring-3 C program, syscall write, syscall exit, return to kernel |

The user-mode portion produces the following serial output:

```text
User: loaded ELF64 entry 0x40000000 with 2 segments, stack 0x40010000
User: entering ring 3
Hello from Z-OS ring 3
User syscall return path verified
C ELF64 user program checksum verified
ELF64 BSS zero check verified
```

## Boot smoke test

The normal image is booted with no debug-exit device. The harness waits until
the serial log contains both the boot completion line and a shell prompt:

```text
Boot complete. Starting shell.
z-os:/#
```

The process is then terminated and the log is checked for panic or exception
markers. This verifies the normal image path independent of self-test mode.

![Z-OS console](console.png)

## PS/2 keyboard test

The harness boots the normal image with a serial log and a QEMU monitor
connection. After the shell prompt appears, it injects the keys `h`, `e`,
`l`, `p`, and Return through the monitor. The test passes when the serial log
contains the echoed `help` command and the complete command list. This covers
the PS/2 controller, IRQ1, scan-code translation, the input queue, shell line
editing, and command dispatch.

## Interactive shell test

The harness starts QEMU with a TCP serial backend, connects to it, and drives
the shell over COM1. The following commands and properties are checked:

- `help` lists the command surface.
- `mkdir /tmp/verify` creates a directory.
- `write` creates a file with text and a trailing newline.
- `cat` returns the exact text.
- `append` grows the file.
- `mv` renames the file.
- `ls` lists the renamed file.
- `cp` copies the file.
- `stat` reports the expected size of 24 bytes.
- `hexdump` shows the expected bytes and ASCII.
- `rm` and `rmdir` clean up the test tree.
- `mem` reports physical, heap, and ramfs figures.
- `df` reports filesystem figures.
- `uptime` advances.
- `ps` shows the init and shell threads.
- `rand` produces random bytes.
- `usertest` validates the embedded ELF, loads text and data pages, enters
  Ring 3, verifies a checksum and zeroed BSS, and exits with status 0.
- The shell reports `user program exited with status 0`.
- `cat /README` reads the boot filesystem image.
- `shutdown` issues the ACPI PM1a power request and makes QEMU exit with
  status 0.

The test fails on unexpected panic, exception, or unknown-command output.

## Console behavior notes

QEMU's `-serial stdio` backend can deliver a pasted command as a large burst.
The emulated 16550 receive FIFO holds 16 bytes, and data can be lost while the
guest is not draining it quickly enough. Human typing and the TCP-based test
harness are unaffected. The automated tests use TCP and pace command bytes to
avoid depending on host terminal buffering.

The console driver leaves interrupts enabled during output and disables only
scheduler preemption. This allows the serial interrupt to drain receive data
while text is being rendered.

## Shutdown check

`shutdown` writes an SLP_EN request to the ACPI PM1a control block at I/O port
0x604 and waits. On the verified QEMU machine, QEMU exits with status 0. The
kernel also contains fallback writes for Bochs and VirtualBox-style power
ports. There is no ACPI table parser yet, so this is not a general chipset
solution.

## Reproducing from a clean tree

```bash
rm -rf build
python tools/build.py
python tools/verify.py all
```

The build directory is created automatically. The verification command
rebuilds first unless `--skip-build` is supplied.

## What is not verified

- Physical hardware.
- SMP, APIC, or multi-core interrupt behavior.
- Per-process address spaces, multiple user processes, `fork`, `exec`, `wait`,
  signals, or a file-descriptor table.
- Static PIE relocation, dynamic linking, libc, argv, or environment strings.
- Long-running stability beyond the boot tests.
- Disk persistence, networking, USB, audio, graphics acceleration, or GPU.
- Windows, Linux, or macOS binary compatibility.
- Adversarial security resistance.
- Memory hotplug, CPU hotplug, suspend, or resume.
- Real NX, SMEP, SMAP, KASLR, or ASLR behavior.

Those items are tracked in [ROADMAP.md](ROADMAP.md) and are not implied by a
passing 0.2.0 test run.
