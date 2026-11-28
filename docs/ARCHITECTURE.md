# Z-OS Architecture

This document describes the Z-OS 1.4.0 implementation as it exists in this
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

Non-goals for 1.1.0:

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
| Mouse | PS/2 auxiliary device, interrupt vector 44 |
| Storage | ATA primary channel polled PIO at 0x1F0, control 0x3F6 |
| Network | PCI e1000 82540EM, MMIO BAR, RX and TX descriptor rings, IRQ line from PCI config |
| Transport | TCP control blocks, delayed acknowledgements not implemented, retransmission timer from network polling |
| Hardening | SMEP, SMAP, AC scoped user copies, heap KASLR slot, NX via page table NX bit not yet enabled |
| Internet | DHCP client, DNS cache with negative entries, HTTP 1.0 GET client, socket process ownership |
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
7. It asks the CPU whether long mode exists: CPUID leaf 0x80000000 must
   report a maximum extended leaf of at least 0x80000001, and CPUID leaf
   0x80000001 must set EDX bit 29. A CPU without long mode produces diagnostic
   code 6 instead of a fault.
8. It validates the page tables before enabling paging. Each table base must
   be 4096-byte aligned, and the four entries written earlier must match the
   expected values exactly: PML4[0] 0x2003, PDPT[0] 0x3003, PD[0] 0x83, and
   PD[1] 0x200083. A failure writes a red VGA message with a diagnostic code
   and halts instead of triple faulting.
9. It enters long mode in the Intel manual order: set CR4.PAE, read CR4 back to
   confirm the PAE bit is set (code 7 otherwise), read MSR 0xC0000080 into
   EAX:EDX, set bit 8 with `bts`, write the MSR back, read the MSR again and
   test bit 8 (code 5 if it did not take effect), load CR3 with the PML4 frame,
   set CR0.PG and CR0.PE, then perform the far jump to the 64-bit code
   descriptor. CR3 is reloaded once after paging is enabled to discard stale
   TLB entries.
10. It places the BootInfo pointer in RDI and jumps to 0x100000.

The kernel repeats the verification from the other side of the transition:
`kmain` reads MSR 0xC0000080 and CR4, requires EFER.LME and CR4.PAE to be set,
panics if either is missing, and prints `Long mode: EFER.LME verified,
CR4.PAE verified` so the transition is confirmed both in the loader and in the
64-bit kernel.

Loader diagnostic codes shown on the VGA screen:

| Message | Meaning |
| --- | --- |
| `Z-OS STAGE2 PAGING ERROR 1` | PML4 base misaligned or PML4[0] wrong |
| `Z-OS STAGE2 PAGING ERROR 2` | PDPT base misaligned or PDPT[0] wrong |
| `Z-OS STAGE2 PAGING ERROR 3` | Page directory base misaligned or PD[0] wrong |
| `Z-OS STAGE2 PAGING ERROR 4` | PD[1] wrong, identity map incomplete |
| `Z-OS STAGE2 LONG MODE ERROR 5` | EFER.LME did not take effect after the MSR write |
| `Z-OS STAGE2 LONG MODE ERROR 6` | CPU reports no long mode support through CPUID |
| `Z-OS STAGE2 LONG MODE ERROR 7` | CR4.PAE did not read back as set |

Each message ends with `HALT` and the CPU stops with interrupts disabled, so
the failure is visible on screen instead of producing a reset loop.

Every loader failure path reports to the VGA text buffer, so an early paging
breakdown produces a readable message rather than a reset loop. The kernel
installs its real IDT in `kmain` before the physical allocator, page-table
manager, and heap start touching mapped memory, so faults after that point are
reported by the exception handler instead of escalating.

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

## VFS, ramfs, and file descriptors

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

`kernel/fs/fd.c` implements per-process descriptor tables:

- 16 entries per process: 0 read console, 1 write console, 2 write console.
- Entry types: console, VFS node, null device.
- Flags: read, write, append. Reads and writes are rejected when the flag is
  missing.
- `open` resolves a path to a VFS node, `dup` copies an entry, `close` releases
  it, and `fork` copies the whole table so parent and child own independent
  descriptor state.
- VFS file descriptors track their own byte offset.

Limitations: no permissions, ownership, timestamps beyond creation ticks, hard
links, symbolic links, mount points, page cache, journaling, persistent
backing store, fine-grained locking, or descriptor reference counts.

## Credentials

`kernel/task/cred.c` implements a minimal POSIX credential set:

| Field | Meaning |
| --- | --- |
| `uid`, `gid` | Real user and group identifiers |
| `euid`, `egid` | Effective identifiers used for checks |
| `privileged` | True when euid is root |

- Processes start with root credentials.
- `cred_can_access` implements owner, group, and other permission classes and
  grants everything to euid zero.
- `cred_seteuid` and `cred_setegid` reject changes that would escalate an
  unprivileged identity.
- `cred_switch_user` lets a privileged credential become a normal user.
- Credentials are inherited across fork.

VFS nodes do not yet store ownership or mode bits, so the checks are exercised
by unit tests and available to future filesystem and compatibility layers.

## Signals

`kernel/task/signal.c` implements signal state per process:

- 32 signal numbers, pending mask, blocked mask, handler table, and trampoline
  address.
- `SIGKILL` and `SIGSTOP` cannot be caught or blocked.
- Delivery happens at the end of an interrupt or system call return when a
  deliverable signal exists and the interrupted context is Ring 3.
- The kernel writes the current interrupt frame plus the user trampoline
  address below the user stack pointer, redirects `rip` to the handler, sets
  `rdi` to the signal number, and resumes Ring 3.
- The handler returns into the trampoline, which calls the `sigreturn`
  system call; the kernel restores the saved frame and resumes the interrupted
  instruction with its original return value.
- Delivery validates the target user stack range against the process address
  space and temporarily switches CR3 if the caller is not already running in
  that address space.
- Default actions terminate the process for fatal signals and ignore
  `SIGCHLD` and `SIGCONT`.

## Wait queue and timed wait

`kernel/task/waitq.c` implements blocking wait bookkeeping:

- Entries record the waiting PID, an optional target child PID, and a deadline
  in timer ticks.
- A child exit wakes every waiter whose target matches.
- The 100 Hz timer calls `waitq_expire`, which wakes parents whose deadline
  passed.
- `waitpid` first attempts to reap a zombie child. If a live child exists, it
  records a deadline, marks the process blocked, and rewinds the saved `rip` by
  the 2-byte `int 0x80` instruction so the call re-executes after wakeup.
- On re-execution the call reaps the child, returns `-EINTR` if a deliverable
  signal is pending, returns `-EAGAIN` if the deadline expired, or blocks again.
- Process exit and reaping remove stale queue entries.

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
| 13 | open | RDI=path, RSI=flags | descriptor or -1 |
| 14 | close | RDI=descriptor | 0 or -1 |
| 15 | read | RDI=fd, RSI=buffer, RDX=length | bytes read or -1 |
| 16 | getuid | none | real user identifier |
| 17 | geteuid | none | effective user identifier |
| 18 | signal | RDI=signum, RSI=handler, RDX=trampoline | 0 or -1 |
| 19 | kill | RDI=pid, RSI=signum | 0 or -1 |
| 20 | sigreturn | RDI=saved frame pointer | resumes the saved context |
| 21 | sigprocmask | RDI=mask, RSI=operation | previous mask |
| 22 | dup | RDI=descriptor | new descriptor or -1 |
| 23 | fb_text | RDI=x, RSI=y, RDX=color, R10=text | 0 or -1 |
| 24 | mouse | RDI=state buffer | 0 or -1 |
| 25 | gui_enter | none | requests a desktop session |
| 26 | gui_exit | none | requests leaving the desktop session |
| 27 | socket | RDI=domain, RSI=type, RDX=protocol | socket descriptor or -1 |
| 28 | bind | RDI=descriptor, RSI=port | 0 or -1 |
| 29 | sendto | RDI=fd, RSI=address, RDX=port, R10=data, R8=length | bytes sent or -1 |
| 30 | recvfrom | RDI=fd, RSI=buffer, RDX=capacity, R10=timeout, R8=source ip out, R9=source port out | bytes received or -1 |
| 31 | socket_close | RDI=descriptor | 0 or -1 |
| 32 | connect | RDI=fd, RSI=address, RDX=port | 0 or -1 |
| 33 | accept | RDI=fd, RSI=timeout | new descriptor or -1 |
| 34 | recv | RDI=fd, RSI=buffer, RDX=capacity, R10=timeout | bytes or -1 |
| 35 | send | RDI=fd, RSI=buffer, RDX=length | bytes or -1 |
| 36 | dns | RDI=name, RSI=address out | address or -1 |
| 37 | get_test_port | none | configured user network test port |
| 38 | select | RDI=read mask, RSI=write mask, RDX=timeout, R10=ready read out, R8=ready write out | ready count or -1 |
| 39 | set_nonblocking | RDI=descriptor, RSI=enable | 0 or -1 |
| 40 | poll | RDI=pollfd array, RSI=count, RDX=timeout | ready count or -1 |

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

`kernel/gui/font_data.h` holds a deterministic 8x16 ASCII atlas generated by
`tools/gen_font.py` from a real system font and committed to the tree.
`kernel/gui/font.c` renders single characters and strings into any graphics
surface.

`kernel/gui/mouse.c` drives the PS/2 auxiliary device: controller
configuration, IRQ12 packet assembly, delta and button decoding, bounds
clamping, and a 12x12 cursor with background save and restore.

`kernel/gui/compositor.c` implements the compositor abstraction: target
surface, heap-backed layers, visibility and z-order, damage rectangles, and a
flush callback invoked after each present.

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

## ATA storage

`kernel/drivers/ata.c` drives the primary ATA channel in polled PIO mode:

- Reset and drive selection on the primary bus at 0x1F0, control port 0x3F6.
- `IDENTIFY DEVICE` (0xEC) reads the model string and the 28-bit LBA sector
  count. The kernel prints the device line during boot.
- Reads (0x20) and writes (0x30) transfer one sector at a time through the
  16-bit data port. After every transfer the driver waits for BSY to clear
  before the next command, and `FLUSH CACHE` (0xE7) is issued after writes and
  waited on, which was required for the QEMU emulate device to commit data.
- Errors map to absent, timeout, I/O and range results. Polling keeps the
  driver deterministic for automated tests, at the cost of throughput.

## ZFS persistent filesystem

`kernel/fs/zfs.c` implements the Z-OS File System, a small persistent
filesystem. It is unrelated to OpenZFS and shares only the name. Layout:

| Region | Sectors | Contents |
| --- | --- | --- |
| Superblock | 0 | magic, version, block size, sector count, data start, file count |
| File table | 1 to 2 | 16 entries of 48 bytes: name, size, first sector, used |
| Data area | 64 onward | 128 contiguous sectors per file slot, 64 KiB per file |

- `zfs_format` writes the superblock and an empty table.
- `zfs_mount` validates the magic, version and block size, then reloads the
  table from disk.
- `zfs_write_file` allocates a slot, writes the payload sectors zero padded,
  then rewrites the superblock and file table.
- `zfs_read_file`, `zfs_remove_file`, `zfs_list` and `zfs_file_count` operate
  on the in-memory table.
- `zfs_mount_vfs` creates `/disk` and populates it with VFS file nodes whose
  contents are read from disk, so the mounted namespace is reachable through
  normal VFS lookups.
- `zfs_sync_vfs` walks `/disk` in the VFS, writes every regular file back to
  ZFS, removes disk entries whose VFS nodes disappeared, and flushes the
  device.

Limitations: fixed 16-file table, contiguous allocation, 64 KiB per file, no
checksums, no journaling, no crash consistency, no permissions, no
directories on disk.

## Mouse events and text-mode pointer

`kernel/gui/mouse.c` extends the PS/2 driver:

- The 8042 controller is initialized by flushing the output buffer, enabling
  the auxiliary port with command 0xA8, reading the controller configuration
  byte with 0x20, setting bit 1 to enable IRQ12 and clearing bit 5 to enable
  the auxiliary clock, writing the byte back with 0x60, then sending device
  commands through the 0xD4 prefix: `F6` set defaults and `F4` enable data
  reporting, each waiting for a `FA` acknowledgement. Without the 0xD4 prefix
  those bytes reach the controller instead of the mouse, which leaves the
  device without data reporting and produces a pointer that never moves.
- IRQ12 packets are decoded into relative delta, absolute clamped position and
  button state.
- Every packet with motion and every button change pushes a `mouse_event` into
  a 32-entry ring buffer. Events carry type, absolute position, delta and
  button mask; `mouse_pop_event` drains them for the desktop loop.
- `mouse_inject_event` allows the kernel and tests to feed the same queue
  without hardware.
- `mouse_text_cursor_update` draws a two by two cell block over the VGA text
  buffer, saving the cells it covers, and restores them when the pointer moves
  or when the console needs the cells. `console_putc_unlocked` restores the
  pointer before every character, so console output cannot be corrupted by the
  overlay. The timer tick refreshes the pointer at 100 Hz.
- The graphical cursor is a 16 by 16 bitmap split into an outline mask and a
  fill mask. The outline is drawn in black and the fill in white, giving a
  classic arrow with a crisp edge on both dark and light backgrounds.
- `mouse_render` saves the background pixels it covers and `mouse_restore`
  writes them back, so the desktop loop erases the cursor before repainting
  the scene and redraws it afterwards. That order is what prevents trails when
  windows move or content changes underneath the pointer.
- Graphics mode sets a flag that suppresses the text pointer while the
  framebuffer cursor is active.

## Desktop environment

`kernel/gui/desktop.c` builds the desktop on top of the window manager:

- Layout: 28-pixel status bar at the top, desktop icon column on the left,
  wallpaper, and a 36-pixel taskbar at the bottom.
- Four applications: Files, Terminal, System Monitor, and Exit. Each opens a
  titled window with painted content.
- Rendering clears the wallpaper, draws icons and labels, composites windows
  through `wm_compose`, then draws the status bar and taskbar above the
  windows, and finally the caller draws the mouse cursor.
- Hit testing covers icons, window title bars, close buttons, window bodies and
  taskbar buttons including the exit control.
- Interaction: icon click opens or raises an application; title bar press
  starts a drag that follows pointer motion and ends on release; close button
  destroys the window; taskbar buttons hide or restore windows; the exit
  control or the Esc key or a typed `exit` command ends the session.
- Keyboard behavior: digits 1 to 4 open applications, printable characters are
  collected into a command buffer, and Enter evaluates `exit`, `quit`, `close`
  or `gui`.

`kernel/gui/wm.c` gained window titles, title-bar rendering, a close box with a
cross, geometric hit tests, raise by z-order, visibility toggles and
`wm_compose` for framed composition. `wm_draw_to` still composites content
only, so earlier window-manager tests keep their original pixel semantics.

## Double buffering and dirty rectangles

`kernel/gui/fb.c` provides an off-screen double buffer for the framebuffer:

- `fb_double_buffer` allocates a full-screen RGB32 surface from the kernel heap
  the first time a graphics session starts and returns it as a normal
  `gfx_surface`.
- `fb_present_rect` copies one rectangle from the back buffer to VRAM and
  `fb_present` copies the whole frame.
- The desktop renders into the back buffer. Because every frame is composed in
  RAM first, the display never shows a half-finished desktop, which is what
  caused the visible flicker when the scene was drawn straight into VRAM.

`kernel/gui/desktop.c` tracks a damage region:

- `desktop_dirty_mark` merges rectangles into a single union, clipping them to
  the screen.
- Window drags mark the union of the old and the new window rectangle, so a
  drag only copies two small window-sized regions per pointer packet.
- Opening, closing, hiding, raising or focusing windows marks the full screen.
- `desktop_dirty_take` returns and clears the pending region.

The GUI session loop only repaints when a handler reports a scene change.
Pointer movement without interaction erases and redraws only the 16 by 16
cursor rectangle on VRAM and performs no scene compositing or full present at
all. The session reports `gui: frames full=<n> dirty=<n> moves=<n> move_full=<n>`
on exit; the automated GUI stage asserts that `move_full` is zero, which proves
that mouse movement never triggers a full-screen repaint.

## GUI mode switching

The shell owns the console, so the desktop runs as a session inside the shell
thread:

- `gui` calls `gui_run_session`: set the framebuffer mode, mark graphics input
  mode, initialize the desktop, then loop on mouse events and non-blocking
  keyboard input, re-rendering after each event.
- `exit` typed inside the desktop, the Esc key, or the taskbar exit control
  ends the loop. The session restores text mode with a full VGA mode 3
  sequence: disable video through the attribute controller, program the
  miscellaneous output register, reset and program the sequencer, unlock and
  program all 25 CRTC registers, program the nine graphics controller
  registers, program the 21 attribute controller registers, restore the
  underline cursor shape, clear the 0xB8000 text buffer, reload the complete
  256-entry 8x16 font into plane 2 through the sequencer and graphics-controller
  memory map, and only then re-enable video. Reloading the font is what makes
  the restored console robust: if the character generator plane was disturbed
  by the VBE session, text mode would otherwise display solid blocks or white
  lines instead of glyphs.
- The kernel self-test reads back the CRTC horizontal and vertical timing
  registers, the sequencer clocking and memory mode registers, the graphics
  controller mode registers, the attribute controller mode control register,
  a font plane byte and a text buffer cell after the restore, so a broken
  restore sequence fails the boot self-test instead of appearing as screen
  corruption.
- Syscalls 25 and 26 (`gui_enter`, `gui_exit`) set request flags that the shell
  checks after each command, so a ring-3 program can ask for a session change
  without owning the console.

## PCI enumeration and e1000 driver

`kernel/drivers/pci.c` provides configuration space access through ports 0xCF8
and 0xCFC, device search by vendor and device identifier, BAR decoding and a
helper that sets the memory space and bus master bits in the PCI command
register. Without bus master enable the NIC cannot DMA descriptor rings or
buffers, which silently prevents both transmission and reception.

`kernel/drivers/e1000.c` drives the Intel 82540EM:

- The BAR0 memory window is mapped into the shared device window with cache
  disable and write through attributes.
- Reset through CTRL.RST, link up through CTRL.SLU, MAC address from RAL and
  RAH, multicast table cleared, receive and transmit rings allocated from the
  physical memory manager so that descriptor and buffer addresses are identity
  mapped physical addresses. Heap memory is not identity mapped, so heap
  addresses must never be handed to the NIC.
- 32 receive descriptors with 2048 byte buffers and 16 transmit descriptors,
  legacy descriptor layouts, RCTL with broadcast accept, 2048 byte buffers and
  CRC stripping, TCTL with pad short packets and collision parameters.
- The interrupt handler only drains hardware descriptors into a small raw frame
  queue; protocol processing runs in thread context from `net_poll`, which
  keeps ARP resolution and reply generation out of interrupt context. The timer
  tick also drains raw frames so reception works even if the PCI interrupt line
  is not delivered.

## Network stack

`kernel/net` implements a small IPv4 stack:

- `net.c` holds the configuration (MAC, 10.0.2.15, gateway 10.0.2.2, netmask
  255.255.255.0), byte order helpers, the internet checksum, dotted address
  parsing and formatting, frame counters and the receive entry point.
- `eth.c` parses 14 byte Ethernet headers, dispatches ARP and IPv4 frames and
  builds outgoing frames with the configured source MAC.
- `arp.c` keeps an eight entry cache, builds requests and replies, answers
  requests for the local address and resolves addresses synchronously with a
  ten millisecond polling loop.
- `ipv4.c` builds and validates IPv4 headers including the header checksum,
  chooses the next hop from the netmask, resolves the next hop through ARP and
  dispatches ICMP and UDP payloads.
- `icmp.c` answers echo requests, sends echo requests and implements a ping
  that waits for a matching identifier and sequence.
- `udp.c` provides eight sockets with a four entry receive queue per socket,
  port binding, datagram transmission and reception with timeouts.
- `socket.c` exposes socket descriptors 1 to 8 mapped onto UDP sockets.

System calls 27 to 31 implement `socket`, `bind`, `sendto`, `recvfrom` and
`socket_close`; user buffers are validated against the caller address space
before copying.

Shell commands `netinfo`, `arp`, `ping` and `udp-echo` expose the stack, and
`udp-echo` can be driven from a host through a QEMU user mode UDP host forward,
which is how the automated network stage performs a real round trip.

## TCP transport

`kernel/net/tcp.c` implements a minimal reliable stream transport:

- Eight control blocks with local and remote address and port, send and receive
  sequence numbers, a sliding send window, a 2048 byte reassembly buffer and a
  2048 byte send buffer. States cover CLOSED, LISTEN, SYN_SENT, SYN_RECV,
  ESTABLISHED, FIN_WAIT_1, FIN_WAIT_2, CLOSE_WAIT, LAST_ACK, TIME_WAIT and
  CLOSING.
- Segments carry the standard 20 byte header. The checksum covers a 12 byte
  pseudo header (source, destination, zero, protocol, length) plus the segment.
- Passive open: `tcp_listen` creates a listener, an incoming SYN allocates a
  child control block in SYN_RECV and answers with SYN-ACK, and the completing
  ACK moves the child to ESTABLISHED. `tcp_accept` returns an established child
  that has not been accepted yet.
- Active open: `tcp_connect` sends SYN from a fresh initial sequence number,
  waits for a matching SYN-ACK and completes the handshake with an ACK.
- Transmission: `tcp_send` appends to the send buffer and flushes segments
  limited by the minimum of the peer window, the unsent bytes and the maximum
  segment size. Acknowledgements advance the unacknowledged sequence, discard
  the matching prefix of the send buffer and trigger more flushes.
- Retransmission: `tcp_tick` runs from `net_poll` and, when unacknowledged data
  or a handshake control segment is pending past the retransmission timeout,
  retransmits the oldest segment and backs the timeout off. Three duplicate
  acknowledgements trigger an immediate fast retransmission. The retry counter
  aborts the connection after six attempts.
- Reception accepts only in-order payload at the receive sequence, appends it
  to the reassembly buffer, acknowledges every segment and advances on FIN to
  CLOSE_WAIT or TIME_WAIT. Checksum failures are counted and dropped.
- `tcp_recv` polls the network until data is reassembled, a FIN arrives or the
  timeout expires; `tcp_close` performs the FIN handshake with bounded waits.

Waiting inside the network stack must work in both kernel thread context and
inside a system call made by a ring 3 process. `net_delay_ms` uses the thread
scheduler when a kernel thread is current and a halt based wait when a user
process is executing a syscall, because the thread scheduler is suspended while
a user batch runs. Protocol processing is never re-entered from a delay loop.

## DNS client

`kernel/net/dns.c` builds recursive A record queries with label encoded names,
sends them from an ephemeral UDP port to the configured DNS server (10.0.2.3 by
default, settable at runtime with `dnsserver` and `dnsport`) and parses the
response: transaction identifier, response bit, result code, question skipping
and answer iteration with compression pointer support. The first A record with
class IN yields the IPv4 address.

## DHCP client

`kernel/net/dhcp.c` implements the client side of DHCP:

- Discovery builds a BOOTP style message with the transaction identifier, the
  broadcast flag, the client hardware address, the magic cookie and options for
  message type, client identifier, parameter request list and the requested
  address. It is sent as an IPv4 broadcast frame to port 67 from port 68.
- The offer parser validates the reply opcode, the transaction identifier, the
  magic cookie and the option bounds before reading the offered address,
  subnet mask, router, DNS server, server identifier and lease time.
- After an offer the client sends a request naming the offered address and the
  server, waits for the acknowledgement and applies the lease through
  `dhcp_apply_lease`, which updates the address, netmask, gateway and DNS server
  in the network configuration and clears the ARP cache.
- Lease acquisition is bounded by a timeout split across polling rounds; the
  shell command `dhcp` reports the resulting lease.

## DNS cache

`kernel/net/dns.c` keeps an eight entry cache with a 64 byte name key, the
address, the raw TTL, an expiry timestamp from `timer_uptime_ms` and a negative
flag. Positive answers are cached with the record TTL from the response
(default 60 seconds when absent); failed lookups are cached negatively for five
seconds. `dns_resolve` checks the cache first, returns cached negatives as
failures, and only issues a query on a miss. `dnscache` prints entries and hit
and miss counters, and the automation asserts that a second resolution of the
same name produces no second query.

## HTTP client

`kernel/net/http.c` implements a minimal HTTP/1.0 client:

- URL parsing accepts `http://host[:port][/path]` and defaults the port to 80
  and the path to `/`.
- `http_get` opens a stream socket, connects, sends a GET request with a Host
  header and a Connection close header, accumulates the response up to 4096
  bytes and parses it.
- Response parsing extracts the status code, locates the header terminator,
  reads Content-Length and the chunked transfer flag, and reports whether the
  body is complete.
- Only 2xx responses are treated as success; the body is copied to the caller
  buffer with a trailing zero.

## Socket ownership

`kernel/net/socket.c` records the creating process identifier in every socket
record. `socket_create` takes the current process identifier, while
`socket_create_for_owner` allows explicit ownership for tests. When a process is
marked as a zombie, `process_mark_zombie` calls `socket_cleanup_process`, which
iterates the socket table, unbinds UDP ports, closes TCP control blocks and
clears the entries. A ring 3 process that leaks sockets therefore releases its
ports and connections on exit, which the internet verification stage asserts by
checking that the tables are empty and that the leaked listening port can be
bound again immediately.

## Security hardening

`kernel/cpu/security.c` turns on hardware supervisor protections when the CPU
reports them through CPUID leaf 7: the supervisor mode execution prevention bit
and the supervisor mode access prevention bit are set in CR4. The kernel prints
the resulting CR4 value and exposes the support and activation state.

Supervisor mode access prevention makes the kernel fault when it touches user
pages without setting the AC flag, so every syscall path that reads or writes a
user pointer opens a narrow AC window with `security_stac` and closes it with
`security_clac`: string copies, file reads and writes, signal frame saving and
restoring, wait status stores, framebuffer info and text, mouse state, DNS
address results, select result masks, and socket send and receive buffers.
Signal delivery writes the saved frame under the same window. The window is
coarse: the kernel keeps AC set while the copied buffer is processed by VFS or
console code, which is acceptable for a first hardening step but should be
narrowed to the actual copy in future work.

Heap address space layout randomization is implemented in `kernel/mm/heap.c`.
The heap keeps its reserved eight megabyte window at 0x60000000 because process
address spaces share those page directory entries, but the first heap block is
placed at a random 64 KiB aligned slot inside the first four megabytes using
entropy from the CPU timestamp counter mixed by `security_random`. Every kernel
object allocated from the heap, including thread stacks, process control blocks
and DMA descriptors, therefore lands at a runtime dependent offset. The kernel
text and the boot page tables remain at fixed addresses because the flat loader
cannot relocate the image.

## Unified task base

`kernel/task/task.c` introduces a shared task descriptor embedded at the front
of both kernel threads and user processes. It records kind, identifier,
scheduling state, quantum and wake tick, provides shared state and kind names,
and counts threads and processes through one interface. Kernel threads and user
processes still have separate scheduler paths, but the descriptor, accounting
and reporting layer is unified and is the base for a future single scheduler.

## POSIX style sockets

`kernel/net/socket.c` adds descriptor flags and polling:

- `socket_set_nonblocking` marks a descriptor non-blocking. Receive, send and
  accept then return `-EAGAIN` when the operation would block, and the
  non-blocking state is visible through `socket_is_nonblocking`.
- Readiness checks ask UDP sockets for queued datagrams and TCP control blocks
  for buffered data, end of stream, established state and send buffer space.
- `socket_select` takes read and write descriptor bit masks, polls readiness
  until a match or the timeout, and returns the ready masks. The kernel system
  call uses a 32 bit descriptor bitmask instead of the historical fd_set layout;
  the user C library wraps it as `net_select`.

System calls 38 and 39 expose select and non-blocking control. User programs
link against `user/libc.h`, which provides `net_socket`, `net_bind`,
`net_connect`, `net_send`, `net_recv`, `net_sendto`, `net_recvfrom`,
`net_select`, `net_nonblocking` and `net_close` over the syscall wrappers.

The ring 3 `wget` tool uses the library to open a TCP connection to the
configured test host, send a HTTP/1.0 GET request, read the response, locate the
header terminator, and write the body to `/wget.out` through the file system
calls. The shell command `wget <port>` creates the output file, copies the port
into the kernel test configuration, runs the program and reports its status.

## Kernel hardening

Guard pages: `kernel/mm/kstack.c` manages kernel stacks in a dedicated window
above the heap. The window is shared into every process address space through
the same page directory entries as the heap. Each slot reserves one unmapped
guard page followed by the mapped stack pages, so a downward stack overflow
hits an unmapped page instead of neighbouring memory. Threads and process
kernel stacks both use this allocator, and freeing unmaps and returns the
frames.

Stack canaries: the build adds `-fstack-protector-strong`, the kernel defines
`__stack_chk_guard` and `__stack_chk_fail`, and the canary is seeded from the
boot entropy source before kernel threads start. A corrupted canary calls
`panic`.

NX and W^X: extended feature detection enables EFER.NXE and the kernel sets
CR0.WP. `kernel/mm/protect.c` splits the low identity page directory so the
kernel image is mapped with 4 KiB pages: text and read-only data lose the
writable bit, data and BSS gain the no-execute bit, and the page containing the
text to data boundary stays writable and executable because section boundaries
are not page aligned. Heap, guarded stacks and DMA buffer pages are mapped no
execute. This is a partial W^X policy: the kernel image is split and data pages
are non executable, but the boundary page and the identity mappings for the
physical memory manager remain executable.

KASLR: the heap base slot inside the reserved heap window is randomized from
timestamp entropy, and guarded stacks live above the heap in a fixed window, so
kernel object and stack addresses vary per boot. The flat-loaded kernel image
and the boot page tables remain at fixed addresses.

Kernel page table isolation was designed but not implemented in the 1.1.0
milestone and remains unimplemented in 1.2.0. A late boot NX sweep now sets the no-execute bit on every writable leaf
mapping outside the kernel text range, so the RWX audit reports zero pages:
text stays read only and executable, every other mapping is non executable.
Kernel page table isolation and kernel image KASLR remain unimplemented: the
user page table still contains kernel mappings, and the flat loader still
places the kernel at a fixed physical and virtual base. The 1.1.0 note follows:
the kernel still runs every path with the full process page table. The design
requires a restricted user page table, entry trampolines and CR3 switching on
every syscall and interrupt return, and the current return paths (signal
delivery, fork resume, timer preemption) would all need auditing. It is the
first item of the next hardening milestone.

## Poll and readiness

`kernel/net/socket.c` exposes `socket_events`, mapping UDP queue state and TCP
receive, send and accept readiness to POLLIN, POLLOUT and POLLERR. System call
40 implements poll over up to sixteen `struct pollfd` entries, copying the array
under an AC window, polling until any descriptor is ready or the timeout
expires, and writing revents back. `user/libc.h` provides `struct pollfd` and
`net_poll`, and the shell `polltest` command exercises idle, readable and
timeout behaviour.

## KPTI stage one: restricted user view and entry trampoline

`kernel/mm/kpti.c` builds a separate page table hierarchy for the restricted
user view at boot:

- A private PML4, PDPT, direct page directory and a sealed copy of the user page
  directory. The sealed copy keeps only the user mapped entries, so the user
  view no longer inherits the kernel heap page directory entries that the
  process address space shares.
- The view maps exactly: the trampoline pages, the interrupt descriptor table
  page, the trampoline stack, the current kernel stack pages, the probe state
  words, and the process user pages through the sealed directory. Kernel image
  pages, heap pages, the high-half direct map and the device window are absent.
- `kernel/arch/trampoline.asm` holds the entry trampoline in its own executable
  section. `kpti_probe_switch` switches CR3 to the restricted view, observes the
  value, and switches back; `kpti_probe_entry` is installed on interrupt vector
  0x81 and is reached only while the restricted view is loaded, where it reads
  the kernel CR3 slot and switches to the kernel table before returning.
- `kpti_enter_kernel`, `kpti_leave_kernel` and the probe helpers all run with
  interrupts masked, and every probe restores the caller CR3 before returning,
  so preemption cannot observe a partially switched state.

This is stage one of KPTI: the restricted view exists, the trampoline switches
tables, and the kernel verifies that the view hides kernel memory. The kernel
does not yet run user processes on the restricted view, so no syscall or
interrupt return path is switched yet; that is the next stage.

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
2. Assemble stage 2 and verify it is exactly 16 KiB. The build records
   SHA-256 digests for stage 1, stage 2, the flat kernel, both images, and all
   user programs in `build/manifest.json`.
3. Compile the C user program, link the static ELF64 image, and assemble the
   embedding object that includes the complete ELF.
4. Compile every kernel C file twice: normal and with `-DZ_SELFTEST`.
5. Assemble every kernel NASM file.
6. Link each kernel ELF with LLD and the kernel linker script.
7. Convert each ELF to a flat binary and enforce the 320 KiB loader limit.
8. Place the kernel after stage 2 in a zeroed 1.44 MiB image.
9. Write sizes and SHA-256 hashes to `build/manifest.json`.

The compiler include path adds `kernel/mm`, `kernel/task`, `kernel/gui`,
`kernel/fs`, `kernel/drivers`, and `kernel/net` for the
address-space and process interfaces. New C files are discovered recursively
by the build script.

Compile flags include freestanding mode, no PIC/PIE, no stack protector, no
red zone, no builtins, no MMX/SSE, frame pointers, and optimization level 2.

## Reliability and current boundaries

- Malformed BootInfo leads to a panic, not recovery.
- Stage 2 validates its page tables and EFER state before enabling paging and
  reports coded failures to the VGA text buffer; the automated boot fault
  check verifies image digests, absence of triple faults and page-fault
  exceptions, and reads the VGA text buffer through the QEMU monitor to confirm
  the screen shows the kernel banner and shell prompt and never a loader error.
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

These boundaries are intentional for a 0.6.0 foundation and are tracked in
the roadmap.
