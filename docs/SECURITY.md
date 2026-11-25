# Z-OS Security Model

## Scope

This document describes the security properties and known weaknesses of Z-OS
0.3.0 Process. It is deliberately conservative. The system is a development
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
- A ring-3 exception is reported and converts the synchronous process run into
  an error status instead of panicking the kernel.
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

This is not yet a complete process security boundary:

- User processes are executed synchronously and sequentially, not concurrently.
- Kernel heap mappings are shared and supervisor-only; the kernel heap is not
  per-process.
- There are no process credentials, file descriptors, or namespace isolation.
- `fork` clones a ready process image rather than continuing at the caller's
  saved instruction pointer.
- `wait` is nonblocking for syscalls.
- Only the built-in ELF image is supported by `exec`.

### High-half mapping

The kernel and every process share a high-half direct map at
0xFFFFFFFF80000000. This mapping is identical across address spaces and is
supervisor-only. The kernel still executes from the low identity window in
0.3.0, so this is not yet a fully relocated higher-half kernel.

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
- Synchronous user-process execution disables scheduler preemption, so only one
  user process runs at a time.
- VFS node pointers have no reference counts. A caller holding a node pointer
  across a concurrent removal would observe freed state.
- Memory allocation while holding VFS locks can extend interrupt-disabled
  sections.

### User-mode limits

- All processes use fixed user virtual addresses in a 64 KiB window.
- The loader accepts only static page-aligned `ET_EXEC` images. PIE relocation,
  dynamic linking, argv, envp, and auxiliary vectors are not implemented.
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
process reaping, and exception containment are implemented and tested.

## Hardening priorities

1. Preemptive user-process scheduling with per-process kernel and interrupt
   stacks.
2. True fork continuation and blocking wait queues.
3. NX, W^X, SMEP, SMAP, and UMIP.
4. Stack guard pages and a slab allocator with redzones.
5. VFS reference counts and credential checks.
6. Cryptographic random source from hardware or a seeded entropy pool.
7. ACPI table parsing instead of fixed power ports.
8. Signed boot and measured boot.
9. Syscall fuzzing and a kernel sanitizer build.
10. KASLR and ASLR.

## Reporting

This project has no production security response process. Do not deploy it in
an environment where compromise of the machine or its data matters. Security
issues in the prototype should be documented as issues with a reproducible
QEMU configuration and a serial log.
