# Z-OS Roadmap

## Framing

Windows, Linux, and macOS each contain tens of millions of lines of kernel,
driver, runtime, and user-space code accumulated over decades. Full feature
parity with all three is not a single deliverable and cannot be truthfully
claimed by a small prototype. The useful goal is a staged, testable path from
a real kernel foundation to a general-purpose operating system, with explicit
scope and acceptance criteria at each stage.

Z-OS 0.1.0 is the Foundation stage: it boots, manages memory, runs preemptive
kernel threads, hosts a VFS and shell, enters ring 3, and verifies itself
under QEMU. Everything below is future work unless marked implemented.

## Capability matrix

| Domain | 0.1.0 Foundation | Needed for general purpose |
| --- | --- | --- |
| Boot | BIOS two-stage, E820, long mode | UEFI, GPT, secure boot, ACPI handoff, multiboot protocol |
| CPU | GDT, TSS, IDT, exceptions, PIC, PIT | SMP, local APIC, IOAPIC, HPET, TSC deadline, CPU hotplug |
| Memory | Frame bitmap, 4-level paging, heap | VMA tracking, demand paging, COW, swap, NUMA, slab allocators |
| Processes | One synchronous ring-3 demo | ELF loader, process table, fork/exec/wait, signals, credentials |
| Threads | Preemptive kernel round-robin | User threads, priorities, affinity, futexes, real-time classes |
| VFS | Ramfs, 128 nodes, global lock | Mounts, dcache, inode cache, page cache, permissions, quotas |
| Storage | None | ATA, NVMe, virtio-blk, partitioning, journaling FS, fsck |
| Networking | None | PCI NIC drivers, ARP, IPv4/IPv6, UDP/TCP, sockets, DNS, TLS |
| Graphics | VGA text | Framebuffer, compositor, fonts, mouse, GPU drivers, acceleration |
| Security | Ring 3, user/supervisor pages | NX, SMEP, SMAP, W^X, KASLR, ASLR, capabilities, MAC, audit |
| Power | QEMU-compatible ACPI power request | ACPI parser, S3/S4, thermal, battery, cpufreq, idle governors |
| Devices | PIT, PIC, PS/2, COM1, CMOS | PCI enumeration, interrupt routing, USB, audio, timers, hotplug |
| ABI | Five custom syscalls | POSIX/Linux ABI, libc, PE/Win32, Mach-O, dynamic linking |
| Userland | Kernel shell and one demo | init, coreutils, shell, package manager, installer, GUI |
| Reliability | 43 boot tests and shell checks | Stress, fuzzing, crash dumps, watchdog, recovery, updates |
| Hardware | QEMU PC reference | Broad chipsets, laptops, servers, virtual machines, ARM64 |

## Milestone 0.2: foundation hardening

Goal: make the existing kernel robust and observable before adding major
subsystems.

- Add guard pages around kernel and user stacks.
- Add a kernel symbol table and stack traces to exception reports.
- Add output serialization that does not require disabling preemption on a
  single CPU, in preparation for SMP.
- Add heap redzones, canaries, and allocation tracking.
- Add VFS reference counts so returned nodes cannot be freed under a caller.
- Add a full static analysis and warnings-as-errors build.
- Add a QEMU matrix for 128 MiB, 256 MiB, 512 MiB, and 1 GiB.
- Add deterministic serial automation with a monitor command channel.
- Add a boot deadline watchdog and crash log in a reserved page.
- Add a real ACPI RSDP/FADT path for shutdown instead of fixed ports.
- Add a framebuffer console through VBE so the kernel does not depend on VGA
  text mode on real hardware.

Acceptance criteria:

- All existing tests pass on the QEMU matrix.
- No compiler warnings under the project warning set.
- A deliberate kernel fault produces a register dump and stack trace.
- A deliberate user fault terminates only the user demo.

## Milestone 0.3: kernel completeness

Goal: move from a kernel demo environment to a kernel capable of hosting
independent processes.

- Parse ACPI MADT, FADT, and MCFG tables.
- Program the local APIC, IOAPIC, and HPET; retire the PIC and PIT.
- Bring up application processors and implement per-CPU data.
- Add ticket or queued spinlocks, atomic reference counts, and lockdep-style
  checks.
- Add interrupt-safe per-CPU scheduler queues and work stealing.
- Add a slab allocator with size classes and per-CPU caches.
- Add VMA objects, page-fault-driven demand paging, and copy-on-write.
- Add per-process PML4 trees, kernel high-half mappings, and address-space
  switching.
- Enable NX and W^X. Enable SMEP and SMAP after audit.
- Add KASLR for the kernel image and ASLR for user mappings.
- Add a process table with PID allocation, parentage, credentials, and exit
  status.
- Add an ELF64 loader for static PIE executables.
- Add `fork`, `exec`, `wait`, `exit`, `mmap`, `munmap`, `brk`, and signal
  delivery.
- Add a file-descriptor table and per-process current directory.
- Add a kernel init process that launches a user-space init.

Acceptance criteria:

- The kernel boots on one to four emulated CPUs.
- Two independent user processes cannot read or write each other's memory.
- A faulting process is terminated and reaped without crashing the kernel.
- A static ELF binary can be loaded from ramfs and run.

## Milestone 0.4: POSIX and developer ABI

Goal: make Z-OS usable for porting software.

- Define a stable syscall ABI and versioned syscall table.
- Implement POSIX process, memory, time, signal, and file APIs.
- Add pipes, Unix domain sockets, `poll`, `select`, `epoll`-equivalent, and
  event queues.
- Add a libc with startup code, TLS, thread-local storage, and dynamic linker.
- Add a user-space shell, coreutils, and a package format.
- Add a C and C++ toolchain port or a cross-development workflow.
- Add a FUSE-style user-space filesystem interface.
- Add a compositor client protocol and a window manager.
- Add documentation for every syscall and structure layout.

Acceptance criteria:

- A small POSIX test suite passes.
- A third-party command-line program can be compiled and run.
- The user-space init starts services and a login shell.

## Milestone 0.5: storage and persistence

Goal: durable files, devices, and administration.

- Enumerate PCI devices through configuration space and ACPI MCFG.
- Add ATA PIO and DMA, virtio-blk, and NVMe front ends.
- Implement a block layer with request queues, caching, and barriers.
- Implement a native journaling filesystem with checksums and snapshots.
- Add a FAT32 read path for interoperability and an ext2 read-only path.
- Implement mount points, superblocks, inode and dentry caches, and a page
  cache.
- Add permissions, ownership, ACLs, quotas, and extended attributes.
- Add `fsck`, crash recovery, and a write-ahead log.
- Add partition discovery with MBR and GPT.
- Add an installer image that writes a bootable disk.

Acceptance criteria:

- Files survive reboot and power loss at injected crash points.
- A corrupted filesystem is detected by checksums and repaired by fsck.
- The system boots from a partitioned disk as well as a floppy image.

## Milestone 0.6: networking

Goal: first-class local and Internet connectivity.

- Add PCIe and virtio network drivers.
- Add an interrupt-driven network receive path and a TX queue.
- Implement Ethernet, ARP, IPv4, ICMP, and UDP.
- Implement TCP with congestion control and retransmission.
- Add a socket API integrated with the process file-descriptor table.
- Add DHCP, DNS, routing tables, and network configuration tools.
- Add IPv6 and neighbor discovery.
- Add TLS and a certificate store.
- Add packet capture and traffic statistics.
- Add firewall rules and network namespaces in the security milestone.

Acceptance criteria:

- The guest downloads a file from a host HTTP server.
- Concurrent TCP streams run without data corruption.
- Packet loss and reordering are handled by automated QEMU network tests.

## Milestone 0.7: graphics, input, and audio

Goal: a usable desktop and media foundation.

- Set a linear framebuffer through VBE or UEFI GOP.
- Add a framebuffer console and text renderer independent of VGA.
- Add a 2D compositor with damage tracking and vsync.
- Add mouse, tablet, and USB HID input.
- Add a font and image library.
- Add a window manager, application toolkit, and clipboard.
- Add PCI GPU drivers beginning with QEMU stdvga and virtio-gpu.
- Add audio through Intel HDA or virtio-sound.
- Add video decode only after the base graphics stack is stable.

Acceptance criteria:

- The console and a graphical terminal render on a framebuffer.
- Multiple windows can be moved and redrawn without corruption.
- Input events reach applications with correct coordinates and focus.

## Milestone 0.8: security and isolation

Goal: defend the kernel and isolate untrusted workloads.

- Define a capability model for kernel objects and device access.
- Implement per-process credentials, privilege checks, and setuid transitions.
- Add seccomp-style syscall filtering.
- Add namespaces or containers with isolated VFS, PID, network, and mount views.
- Add W^X enforcement, stack canaries, fortified string operations, and heap
  hardening.
- Add NX, SMEP, SMAP, UMIP, and CET where hardware supports them.
- Add audit logging, immutable boot measurements, and a TPM path.
- Add a signed update and package verification design.
- Add an out-of-tree fuzzing harness for syscalls, VFS paths, and network
  parsers.

Acceptance criteria:

- A malicious user process cannot escape its address space or bypass file
  permissions.
- A fuzzed syscall path does not corrupt kernel state.
- A tampered boot image fails signature verification.

## Milestone 0.9: compatibility layers

Goal: run software from other operating-system families where practical.

Linux ABI:

- Implement the Linux x86_64 syscall numbers, structures, and semantics.
- Provide a compatible ELF loader, vDSO-equivalent, procfs, sysfs, and devfs.
- Run an unmodified static Linux binary, then a dynamically linked one.
- Build a compatibility test corpus.

Windows ABI:

- Implement a PE32+ loader and a Win32/NT subset.
- Map Windows file, process, thread, registry, and window APIs onto Z-OS
  primitives.
- Start with console and file utilities, then graphics applications.

macOS ABI:

- Treat Mach-O, Mach IPC, and the Cocoa frameworks as a separate multi-year
  compatibility project.
- Define the supported subset before implementation.

These layers are not part of the kernel core. Each should be a separately
versioned subsystem with its own tests and threat model. Claiming binary
compatibility requires passing real application tests, not implementing
loader headers.

## Milestone 1.0: supported general-purpose release

Goal: a coherent release that can be installed and used for real work.

- Stable kernel and driver ABI.
- Package repository with signed packages.
- Installer, recovery mode, and online updates.
- User-space init, login, shell, editor, file manager, and browser-quality
  networking stack.
- Administration tools for users, mounts, networking, and updates.
- Documentation for users, developers, and administrators.
- Hardware compatibility list and regression matrix.
- Security response process and release signing.
- Sustainable governance and contribution process.

Acceptance criteria are defined by external users completing real workflows,
not by feature checkboxes. A release is not "all features of Windows, Linux,
and macOS" unless independent application and hardware test suites pass.

## Engineering constraints and risks

- Driver breadth is the largest effort. Every chipset, laptop, and server
  model adds validation work.
- Compatibility layers require sustained conformance testing, not just
  loaders.
- Security claims require adversarial review and fuzzing; ring separation
  alone is not sufficient.
- SMP introduces locking, interrupt, and memory-ordering correctness work
  that cannot be validated by the current single-CPU design.
- The current floppy/DOS-style image is a bootstrap device, not a deployment
  format. UEFI and GPT are prerequisites for broad hardware.
- The codebase needs a coding standard, review process, and CI hardware matrix
  before it can accept broad contributions.

## Immediate next steps

1. Complete 0.2 hardening, especially guard pages, reference counts, and
   ACPI table parsing.
2. Replace the fixed QEMU power ports with an FADT-driven shutdown path.
3. Introduce per-process address spaces and an ELF loader.
4. Add a kernel/user test harness that runs real binaries.
5. Move the console to a framebuffer path for hardware independence.
6. Add PCI enumeration and a persistent disk driver.
7. Build the first user-space init and libc.

Progress should be measured by verified tests and external use cases, not by
the number of named features.
