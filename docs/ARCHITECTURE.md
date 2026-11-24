# Z-OS Architecture

This document describes the Z-OS 0.1.0 implementation as it exists in this
repository. It distinguishes implemented mechanisms from planned work.

## Design goals

- Boot on a conventional BIOS PC with ordinary emulation and no external
  bootloader.
- Keep the kernel freestanding and auditable: no hosted C library, no runtime,
  no hidden firmware services after long mode is entered.
- Make every major subsystem observable from the shell and testable from an
  automated QEMU run.
- Prefer explicit, simple mechanisms over claims of completeness.
- Leave clean extension points for processes, storage, networking, SMP, and
  compatibility layers.

Non-goals for 0.1.0:

- Binary compatibility with Windows, Linux, or macOS.
- Production security, hardware certification, or data durability.
- Support for arbitrary PC hardware beyond QEMU and similar BIOS machines.

## Platform model

| Item | Value |
| --- | --- |
| Architecture | x86_64, long mode |
| Firmware | Legacy BIOS, SeaBIOS in QEMU |
| Boot medium | 1.44 MiB floppy image, 80/2/18 geometry |
| Kernel location | Physical and virtual 0x00100000, identity mapped |
| Kernel format | Flat binary linked at 0x00100000 |
| Kernel slot | 640 sectors, 320 KiB maximum |
| Timer | 8254 PIT, 100 Hz, interrupt vector 32 |
| Keyboard | PS/2 scan-code set 1, interrupt vector 33 |
| Serial | 16550-compatible COM1 at 0x3F8, interrupt vector 36 |
| Console | VGA text mode at 0xB8000 plus serial mirror |
| Physical memory cap | 1 GiB, the region covered by boot identity mappings |

The loader and kernel currently assume one processor. Interrupt controllers
are the legacy 8259 PIC pair and the legacy PIT. ACPI tables are not parsed.
A QEMU-compatible ACPI PM1a power request is issued for shutdown.

## Boot sequence

### Stage 1

`boot/stage1.asm` is a 512-byte master boot record.

1. It establishes real-mode segment registers and a stack at 0x7C00.
2. It reads 17 sectors from cylinder 0, head 0, sector 2 into 0x0800:0000.
3. It reads 15 sectors from cylinder 0, head 1, sector 1 into 0x0800:2200.
4. It jumps to 0x8000 with the BIOS boot drive in DL.

The two reads avoid crossing a track boundary during a single BIOS call.
Stage 2 occupies exactly 32 sectors, from LBA 1 through LBA 32.

### Stage 2

`boot/stage2.asm` is a 16 KiB real-mode loader.

1. It enables the A20 gate through system control port 0x92.
2. It calls BIOS `INT 15h, EAX=E820` and writes up to 32 descriptors into the
   BootInfo block at physical 0x5000.
3. It reads 640 kernel sectors one at a time from LBA 33 onward into a 320 KiB
   staging area at 0x10000. CHS geometry is computed for every sector.
4. It installs a temporary GDT and enters 32-bit protected mode.
5. It copies the staging area to 0x100000.
6. It builds identity page tables for the first 1 GiB with 2 MiB pages:
   PML4 at 0x1000, PDPT at 0x2000, PD at 0x3000.
7. It enables PAE, long mode, and paging, then far jumps to a 64-bit code
   descriptor.
8. It places the BootInfo pointer in RDI and jumps to 0x100000.

The kernel accepts the fixed kernel slot. The build fails if the flat kernel
exceeds 320 KiB.

### Kernel entry

`kernel/arch/entry.asm` performs the work that must happen before any C code:

1. Clear interrupts and the direction flag.
2. Save the BootInfo pointer in R15.
3. Clear CR0.EM and CR0.TS, set CR0.MP, and enable CR4.OSFXSR and
   CR4.OSXMMEXCPT.
4. Zero the entire kernel BSS using the linker-provided bounds.
5. Switch to the 64 KiB boot stack in BSS.
6. Call `kmain`.

Kernel initialization order:

1. Serial, VGA console, and BootInfo validation.
2. GDT, TSS, IDT, PIC remap, and mask-all.
3. Physical memory manager from E820.
4. Virtual memory manager over the boot page tables.
5. Kernel heap and its virtual mapping.
6. VFS and ramfs population.
7. Scheduler initialization.
8. PIT, keyboard, serial RX, and RTC.
9. Enable interrupts.
10. Create the shell thread, or the self-test thread in the test image.
11. Enter the idle loop.

## Physical memory map

| Range | Use |
| --- | --- |
| 0x00000000 to 0x00000FFF | Real-mode IVT, BIOS data, reserved |
| 0x00001000 to 0x00003FFF | Boot PML4, PDPT, and PD |
| 0x00005000 | BootInfo and E820 descriptors |
| 0x00008000 to 0x0000BFFF | Stage 2 |
| 0x00010000 to 0x0005FFFF | Kernel staging buffer during boot |
| 0x0007C000 to 0x0007FFFF | Stage 1 stack and boot sector |
| 0x000A0000 to 0x000BFFFF | VGA text memory |
| 0x00100000 to kernel_end | Kernel text, rodata, data, BSS |
| Above kernel_end | Allocatable frames below 1 GiB |

The physical memory manager marks all frames used, frees usable E820 ranges
below 1 GiB, then reserves the low 1 MiB and the loaded kernel image including
BSS. It never allocates a frame above the boot identity map.

## Virtual address map

| Range | Use |
| --- | --- |
| 0x0000000000000000 to 0x000000003FFFFFFF | Identity map of physical memory |
| 0x0000000040000000 | Ring-3 code page |
| 0x0000000040001000 | Ring-3 stack page |
| 0x0000000050000000 | Self-test scratch mapping |
| 0x0000000060000000 to 0x00000000607FFFFF | 8 MiB kernel heap |
| 0x0000000070000000 | Self-test scratch mapping |

The initial page tables map 1 GiB with 2 MiB pages. `vmm_map_page` splits a
2 MiB mapping into a 4 KiB page table when a finer mapping is requested. New
page tables are allocated from the physical memory manager and are reachable
through the identity map.

The kernel heap is placed outside the 1 GiB identity window so that mapping
heap pages never aliases a physical frame that the allocator might need to
clear through its identity address.

## Physical memory manager

- Bitmap covers 262,144 frames, or 1 GiB.
- One bit per 4 KiB frame, 1 means used.
- Allocation uses a moving search hint and 64-bit word scanning.
- Newly allocated frames are zeroed through the identity map.
- Contiguous allocation exists for callers that need physically adjacent frames.
- Statistics report total usable, free, and used frames.
- The lock is a spinlock that disables interrupts.

## Virtual memory manager

- Four-level x86_64 page tables.
- `vmm_map_page` and `vmm_unmap_page` operate on 4 KiB pages.
- Intermediate tables are allocated and zeroed on demand.
- 2 MiB PD entries are split into page tables while preserving base flags.
- Kernel mappings are supervisor-only. Ring-3 mappings set the user bit at
  every level.
- TLB entries are invalidated with `INVLPG`.
- The current address space is the boot address space; user mode shares it.

`vmm_get_physical` supports 1 GiB, 2 MiB, and 4 KiB leaf entries.
`vmm_user_range_valid` walks the page table for every page in a range and
requires the user bit at all levels. It is used by syscalls before kernel
code touches a user pointer.

## Kernel heap

- Virtual region: 8 MiB at 0x60000000.
- Backing: individual 4 KiB frames, not necessarily contiguous.
- Allocator: doubly linked boundary-tag blocks with a 48-byte header.
- First-fit allocation, 16-byte alignment, split on allocation, coalesce on free.
- Supports `kmalloc`, `kzalloc`, `krealloc`, and `kfree`.
- Guard patterns for invalid free and double free cause a kernel panic.
- The heap does not return pages to the physical allocator.

## Interrupts and exceptions

- 256 IDT gates, all interrupt gates.
- Vectors 0 through 31 are CPU exceptions. Vectors with hardware error codes
  and vectors without them are normalized by generated assembly stubs.
- Vector 8 uses the TSS IST1 stack, an 8 KiB emergency stack.
- Vectors 32 through 47 are remapped PIC IRQs.
- Vector 0x80 is the syscall gate and is the only gate with DPL 3.
- The common stub saves 15 general registers, calls C dispatch, and restores
  the frame.
- Fatal kernel exceptions print the vector, error code, RIP, CS, RFLAGS, key
  registers, and CR2 for page faults, then halt.
- A ring-3 exception is reported and converted into a user-mode exit status.
- The PIC EOI is sent before the device handler so a scheduler switch inside
  a timer handler cannot hold off further interrupts.

`interrupt_dispatch` routes timer, keyboard, and serial interrupts. The timer
increments the tick counter, wakes sleeping threads, accounts CPU time, and
may context switch.

## Scheduler

- Up to 32 thread slots.
- Thread 0 is the boot/init thread with a 64 KiB static stack.
- Other threads receive 16 KiB stacks from the kernel heap.
- The scheduler is preemptive and round-robin with a 10 ms quantum.
- Thread states: unused, ready, running, sleeping, zombie.
- Context switch saves and restores the System V callee-saved registers:
  R15, R14, R13, R12, RBX, RBP.
- A newly created thread begins at an assembly trampoline that enables
  interrupts and invokes its C entry point.
- Exiting a thread marks it zombie and schedules another thread. The init
  thread reaps zombie stacks in its idle loop.
- `sched_sleep_ms` puts a thread to sleep until a tick deadline. The timer
  interrupt wakes it.
- A preemption-disable depth protects console output and the synchronous
  user-mode demo from scheduler switches. Interrupts remain enabled so the
  serial FIFO can drain.
- `tss_set_rsp0` is updated on each context switch for future ring-3 work.

There is no SMP scheduler, no priority scheduling, no CPU affinity, and no
work stealing. The design is deliberately uniprocessor.

## VFS and ramfs

- Static node pool of 128 nodes, protected by a global spinlock.
- Node types: regular file, directory, character device.
- Files grow through heap allocations in power-of-two capacities up to 1 MiB.
- Directories use child and sibling links.
- Path lookup handles absolute paths, relative paths, `.`, `..`, and repeated
  separators.
- Operations: create, mkdir, remove, rename, read, write, append, truncate,
  list, stat, and path reconstruction.
- Rename rejects moving a directory into its own subtree.
- Removal rejects non-empty directories.
- Device nodes: `/dev/null`, `/dev/console`, and `/dev/random`.
- Boot population: `/README`, `/etc/motd`, `/etc/version`, `/home/welcome`,
  `/tmp`, `/dev`, and the device nodes.

Limitations: no permissions, ownership, timestamps beyond creation ticks, hard
links, symbolic links, mount points, page cache, journaling, persistent
backing store, or fine-grained locking.

## User mode and syscalls

The embedded ring-3 program is at `user/hello.asm`. It is assembled as a flat
binary, embedded in the kernel's rodata through `user/program.asm`, and copied
to a user page at first use.

Transition sequence:

1. Allocate and map a user code page at 0x40000000 and a user stack page at
   0x40001000. Code is read-only for ring 3; the stack is writable.
2. Disable scheduler preemption for the duration of the synchronous demo.
3. Point TSS.RSP0 at a dedicated 16 KiB interrupt stack.
4. Load user data selectors and execute `IRETQ` with user CS, SS, RFLAGS.IF,
   user RIP, and user RSP.
5. `INT 0x80` enters the kernel through the DPL-3 gate. The CPU switches to
   TSS.RSP0 and the common stub builds a register frame.
6. The syscall dispatcher validates user pointers and performs the operation.
7. `exit` abandons the interrupt frame and restores the pre-demo kernel stack
   through an assembly return path.
8. Kernel preemption and the previous TSS.RSP0 are restored.

Syscall ABI:

| RAX | Name | Arguments | Result |
| --- | --- | --- | --- |
| 1 | write | RDI=fd, RSI=buffer, RDX=length | bytes written or -1 |
| 2 | exit | RDI=status | does not return |
| 3 | getpid | none | current thread PID |
| 4 | uptime | none | milliseconds since boot |
| 5 | yield | none | 0 |

Only file descriptors 1 and 2 are accepted by `write`, and both target the
kernel console. Buffers must be fully mapped with user permission and are
limited to 4096 bytes per call.

Important limitations:

- One synchronous user demo, not a general process model.
- The user program shares the kernel's address space. Kernel pages are
  supervisor-only, but there is no per-process isolation yet.
- No ELF loader, argv/envp, file descriptors, heap, signals, or process table.
- No NX, SMEP, SMAP, KASLR, or ASLR.
- User code and stack have fixed virtual addresses.

## Console and shell

The console driver writes VGA text memory and mirrors output to COM1. Newlines
become CRLF on serial. A hardware cursor is updated through VGA I/O ports.

Console output disables preemption but leaves interrupts enabled. This allows
the serial receive interrupt to drain the emulated UART FIFO while output is
in progress. It is safe on one CPU because no interrupt handler writes to the
console except fatal panic paths.

Input is a single-producer, single-consumer ring buffer. PS/2 keyboard and
serial receive interrupts push characters. The shell consumes them. Keyboard
translation supports US scan-code set 1, shift, caps lock, control, backspace,
tab, enter, and arrow keys.

The shell is a kernel thread. It uses the VFS directly and provides file,
memory, scheduling, RTC, user-mode, and power commands. It does not implement
pipes, redirection, background jobs, environment variables, or quoting beyond
double-quoted arguments.

## Power control

`system_reboot` resets through the keyboard controller and falls back to a
triple fault with a null IDT.

`system_shutdown` writes 0x2000 to the ACPI PM1a control block at port 0x604,
then tries the Bochs and VirtualBox ports. The QEMU path is verified to exit
the emulator. There is no ACPI table parser or general chipset discovery.

## Build pipeline

`tools/build.py` performs:

1. Assemble stage 1 and verify it is exactly 512 bytes.
2. Assemble stage 2 and verify it is exactly 16 KiB.
3. Assemble the ring-3 demo and its embedding object.
4. Compile every kernel C file twice: normal and with `-DZ_SELFTEST`.
5. Assemble every kernel NASM file.
6. Link each kernel ELF with LLD and the kernel linker script.
7. Convert each ELF to a flat binary and enforce the 320 KiB loader limit.
8. Place the kernel after stage 2 in a zeroed 1.44 MiB image.
9. Write sizes and SHA-256 hashes to `build/manifest.json`.

Compile flags include freestanding mode, no PIC/PIE, no stack protector, no
red zone, no builtins, no MMX/SSE, frame pointers, and optimization level 2.

## Reliability and current boundaries

- Malformed BootInfo leads to a panic, not recovery.
- Kernel page faults, invalid opcodes, double faults, and stack faults halt.
- User exceptions return an error to the shell, but do not kill a process
  because there is no process abstraction.
- The VFS and heap are protected by global locks. Contention and scalability
  are not modeled.
- There is no disk, so ramfs contents vanish on restart.
- QEMU is the reference platform. Real-hardware behavior depends on BIOS
  compatibility, floppy geometry support, and chipset-specific power control.
- The kernel assumes exactly one CPU and does not program the local APIC.

These boundaries are intentional for a 0.1.0 foundation and are tracked in
the roadmap.
