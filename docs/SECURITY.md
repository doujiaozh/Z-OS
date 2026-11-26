# Z-OS Security Model

## Scope

This document describes the security properties and known weaknesses of Z-OS
0.8.0 Transport. It is deliberately conservative. The system is a development
prototype and must not be used to protect sensitive data.

## Implemented boundaries

- The kernel executes in ring 0. Static ELF64 C programs execute in ring 3.
- Each process owns a private PML4, PDPT, user page directory, and user page
  tables. User ELF segments and the user stack are mapped with the user bit at
  every page table level. Kernel text, data, page tables, the shared kernel
  heap, and the high-half direct map are supervisor-only.
- The shared kernel low identity window and high-half direct map are identical
  supervisor mappings in all process address spaces.
- Process user pages are private physical frames. Address-space cloning copies
  user pages into new frames rather than sharing them.
- `vmm_destroy_address_space` frees user frames and page tables when a process
  is reaped.
- The static ELF loader validates magic, class, endianness, type, machine,
  header sizes, program-header bounds, segment bounds, address range, overlap,
  alignment, and segment flags before mapping.
- The loader rejects every segment that requests both write and execute. Text
  segments are mapped without write permission; writable data segments retain
  write permission.
- The syscall gate has DPL 3. All other IDT gates have DPL 0.
- The TSS provides a dedicated ring-0 stack for interrupts and syscalls that
  arrive while the CPU is in ring 3.
- Syscall buffer arguments are validated page by page against the current
  process address space with `vmm_user_range_valid_in` before the kernel
  copies or prints from them.
- Syscall write lengths are capped at 4096 bytes.
- The I/O privilege level is zero. Ring-3 code cannot execute `IN`, `OUT`,
  `CLI`, `STI`, `HLT`, or privileged control-register instructions.
- A ring-3 exception terminates only the faulting process with an error exit
  status; the kernel continues and the zone is reaped by its parent or the test.
- User processes are preemptively scheduled, but every switch passes through a
  single supervisor scheduler path that saves and restores complete interrupt
  frames and switches CR3 under kernel control. Ring-3 code cannot observe or
  modify another process kernel stack.
- Every process kernel stack lives in the shared supervisor heap, so it is not
  reachable from Ring 3 even while a different process CR3 is loaded.
- Blocking system calls rewind the saved user instruction pointer by exactly
  the length of `int 0x80`; the kernel never resumes user code at an
  attacker-chosen address because the frame is kernel-generated or copied from
  the caller own saved frame.
- The framebuffer is mapped only through the shared supervisor device window.
  Ring-3 programs cannot map VRAM and must draw through validated `fb_info`,
  `fb_rect`, and `fb_pixel` system calls with coordinate and size bounds.
- `fb_rect` rejects coordinates above 0xFFFF and the driver clips every
  rectangle to the active surface, so a process cannot write outside the
  framebuffer mapping through the system call interface.
- The scheduler rejects batches that reference missing or non-ready processes
  and reports deadlock instead of spinning, so a stuck batch returns control to
  the kernel caller.
- Descriptor system calls validate every descriptor index against the calling
  process table and reject reads on write-only descriptors and writes on
  read-only descriptors. A forked child receives a table copy, so closing a
  descriptor in the child cannot release the parent descriptor.
- Path system calls copy user strings one byte at a time with a per-byte user
  range check, so a string that crosses into an unmapped page is rejected
  before use.
- Signal delivery validates the computed user signal frame range against the
  target address space, refuses to build a frame when the handler or trampoline
  address is missing, and performs frame writes under the target CR3.
- `sigreturn` requires a fully mapped 176-byte user context and replaces the
  interrupt frame from validated user memory.
- `SIGKILL` and `SIGSTOP` cannot be caught or blocked, so a runaway process
  remains killable from the kernel side.
- Wait queue entries are keyed by PID, removed on process exit and reaping, and
  bounded to 32 entries.
- Framebuffer text system calls validate the user string byte by byte and cap
  the text length at 96 characters.
- Mouse state is read-only for user processes through a single validated
  structure copy; user code cannot program the PS/2 controller.
- The ATA driver validates every transfer against the identified sector count
  and rejects out-of-range requests, so a bad filesystem request cannot wrap
  the LBA.
- The ZFS mount validates the superblock magic, version and block size before
  trusting the file table, and the file table is bounded to 16 entries and
  64 KiB per file.
- The desktop session runs inside the kernel shell thread and copies no user
  pointers, so a user process cannot supply UI state directly; syscalls 25 and
  26 only set request flags.
- Socket system calls validate domain and type, validate every user buffer
  against the caller address space before copying, cap datagram sizes at 512
  bytes, reject unbound descriptors and reject address zero.
- The e1000 driver validates frame sizes before transmission and copies frames
  into fixed DMA buffers owned by the kernel; received frames are length
  checked against the descriptor before dispatch and the raw frame queue is
  bounded.
- The network stack validates Ethernet length, IPv4 version, header length,
  total length and header checksum before dispatching, and bounds every
  protocol payload by the IPv4 total length.
- TCP validates the segment header length, the pseudo header checksum, the peer
  window and the sequence ranges before touching buffers; the receive buffer is
  bounded and out of order data is acknowledged but not stored.
- Sockets validate every user buffer and cap TCP transfers at 2048 bytes per
  call; DNS names, query sizes and response sizes are bounded.
- Physical frames returned by the allocator are zeroed, so ELF BSS bytes are
  zero before user execution.
- The boot image is deterministic and the build hash is recorded.
- QEMU's debug-exit port is used only by the self-test image and only by the
  self-test thread.

## Known weaknesses

### Address-space isolation

Each process owns a private PML4 and user page tables, and user frames are not
shared across processes. Two processes loading the same ELF therefore receive
different physical pages at the same virtual addresses.

Transport surfaces are not yet a security boundary:

- TCP has no SYN flood mitigation, no randomized initial sequence numbers
  beyond a monotonic counter, no connection rate limiting and no per-process
  ownership of control blocks.
- Retransmission is driven by network polling, so connections stall when no
  polling thread runs; there is no keepalive and no TIME_WAIT table bounded by
  memory pressure.
- DNS responses are not authenticated; the client accepts any response whose
  transaction identifier matches and does not check the source address beyond
  the UDP socket demultiplexing.
- Stream sockets share the global socket table with datagram sockets, so the
  same descriptor ownership weaknesses apply.
- There is no TLS, no certificate validation and no HTTP client hardening.

Network surfaces are not yet a security boundary:

- There is no authentication, encryption, firewall or packet filtering; any
  host that can reach the QEMU user network can send ARP, ICMP and UDP frames
  to the guest.
- The ARP cache has eight entries with no locking and no aging; a flood of ARP
  replies can evict legitimate entries.
- The UDP receive queue is four datagrams per socket and silently drops excess
  traffic; there is no backpressure and no ICMP error reporting to applications.
- Socket descriptors are process agnostic global objects; a descriptor created
  by one process can be used by another if the number is guessed, and closing a
  process does not close its sockets.
- The IPv4 stack does not implement fragmentation, reassembly, broadcast
  filtering or TTL decrementing for forwarded traffic.
- The e1000 driver trusts descriptor status bits and does not validate the
  receive length against the buffer beyond clamping, and the raw frame queue
  drops frames silently when full.

Storage and desktop surfaces are not yet a security boundary:

- ZFS has no checksums, journaling or crash consistency. A corrupt or hostile
  disk image can make file reads return arbitrary data, and a partial write can
  lose the file table.
- ZFS has no permissions, ownership or path validation beyond a 32-byte name;
  any kernel caller can read, write or remove any file on the disk.
- ATA polling has no timeout recovery protocol beyond status polling, so a
  wedged device can stall the caller.
- The desktop trusts global input state: any process that can emit mouse or
  keyboard events can trigger clicks, drags, window closes and the taskbar exit
  control.
- Syscalls 25 and 26 let any process request a desktop session change; the
  request is honored after the current shell command returns, with no privilege
  check and no session ownership.
- The desktop renders into kernel-owned framebuffer memory and reads the whole
  screen; there is no per-window capability model, damage tracking or clipping
  against other clients.

This is not yet a complete process security boundary:

- One CPU, one batch at a time; there is no cross-core isolation to design for
  yet, but also no per-CPU scheduler state.
- New processes start with root credentials, and `kill` does not check whether
  the caller owns the target process. Any process can signal any other process
  in the same batch.
- VFS nodes store no owner or mode bits, so the credential permission matrix is
  not yet applied to file operations; descriptors grant access to whatever the
  path lookup returns.
- Descriptor tables are fixed at 16 entries with no per-process quota, and
  descriptor reference counts are not tracked when the same VFS node is opened
  twice.
- Signal handler and trampoline addresses are user supplied and only the stack
  frame range is validated; a process can register any executable address in
  its own space, which is acceptable for a process-private handler but must be
  revisited for signal forwarding.
- Kernel heap mappings are shared and supervisor-only; the kernel heap is not
  per-process.
- There are no process credentials, file descriptors, sessions, or namespace
  isolation.
- `fork` copies every user page eagerly, so a process can multiply memory usage
  with repeated fork calls up to the scheduler batch limit and physical memory.
- `wait` has no timeout and no signal interruption.
- The process table has 32 fixed slots and no quota or resource accounting.
- Only the built-in ELF image is supported by `exec`.

### Graphics surfaces

The kernel and every process share a high-half direct map at
0xFFFFFFFF80000000 and a supervisor device window at 0xFFFFFFFF40000000. Both
mappings are identical across address spaces and supervisor-only. The kernel
still executes from the low identity window in 0.6.0, so this is not yet a
fully relocated higher-half kernel.

Window surfaces live in the kernel heap and are composited by kernel code. They
are not mapped into any process address space, so a process cannot read another
window buffer directly; it can only observe the composed framebuffer through
`fb_pixel`.

### No executable protection

NX is not enabled. The kernel identity mappings and heap are executable.
Writable user data pages and the user stack are executable by hardware even
though the ELF loader rejects W+X segments and maps text without write
permission. SMEP and SMAP are not enabled, so the kernel does not prevent
itself from executing or accessing user pages. W^X is enforced only at ELF
load time, not by page-table NX bits.

### No kernel hardening

- The kernel has no KASLR.
- Kernel text and rodata are covered by writable large pages.
- There are no stack canaries or guard pages.
- The heap validates magic values but has no redzones, quarantine, or canary
  checks.
- The PMM, VMM, heap, and VFS use global locks without lockdep validation.
- Double faults use one emergency stack, but there is no task-specific double
  fault recovery.
- Kernel exceptions halt the machine.
- There is no crash dump, watchdog, or recovery path.

### Filesystem security

- There are no users, owners, groups, modes, ACLs, or capabilities.
- Any code in ring 0 can read or write any VFS node.
- There are no mount namespaces or per-process root directories.
- Device nodes are ordinary VFS nodes with no access control.
- Ramfs data is lost on reboot and is not checksummed or journaled.
- `/dev/random` uses a deterministic xorshift generator seeded with a constant.
  It is not suitable for cryptography, key generation, or any security decision.

### Boot trust

- The BIOS boot chain is unsigned.
- Stage 1 trusts CHS reads and does not verify a signature.
- The kernel trusts the BootInfo block and E820 data supplied by the loader.
- There is no measured boot, TPM integration, or secure boot support.
- A malicious boot image can replace any part of the system.

### Serial and debug surfaces

- COM1 is a root console. Anyone with access to the serial port gets a shell.
- QEMU monitor access is outside the guest threat model.
- The self-test image powers off through an emulator-specific port.
- The normal image can write fixed ACPI/legacy power ports. Those writes are
  ring-0 only but do not validate chipset identity.

### Concurrency and timing

- The kernel assumes a single CPU.
- There is no preemption-safe read-copy-update mechanism.
- Console output is serialized with a preemption-disable depth, not a full
  sleepable lock.
- Batch user-process execution suspends kernel-thread scheduling, so user
  processes are the only runnable entities while a batch is active.
- Signal delivery happens inside the interrupt-return path with interrupts
  disabled and touches user memory under the target CR3; this is safe on one
  CPU but needs a copy-first design once multiple CPUs run processes.
- VFS node pointers have no reference counts. A caller holding a node pointer
  across a concurrent removal would observe freed state.
- Memory allocation while holding VFS locks can extend interrupt-disabled
  sections.

### User-mode limits

- All processes use fixed user virtual addresses in a 64 KiB window.
- The loader accepts only static page-aligned `ET_EXEC` images. PIE relocation,
  dynamic linking, argv, envp, and auxiliary vectors are not implemented.
- User graphics programs share the whole screen: there is no per-window
  ownership, capability check, or compositor client isolation. Any process can
  draw over every pixel through the framebuffer system calls.
- `fb_pixel` lets a process sample screen content, which would be an
  information leak between windows once multiple applications exist.
- The mouse position, buttons, and cursor are global kernel state. Every
  process observes the same pointer and any process can redraw the cursor
  through framebuffer calls.
- The compositor and window surfaces live in the shared kernel heap and are
  not mapped into any address space; they are kernel-only structures today,
  with no user-facing compositor protocol or per-client validation.
- There is no ASLR, stack protector, RELRO, or dynamic linker.
- There are no file descriptors or user heap.
- User buffers are validated and then consumed with interrupts disabled on a
  single CPU, which currently avoids a user/kernel time-of-check race. An SMP
  implementation must copy-first or pin the pages.

## Threat model

The current model assumes:

- A trusted operator controls the boot image, QEMU configuration, serial port,
  and host.
- Ring-3 code is not yet a meaningful security boundary for sensitive data.
- There is no network, disk, or multi-user access.
- Availability attacks by ring-0 code or by the operator are out of scope.

Within that model, the ring transition, per-process user/supervisor page bits,
ELF validation, syscall pointer validation in the process address space,
descriptor table isolation across fork, credential permission checks, signal
frame validation and CR3 switching, blocking wait restart, ATA range checks,
ZFS superblock validation, e1000 DMA buffer ownership, IPv4 header validation,
TCP checksum and sequence validation, ARP, UDP and DNS bounds checks, mouse
packet decoding and bounds clamping, text pointer save and restore, compositor
layering, desktop hit testing, process reaping, and exception containment are
implemented and tested.

## Hardening priorities

1. TCP hardening: SYN backlog limits, randomized initial sequence numbers from
   an entropy source, per-process socket ownership and socket close on exit.
2. DNS response validation by source address and a bounded DNS cache with
   negative caching disabled by default.
3. Network input validation hardening: ARP cache limits with aging, TTL
   handling, fragment rejection policy and frame queue accounting.
4. ZFS checksums, atomic metadata updates and a repair tool, plus filesystem
   permissions and path validation.
4. VFS owner and mode storage so credentials actually gate file access, plus
   a non-root default identity for spawned processes.
5. A user-space display client model with clipping, damage and per-window
   capabilities instead of a kernel-owned desktop.
2. Ownership checks for `kill`, sessions, process groups, and job control.
6. NX, W^X, SMEP, SMAP, and UMIP.
7. Stack guard pages and a slab allocator with redzones.
8. Descriptor reference counts and per-process quotas.
9. Copy-on-write fork and per-process memory accounting.
10. Screen read policy and an opt-in input focus model for the desktop.
11. Cryptographic random source from hardware or a seeded entropy pool.
12. ACPI table parsing instead of fixed power ports.
13. Signed boot and measured boot.
14. Syscall fuzzing, scheduler stress tests, and a kernel sanitizer build.
15. KASLR and ASLR.

## Reporting

This project has no production security response process. Do not deploy it in
an environment where compromise of the machine or its data matters. Security
issues in the prototype should be documented as issues with a reproducible
QEMU configuration and a serial log.
