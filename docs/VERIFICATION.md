# Verification

## Environment

| Item | Value |
| --- | --- |
| Date | 2026-11-25 |
| Version | 0.4.0 Scheduler |
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
- `build/user_init.elf`, `build/user_multi.elf`, `build/user_forktest.elf`,
  `build/user_guitest.elf`
- `build/manifest.json`

The loader enforces a 320 KiB kernel slot. The 0.4.0 kernel occupies
approximately 117 KiB of the slot, leaving room for additional subsystems. The
image is zero-padded to 1,474,560 bytes. SHA-256 hashes for both images and all
four user ELF programs are written to `build/manifest.json` after every build.

## Self-test result

The self-test image executes 107 checks and exits QEMU through the ISA debug
exit device. A successful run returns host exit status 33, which is
`(0x10 << 1) | 1`. A failure returns 35.

Verified output ends with:

```text
ZTEST SUMMARY passed=107 failed=0
ZTEST RESULT PASS
```

The original 73 checks remain in place. This milestone adds 34 checks covering
preemptive time slicing, fork continuation, blocking wait, framebuffer VRAM
access, graphics primitives, window composition, and user graphics output.

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
| Address spaces | Distinct PML4 roots, distinct user frames, cross-space invisibility, high-half identity, high-alias translation, private kernel stacks |
| Processes | Creation, PID/parent links, image replacement, exit status, reaping, table drain |
| Preemption | Quantum configuration, two-process completion, marker totals, interleaving, forced switch counters |
| Fork and wait | Child resumes at parent code point, private memory copy, parent memory integrity, blocking order, exit status |
| Framebuffer | PCI VGA probe, BAR address, VRAM read/write/readback, mode set, mode dimensions, surface exposure |
| Graphics | Rectangle fill counts, clipping, checksums, pixel readback |
| Window manager | Window creation, second window, composition, first and later window drawing, z order, destruction, composition removal, table drain |
| User graphics | Ring-3 program exit status, real framebuffer pixels after drawing |
| Display registry | Framebuffer registration, lookup, primary selection, count, unregistration |
| User mode | Ring-3 C program under the process scheduler, syscall write, syscall exit, reaping |

The scheduler and graphics tests produce the following serial output:

```text
multi: process started
multi: process started
multi: process finished
multi: process finished
forktest: program started
forktest: fork child resumed at parent code point
forktest: blocking wait returned child status
guitest: graphics program started
guitest: framebuffer info received
guitest: rectangles drawn to framebuffer
guitest: pixel readback verified
```

The preemption test additionally verifies that the marker trace records both
process identifiers, that the trace interleaves them, that the timer
preemption counter increases, and that both processes record forced context
switches. The fork test verifies that parent and child share a code location
but not memory, and that the wait return mark appears after the child exit mark.

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

![Z-OS graphics stack](gui.png)

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
- `usertest` runs the ring-3 program as a scheduled process and reaps it.
- `schedtest` runs two processes under time slicing and reports quantum,
  completions, preemption count, and both exit statuses.
- `forktest` runs the fork and blocking wait program and reports the parent
  exit status.
- `gfx test` enters graphics mode, composes two windows, verifies framebuffer
  pixels, and restores text mode.
- `cat /README` reads the boot filesystem image.
- `shutdown` issues the ACPI PM1a power request and makes QEMU exit with
  status 0.

The shell observes the scheduler and graphics lines:

```text
schedtest: quantum=2 ticks completed=2 preemptions=...
gfx: framebuffer 800x600x32 at 0xfd000000, surface vram
gfx: window manager composited 2 windows
gfx: composition verified
```

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
- SMP or concurrent execution on more than one CPU. The scheduler is
  single-core round robin, one batch at a time.
- POSIX signals, credentials, sessions, or a file-descriptor table.
- Copy-on-write fork, demand paging, or a user-space heap.
- Exec path lookup, argv/envp, auxiliary vectors, or dynamic linking.
- Text rendering, mouse input, damage tracking, or a user-space compositor.
- The kernel executing from the high-half direct map. The high-half window is
  mapped and tested, while kernel text still executes through the low identity
  window.
- Long-running stability beyond the boot tests.
- Disk persistence, networking, USB, audio, 3D acceleration, or GPU drivers.
- Windows, Linux, or macOS binary compatibility.
- Adversarial security resistance.
- Memory hotplug, CPU hotplug, suspend, or resume.
- Real NX, SMEP, SMAP, KASLR, or ASLR behavior.

Those items are tracked in [ROADMAP.md](ROADMAP.md) and are not implied by a
passing 0.4.0 test run.
