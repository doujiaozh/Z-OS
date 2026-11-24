# Z-OS Security Model

## Scope

This document describes the security properties and known weaknesses of Z-OS
0.1.0 Foundation. It is deliberately conservative. The system is a development
prototype and must not be used to protect sensitive data.

## Implemented boundaries

- The kernel executes in ring 0. The demo program executes in ring 3.
- User code and user stack pages are mapped with the user bit at every page
  table level. Kernel text, data, heap, stacks, and page tables are mapped
  supervisor-only.
- The syscall gate has DPL 3. All other IDT gates have DPL 0.
- The TSS provides a dedicated ring-0 stack for interrupts and syscalls that
  arrive while the CPU is in ring 3.
- Syscall buffer arguments are validated page by page with
  `vmm_user_range_valid` before the kernel copies or prints from them.
- Syscall write lengths are capped at 4096 bytes.
- The user code page is mapped read-only. The user stack is writable and not
  executable by policy, though NX is not enabled.
- The I/O privilege level is zero. Ring-3 code cannot execute `IN`, `OUT`,
  `CLI`, `STI`, `HLT`, or privileged control-register instructions.
- A ring-3 exception is reported and converted into a user-mode failure
  return instead of panicking the kernel.
- Physical frames returned by the allocator are zeroed.
- The boot image is deterministic and the build hash is recorded.
- QEMU's debug-exit port is used only by the self-test image and only by the
  self-test thread.

## Known weaknesses

### Address-space isolation

There is one synchronous user demo. It shares the kernel page tables rather
than owning a per-process address space. Once the demo returns, the user page
table entries remain present. There is no process table, so a future process
model must replace this shared layout before untrusted workloads can run.

### No executable protection

NX is not enabled. The kernel identity mappings and heap are executable.
SMEP and SMAP are not enabled, so the kernel does not prevent itself from
executing or accessing user pages. W^X is not enforced.

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
- VFS node pointers have no reference counts. A caller holding a node pointer
  across a concurrent removal would observe freed state.
- Memory allocation while holding VFS locks can extend interrupt-disabled
  sections.

### User-mode limits

- The embedded demo uses fixed virtual addresses.
- There is no ASLR, stack protector, RELRO, or dynamic linker.
- There are no file descriptors, so a future POSIX layer must add descriptor
  validation and lifetime rules.
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

Within that model, the ring transition, user/supervisor page bits, syscall
pointer validation, and exception containment are implemented and tested.

## Hardening priorities

1. Per-process address spaces and process teardown.
2. NX, W^X, SMEP, SMAP, and UMIP.
3. Stack guard pages and a slab allocator with redzones.
4. VFS reference counts and credential checks.
5. Cryptographic random source from hardware or a seeded entropy pool.
6. ACPI table parsing instead of fixed power ports.
7. Signed boot and measured boot.
8. Syscall fuzzing and a kernel sanitizer build.
9. KASLR and ASLR.
10. A written security response process.

## Reporting

This project has no production security response process. Do not deploy it in
an environment where compromise of the machine or its data matters. Security
issues in the prototype should be documented as issues with a reproducible
QEMU configuration and a serial log.
