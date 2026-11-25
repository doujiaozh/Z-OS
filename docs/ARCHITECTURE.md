# Z-OS Architecture

This document describes the Z-OS 0.4.0 implementation as it exists in this
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

Non-goals for 0.4.0:

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

The kernel image executes through the low identity window at 1 MiB. Every
process address space also contains a shared supervisor-only direct map of
the first 1 GiB at 0xFFFFFFFF80000000. This establishes the kernel high-half
window without relocating kernel text execution yet.

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
| 0x0000000040000000 to 0x000000004000FFFF | Per-process ELF64 segment window |
| 0x0000000040010000 to 0x0000000040010FFF | Per-process Ring-3 stack |
| 0x0000000050000000 | Self-test scratch mapping |
| 0x0000000060000000 to 0x00000000607FFFFF | 8 MiB kernel heap |
| 0x0000000070000000 | Self-test scratch mapping |
| 0xFFFFFFFF40000000 to 0xFFFFFFFF7FFFFFFF | Shared supervisor device window (framebuffer) |
| 0xFFFFFFFF80000000 to 0xFFFFFFFFBFFFFFFF | Shared kernel high-half direct map |

The initial page tables map 1 GiB with 2 MiB pages. `vmm_map_page` splits a
2 MiB mapping into a 4 KiB page table when a finer mapping is requested. New
page tables are allocated from the physical memory manager and are reachable
through the identity map.

The kernel heap is placed outside the 1 GiB identity window so that mapping
heap pages never aliases a physical frame that the allocator might need to
clear through its identity address.

Each process PML4 has three top-level regions:

- PML4[0] points to a private PDPT. PDPT[0] shares the kernel low identity
  page directory for 0 to 1 GiB. PDPT[1] points to a private user page
  directory for 1 to 2 GiB.
- The private user page directory holds ELF pages and the user stack with the
  user bit set. Entries 256 through 259 are copied from the boot address space
  to share the supervisor-only 8 MiB kernel heap mapping.
- PML4[511] points to a shared high-half PDPT. PDPT entry 510 maps a shared
  1 GiB direct map of physical memory 0 to 1 GiB. PDPT entry 509 maps a shared
  supervisor-only device window used for framebuffer and other memory mapped
  device apertures. Both are identical in every address space.

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
- `vmm_map_page` and `vmm_unmap_page` operate on 4 KiB pages in the boot
  address space.
- `vmm_create_address_space` allocates a private PML4, PDPT, and user page
  directory, installs shared kernel mappings, and copies shared heap entries.
- `vmm_switch_address_space` writes CR3. `vmm_switch_kernel` restores the boot
  PML4.
- `vmm_map_user_page` and `vmm_unmap_user_page` map only the fixed user window
  in a specified process address space.
- `vmm_clone_address_space` deep-copies user pages into new physical frames
  and page tables while sharing supervisor kernel mappings.
- `vmm_destroy_address_space` frees user frames, user page tables, the user
  page directory, the PDPT, and the PML4. Shared kernel tables are not freed.
- Intermediate tables are allocated and zeroed on demand.
- 2 MiB PD entries are split into page tables while preserving base flags.
- Kernel mappings are supervisor-only. Ring-3 mappings set the user bit at
  every level.
- TLB entries are invalidated with `INVLPG` when a mapping is changed in the
  active address space.

`vmm_get_physical` supports 1 GiB, 2 MiB, and 4 KiB leaf entries.
`vmm_user_range_valid` and `vmm_user_range_valid_in` walk page tables for
every page in a range and require the user bit at all levels. Syscalls use
these checks before touching user pointers.

The high-half direct map is built during `vmm_init` from two PMM frames. It
maps physical memory 0 to 1 GiB with 2 MiB pages and is shared by every
process through PML4[511]. It is verified by translation and by reading the
kernel image through the high alias.

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
increments the tick counter, charges the active process quantum, wakes
sleeping kernel threads, accounts CPU time, and may context switch kernel
threads. After dispatch, the stub calls `sched_interrupt_return`, which may
switch to another user-process interrupt frame before `iretq`.

## Kernel thread scheduler

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
- The thread scheduler yields completely while a user-process batch is active.
- A preemption-disable depth protects console output.

There is no SMP scheduler, no priority scheduling, no CPU affinity, and no
work stealing. The design is deliberately uniprocessor.

## Preemptive user-process scheduler

`kernel/task/sched.c` runs Ring 3 processes with time-slice round-robin
preemption on top of the uniprocessor kernel.

Per-process state used by the scheduler:

| Field | Purpose |
| --- | --- |
| `kernel_stack_base` | 16 KiB heap stack, shared supervisor mapping |
| `kernel_stack_top` | Initial TSS.RSP0 for the process |
| `frame_rsp` | Saved pointer to the 176-byte interrupt frame |
| `quantum` | Ticks left in the current slice |
| `context_switches` | Forced switch counter |
| `cpu_ticks` | Timer ticks charged to the process |

Context switch mechanics:

1. An interrupt or `int 0x80` from Ring 3 switches the CPU stack to
   `TSS.RSP0`, which points at the current process kernel stack.
2. The stub pushes error code, vector, and the 15 general registers. The
   resulting frame layout matches `struct registers` and ends with the Ring 3
   `rip`, `cs`, `rflags`, `rsp`, and `ss` pushed by the CPU.
3. After C dispatch, the stub calls `sched_interrupt_return(frame_rsp)`.
4. If a batch is active, the frame came from Ring 3, and the process is
   blocked or its quantum expired, the scheduler saves `frame_rsp`, updates
   state and ready queue, writes CR3 for the next process, updates
   `TSS.RSP0`, and returns the next frame pointer.
5. The stub moves `rsp` to that pointer and falls through to `iretq`, which
   resumes the next process exactly where it was interrupted.

`kernel/arch/sched.asm` contains the three assembly entry points:
`sched_enter_user` (save the kernel batch context and resume a process frame),
`sched_resume_user_frame` (resume without saving, used by exit), and
`sched_abandon_to_dispatcher` (return to the batch driver kernel context).

Batch execution:

- A kernel thread calls `sched_run_batch(pids, count, &completed)`.
- The driver validates that every target process is ready, disables
  interrupts, and enters the first process.
- Whenever no ready process remains, control returns to the driver, which
  either enters the next ready process or ends the batch.
- The batch ends with `SCHED_ERROR_NONE` when no batch member is blocked.
  A blocked member with no runnable process is reported as
  `SCHED_ERROR_DEADLOCK` and remaining members are terminated.
- Kernel-thread scheduling is suspended for the duration of the batch, so user
  processes own the CPU.

## Processes and address spaces

`kernel/task/process.c` implements a process table of 32 control blocks.
Each process has a PID, parent PID, state, name, private address space, ELF
entry, user stack, kernel stack, saved interrupt frame, quantum, CPU ticks,
context switches, wait target, and exit status. PID zero represents the
kernel. The ready queue is FIFO, which yields round-robin scheduling.

Process creation:

1. Allocate a PCB and PID.
2. Create a private PML4 address space with shared kernel mappings.
3. Validate and load the embedded ELF64 image into that address space.
4. Map a private user stack page.
5. Allocate a 16 KiB kernel stack and synthesize an interrupt frame at its
   top with `rip = e_entry`, `rsp = user stack top`, `cs = 0x1B`,
   `ss = 0x23`, `rflags = 0x202`.
6. Mark the PCB ready and append it to the ready queue.

Fork:

1. Allocate a child PCB.
2. Deep-clone the parent address space into new physical frames.
3. Allocate a child kernel stack and copy the parent current interrupt frame.
4. Set the child frame `rax = 0` so `fork` returns zero in the child.
5. Queue the child ready.

The copied frame keeps the same `rip` and `rsp` as the parent, so the child
resumes at the instruction after the parent `int 0x80` with an identical user
stack and cloned memory.

Wait:

1. `process_reap_child` scans for a zombie child matching the target PID.
2. If a zombie exists, it frees the child address space and PCB and returns
   the exit status.
3. If only live children exist, the caller marks the process blocked, records
   the wait target, and rewinds `rip` by the 2-byte `int 0x80` instruction.
4. When a child exits, `process_wake_parent` moves a blocked parent back to the
   ready queue.
5. The parent resumes and re-executes `int 0x80`; the second attempt reaps the
   now-zombie child, writes the status to the user pointer, and returns the
   child PID.

Exit:

- `sched_exit_current` marks the process zombie with its status, wakes a
  blocked parent, then switches to the next ready process or abandons to the
  batch driver. The kernel stack and address space are freed when the zombie is
  reaped, never while still in use.

Important limits:

- One CPU, one batch at a time, up to eight processes per batch.
- Ready-queue round robin only; no priorities, affinity, or real-time classes.
- No demand paging, copy-on-write, or swapping; `fork` eagerly copies pages.
- No signals, credentials, sessions, or file-descriptor table.
- `exec` has no path lookup and reloads only the embedded ELF image.
- Kernel threads and user processes use different schedulers. There is no
  unified task model yet.

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

## User mode and ELF64 loading

Four freestanding C programs live in `user/`: `init.c`, `multi.c`,
`forktest.c`, and `guitest.c`. The build compiles each with Clang, links each
at 0x40000000 with `user/user.ld`, and embeds all four static ELF64 images in
kernel rodata through `user/program.asm`.

Each image has two PT_LOAD segments:

| Segment | Flags | Virtual range | Contents |
| --- | --- | --- | --- |
| Text | read, execute | 0x40000000 plus image size | code and rodata |
| Data | read, write | starts at 0x40001000 | data and BSS |

`kernel/cpu/elf.c` implements a static `ET_EXEC` loader for x86_64 images.
Validation checks:

- ELF magic, class 64, little-endian data, version, and OS ABI.
- `ET_EXEC`, `EM_X86_64`, header sizes, program-header size, and count.
- Program-header table bounds within the embedded image.
- PT_LOAD file ranges within the image and `p_filesz <= p_memsz`.
- Virtual ranges inside 0x40000000 to 0x40010000 without overflow.
- Page-aligned virtual addresses and page or smaller alignment metadata.
- Readable, non-writable-executable segment flags.
- No duplicate virtual pages across segments.
- `e_entry` inside an executable segment.

The loader copies file-backed bytes into newly allocated physical frames
before mapping. Bytes beyond `p_filesz` remain zero because the PMM zeroes
frames. Read-only segments are mapped without PTE_WRITABLE; writable segments
receive the write bit. On failure, all pages mapped by that load are unmapped
and freed. `elf_load_into` performs the same operations against a specified
process address space using `vmm_map_user_page`.

The user stack is a separate writable page at 0x40010000. Initial RSP is
0x40010FF8 so the stack pointer satisfies the x86_64 ABI alignment at a C
function entry.

Entering Ring 3:

1. `sched_run_batch` calls `sched_enter_user`, which saves the batch kernel
   context and loads the process saved frame pointer.
2. The epilogue sets `ds` and `es` to the user data selector, restores all
   general registers, skips the error code and vector, and executes `iretq`.
3. Ring 3 runs with the process CR3, its own user pages, and interrupts
   enabled by the saved `rflags`.
4. `int 0x80` traps through the DPL-3 gate. The CPU switches to
   `TSS.RSP0` and the common stub builds the register frame on the process
   kernel stack.
5. The syscall dispatcher validates user pointers against the process address
   space and performs the operation, or blocks the process and rewinds `rip`.
6. `exit` marks the process zombie and resumes another process or the batch
   driver.

Syscall ABI:

| RAX | Name | Arguments | Result |
| --- | --- | --- | --- |
| 1 | write | RDI=fd, RSI=buffer, RDX=length | bytes written or -1 |
| 2 | exit | RDI=status | does not return |
| 3 | getpid | none | current process PID |
| 4 | uptime | none | milliseconds since boot |
| 5 | yield | none | 0 |
| 6 | fork | none | child PID in parent, 0 in child, -1 on error |
| 7 | exec | none | replaces current image and resets user state |
| 8 | wait | RDI=child PID, RSI=status pointer | reaped PID or -1 |
| 9 | fb_info | RDI=buffer | framebuffer metadata or -1 |
| 10 | fb_rect | RDI=x, RSI=y, RDX=w, R10=h, R8=color | 0 or -1 |
| 11 | fb_pixel | RDI=x, RSI=y | packed pixel value or -1 |
| 12 | mark | RDI=marker id | records a scheduler trace mark |

Only file descriptors 1 and 2 are accepted by `write`, and both target the
kernel console. Buffers must be fully mapped with user permission and are
limited to 4096 bytes per call. The 4th, 5th, and 6th arguments use R10, R8,
and R9 as in the System V calling convention.

Important limitations:

- Static non-PIE images only; no relocation, dynamic linking, argv, envp, or
  auxiliary vector.
- Fixed user window; no ASLR, stack protector, or user heap.
- No file-descriptor table, signals, or credentials.
- No NX, SMEP, SMAP, KASLR, or W^X page-table enforcement. The loader rejects
  W+X ELF segments, but NX is not enabled so writable pages stay executable.

## Graphics stack

`kernel/gui/fb.c` implements the framebuffer driver:

- Scans PCI bus 0 for class code 0x03 and reads BAR0.
- Detects the Bochs dispi interface through I/O ports 0x1CE and 0x1CF.
- Sets X resolution, Y resolution, 32 bpp, virtual dimensions, and the LFB
  enable bit, then reads the registers back to confirm the mode.
- Maps VRAM through the shared device window with cache-disabled pages, so
  every process address space can use the same supervisor mapping.
- Falls back to a heap-backed shadow surface when dispi is unavailable.
- Supports VRAM write and readback probes outside the visible text area,
  mode restore, and a text-mode console reset.

`kernel/gui/gfx.c` defines `struct gfx_surface` and primitives: initialize,
clear, plot, get pixel, clip, fill rectangle, horizontal line, vertical line,
rectangle frame, blit, FNV checksum, and color counting.

`kernel/gui/wm.c` defines the window-manager abstraction:

- Up to eight windows with heap-backed ARGB surfaces.
- Window attributes: id, position, size, background, visibility, ownership.
- Create, destroy, find, enumerate, fill, clear, and z-order tracking.
- `wm_draw_to` composites every visible window into a target surface in
  creation order.

The kernel exposes framebuffer access to Ring 3 only through `fb_info`,
`fb_rect`, and `fb_pixel`. User programs cannot map VRAM, so drawing requests
are validated and executed by the kernel on behalf of the process.

`kernel/include/kernel.h` and `kernel/drivers/display.c` additionally keep the
display-device registry introduced in 0.3.0: a locked table of up to eight
`struct display_device` objects with framebuffer metadata and
initialize/set-mode/present/shutdown hooks. The registry is independent of the
PCI framebuffer driver and is covered by its own regression test.

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
memory, process, RTC, and power commands. `usertest` creates a fresh process
and runs the embedded ELF in its private address space. The shell does not
implement pipes, redirection, background jobs, environment variables, or
quoting beyond double-quoted arguments.

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
3. Compile the C user program, link the static ELF64 image, and assemble the
   embedding object that includes the complete ELF.
4. Compile every kernel C file twice: normal and with `-DZ_SELFTEST`.
5. Assemble every kernel NASM file.
6. Link each kernel ELF with LLD and the kernel linker script.
7. Convert each ELF to a flat binary and enforce the 320 KiB loader limit.
8. Place the kernel after stage 2 in a zeroed 1.44 MiB image.
9. Write sizes and SHA-256 hashes to `build/manifest.json`.

The compiler include path adds `kernel/mm`, `kernel/task`, and `kernel/gui` for the
address-space and process interfaces. New C files are discovered recursively
by the build script.

Compile flags include freestanding mode, no PIC/PIE, no stack protector, no
red zone, no builtins, no MMX/SSE, frame pointers, and optimization level 2.

## Reliability and current boundaries

- Malformed BootInfo leads to a panic, not recovery.
- Kernel page faults, invalid opcodes, double faults, and stack faults halt.
- User exceptions terminate the current process with an error status; the
  batch driver reports it and the caller reaps the zombie. There is no
  separate fault-recovery supervisor.
- The VFS and heap are protected by global locks. Contention and scalability
  are not modeled.
- There is no disk, so ramfs contents vanish on restart.
- QEMU is the reference platform. Real-hardware behavior depends on BIOS
  compatibility, floppy geometry support, and chipset-specific power control.
- The kernel assumes exactly one CPU and does not program the local APIC.

These boundaries are intentional for a 0.4.0 foundation and are tracked in
the roadmap.
