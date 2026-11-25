# Z-OS

Z-OS is a from-scratch, 64-bit operating system for the x86_64 PC platform.
It boots on BIOS through a two-stage loader into a freestanding C kernel and
provides a small but real protected-mode environment: preemptive kernel
threads, per-process address spaces, a shared kernel high-half mapping, a
kernel heap, a virtual filesystem backed by ramfs, a ring-3 user program with
syscalls, an interactive shell, a display-device abstraction, and automated
boot-time self-tests.

This repository is an engineering foundation, not a replacement for Windows,
Linux, or macOS. It does not claim feature parity with those systems. The
implemented surface, known limitations, and a staged path toward a
general-purpose OS are documented in [docs/ROADMAP.md](docs/ROADMAP.md).

## Current status

Version 0.3.0, codename Process. Development quality. Not production ready.

| Area | State |
| --- | --- |
| BIOS boot | Two-stage loader, A20, E820, flat kernel load, long mode, identity map |
| CPU tables | GDT, TSS, IDT, exception diagnostics, double-fault IST stack |
| Interrupts | PIC remap, PIT at 100 Hz, PS/2 keyboard, serial RX, RTC |
| Memory | E820 frame bitmap, 4-level paging, per-process PML4, shared high-half direct map |
| Processes | PID table, parent links, ready queue, isolated ELF64 address spaces |
| Scheduling | Preemptive kernel threads; synchronous user-process execution |
| Filesystem | In-memory VFS and ramfs, 128 nodes, 1 MiB maximum file size |
| User mode | Ring-3 static ELF64 C process, TSS kernel stack, `int 0x80` syscalls |
| GUI foundation | Framebuffer structures and display registry, no compositor yet |
| Shell | Directory and file commands, system information, ELF process launcher |
| Verification | 73 in-kernel tests, boot smoke test, interactive shell test |

Implemented and verified:

- BIOS floppy boot with a 32-sector second stage and a fixed 320 KiB kernel slot.
- Long-mode kernel at 1 MiB with its own GDT, TSS, IDT, and page tables.
- Shared kernel high-half direct map at 0xFFFFFFFF80000000 for all processes.
- VGA text console and 115200-baud COM1 serial mirror.
- PIT timer interrupts at 100 Hz and a preemptive round-robin kernel scheduler.
- Physical memory manager using the E820 map, capped at the identity-mapped 1 GiB.
- Four-level virtual memory manager with 2 MiB page splitting and address-space objects.
- Per-process PML4 roots, user-page isolation, address-space clone, switch, and destroy.
- Boundary-tag kernel heap backed by scattered physical frames and shared supervisor mappings.
- PS/2 scan-code set 1 keyboard with shift, caps lock, control, and arrows.
- RTC date and time through CMOS.
- VFS with files, directories, rename, removal, path resolution, and devices.
- Ramfs with `/README`, `/etc`, `/home`, `/tmp`, `/dev/null`, `/dev/console`, and `/dev/random`.
- Static ELF64 loader with header, program-header, bounds, overlap, and W^X validation.
- ELF64 loading into independent process address spaces.
- Process table with PIDs, parent relationships, ready queue, exit status, and reaping.
- Process-manager fork, image replacement, exit, and wait paths.
- Ring-3 static C program, `int 0x80`, and syscalls `write`, `exit`, `getpid`, `uptime`, `yield`, `fork`, `exec`, and `wait`.
- Framebuffer and display-device interface plus a tested display registry.
- Shell commands for filesystem work, diagnostics, scheduling, ELF process launch, and power.
- ACPI PM1a shutdown path for QEMU and compatible chipsets.
- QEMU self-test image with machine-checkable pass or fail exit status.

![Z-OS VGA console](docs/console.png)

Not implemented yet:

- SMP, APIC, ACPI table parsing, and per-CPU scheduling.
- Preemptive user-process scheduling, kernel-thread-backed process contexts, and blocking wait queues.
- True fork continuation at the same user instruction pointer; the current fork clones a ready process image.
- Blocking `wait` semantics and POSIX process credentials.
- Dynamic ELF loading, PIE relocation, shared libraries, and a user-space C library.
- Persistent disks, block cache, journaling filesystem, permissions, and mount points.
- Networking, sockets, USB, audio, GPU acceleration, and a graphical compositor.
- Windows PE and Win32 compatibility, Linux ABI compatibility, or Mach-O support.
- NX, SMEP, SMAP, KASLR, ASLR, W^X enforcement, capabilities, and secure boot.
- Power management beyond a QEMU-compatible S5 shutdown request.

## Build

Requirements:

- Python 3.10 or newer
- NASM 2.15 or newer
- Clang with the `x86_64-none-elf` target
- LLD
- GNU objcopy
- QEMU for running and testing

Ubuntu and Debian:

```bash
sudo apt update
sudo apt install nasm clang lld qemu-system-x86 python3
```

Windows: the build script auto-detects an installed NASM, the Clang and LLD
copies shipped with Lean, GNU objcopy from MinGW, and QEMU in `D:\QEMU`.
Set `PATH` normally if the tools are installed elsewhere.

Build both the normal and self-test images:

```bash
python tools/build.py
```

Artifacts:

- `build/z-os.img`: normal boot image
- `build/z-os-test.img`: kernel self-test image
- `build/z-os.elf`: kernel ELF with debug symbols
- `build/user_init.elf`: static ELF64 user program
- `build/manifest.json`: sizes and SHA-256 hashes

The image is a 1.44 MiB floppy image. The loader is tied to the standard
1.44 MiB geometry: 80 cylinders, 2 heads, 18 sectors per track.

## Run

QEMU with the VGA console and keyboard:

```bash
qemu-system-x86_64 -m 256M -drive format=raw,file=build/z-os.img,if=floppy -boot a
```

Windows example:

```bat
D:\QEMU\qemu-system-x86_64.exe -m 256M -drive format=raw,file=build/z-os.img,if=floppy -boot a
```

Headless serial console:

```bash
qemu-system-x86_64 -m 256M -drive format=raw,file=build/z-os.img,if=floppy -boot a -display none -serial stdio
```

The VGA and serial consoles receive the same output. Input is accepted from
both the PS/2 keyboard and COM1.

## Test

Run the complete automated suite:

```bash
python tools/verify.py all
```

Individual checks:

```bash
python tools/verify.py test
python tools/verify.py smoke
python tools/verify.py keyboard
python tools/verify.py shell
```

The self-test image runs 73 in-kernel checks covering physical memory,
paging, the heap, the VFS, formatting, the RTC, timer wakeups, kernel threads,
ELF64 validation, per-process address spaces, high-half mapping, process
lifecycle, display registration, and the ring-3 syscall round trip. It then
exits QEMU through the ISA debug exit device, so the harness can validate a
machine-readable result rather than a screenshot.

The keyboard check injects `help` through the QEMU monitor, exercising the
PS/2 controller, keyboard interrupt, scan-code translation, input queue, and
shell line editor. The interactive test boots the normal image, connects to a
TCP serial backend, runs shell commands, and verifies file creation, reads,
appends, renames, copies, metadata, hexdumps, memory, scheduling, ELF64
execution, cleanup, and ACPI shutdown.

Verified environment: QEMU 7.0.0 on Windows, 256 MiB RAM, default `pc`
machine and SeaBIOS. See [docs/VERIFICATION.md](docs/VERIFICATION.md).

## ELF64 user program

`user/init.c` is a freestanding C program compiled with Clang and linked by
LLD at 0x40000000. It uses only the Z-OS syscall ABI and has no hosted C
library. The linker produces two PT_LOAD segments: read-execute text and
read-write data with BSS.

The kernel embeds the complete ELF64 executable in its rodata. On `usertest`,
the kernel creates a process, creates a private PML4 address space, validates
the ELF image, copies every PT_LOAD segment into that address space, maps a
private stack, and enters Ring 3 under the process CR3. The high-half kernel
mapping and the supervisor-only low identity mapping are shared by all address
spaces.

The current loader accepts only static `ET_EXEC` images with page-aligned,
non-overlapping PT_LOAD segments inside 0x40000000 to 0x4000FFFF. It does not
support PIE relocation, dynamic linking, argv, or environment strings.

```text
User: entering ring 3 in process address space 0x93e000
Hello from Z-OS ring 3
User syscall return path verified
C ELF64 user program checksum verified
ELF64 BSS zero check verified
user program exited with status 0
```

## Process and address spaces

`kernel/task/process.c` maintains up to 32 process control blocks. Each PCB
has a PID, parent PID, state, private address space, ELF entry point, user
stack, exit status, and ready-queue link.

`kernel/mm/vmm.c` implements address-space creation, switching, cloning,
mapping, and destruction. Each process receives:

- A private PML4 root and PDPT.
- A private user page directory for the 1 to 2 GiB window.
- Shared supervisor mappings for the low kernel identity window.
- Shared supervisor mappings for the kernel heap.
- The shared high-half direct map at 0xFFFFFFFF80000000.

The kernel currently executes through the low identity window. The high-half
window is a verified shared direct map of the first 1 GiB, not a relocated
kernel text execution window yet.

Process runs are synchronous in 0.3.0: the kernel switches CR3, enters Ring 3,
and restores the kernel CR3 after exit. The ready queue, PID allocation,
fork clone, image replacement, exit status, and wait reaping paths are tested.
Concurrent user-process scheduling, true fork continuation at the parent's
instruction pointer, and blocking wait queues remain future work.

## Display foundation

`kernel/include/kernel.h` defines framebuffer metadata, display operations,
and a display device object. `kernel/drivers/display.c` provides a locked
registry for display devices. No framebuffer hardware driver, compositor, or
window manager is implemented yet. The registry is tested with a synthetic
framebuffer device.

## Shell

```text
z-os:/# help
z-os:/# ls /
z-os:/# cat /README
z-os:/# write /tmp/hello hello Z-OS
z-os:/# cat /tmp/hello
z-os:/# mem
z-os:/# ps
z-os:/# usertest
z-os:/# shutdown
```

The shell supports `help`, `about`, `version`, `uname`, `clear`, `echo`,
`pwd`, `cd`, `ls`, `cat`, `mkdir`, `rmdir`, `touch`, `write`, `append`,
`rm`, `mv`, `cp`, `stat`, `hexdump`, `df`, `mem`, `free`, `uptime`, `date`,
`ps`, `threads`, `sleep`, `history`, `rand`, `usertest`, `reboot`, and
`shutdown`.

`usertest` creates a fresh process with a private address space, loads the
embedded ELF64 program, runs it under its own CR3, reaps it, and reports its
exit status.

Line editing supports backspace, Ctrl+C, and Ctrl+L. Arrow keys are decoded
for the shell but history navigation is not implemented yet.

## Repository layout

```text
boot/          BIOS stage 1, stage 2, and loader constants
kernel/        freestanding C and NASM kernel
  arch/        entry, GDT, IDT stubs, context switch, user transition
  cpu/         GDT, IDT, PIC, ELF64 loader, user transition, power
  drivers/     console, serial, keyboard, input queue, PIT, RTC, display registry
  fs/          VFS and ramfs
  include/     shared kernel interfaces and ELF definitions
  lib/         string and formatted output
  mm/          physical memory, paging, address spaces, heap
  sched/       preemptive kernel thread scheduler
  shell/       interactive shell
  subsys/      syscall dispatch
  task/        process control blocks and process lifecycle
user/          static ELF64 C user program and linker script
tools/         build and verification automation
docs/          architecture, roadmap, verification
```

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)
- [docs/ROADMAP.md](docs/ROADMAP.md)
- [docs/VERIFICATION.md](docs/VERIFICATION.md)
- [docs/SECURITY.md](docs/SECURITY.md)

## License

MIT. See [LICENSE](LICENSE).
