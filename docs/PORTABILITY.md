# Architecture Portability: x86_64 and riscv32

Z-OS keeps every architecture-specific detail behind a single Hardware
Abstraction Layer (HAL). The portable kernel sources (heap, VFS, network stack,
scheduler, formatting library) never touch control registers, gate tables, page
table roots or interrupt controllers directly.

## Layout

```
kernel/include/hal/hal.h       HAL operation table and dispatch prototypes
kernel/include/hal/arch.h      Architecture identifiers
kernel/hal/hal.c               HAL dispatch layer, one indirect call per service
kernel/arch/x86_64/            x86_64 backend (GDT, IDT, CR3, PIC, TSS)
kernel/arch/riscv32/           riscv32 backend (mtvec, SV32, CLINT, UART 16550)
```

The backend is selected at build time. `hal_platform()` returns a
`struct hal_operations` describing the active machine; the dispatch layer turns
each HAL call into one indirect call, which keeps the portable code identical on
both targets.

## HAL surface

| Group | Operations |
| --- | --- |
| Identity | `arch`, `name`, `cpu` |
| Early console | `early_init`, `console_init`, `console_putc`, `console_write`, `console_set_color`, `console_reset_color` |
| Traps | `trap_init`, `cpu_verify` |
| Memory management | `mmu_root`, `mmu_switch`, `mmu_enabled` |
| Interrupts | `irq_enable`, `irq_disable`, `irq_save`, `irq_restore`, `ticks` |
| Execution | `context_switch`, `idle`, `halt` |

Portable callers use the `hal_*` wrappers only. Architecture code that still
needs native instructions lives in `kernel/include/io.h` (x86 port I/O, control
registers, MSRs) and in the riscv32 CSR helpers, and is compiled only for the
matching target.

## x86_64 backend

The x86_64 backend maps the HAL onto the pre-existing kernel facilities:

| HAL operation | x86_64 implementation |
| --- | --- |
| `trap_init` | `gdt_init()` followed by `interrupts_init()` |
| `cpu_verify` | reads `EFER` (0xC0000080) and `CR4` for long mode and PAE |
| `mmu_root` / `mmu_switch` | `read_cr3()` / `write_cr3()` |
| `irq_enable` / `irq_disable` | `sti` / `cli` |
| `irq_save` / `irq_restore` | `pushfq`/`popfq` capture of `IF`, then `sti`/`cli` |
| `context_switch` | `kernel/arch/context.asm` |
| `ticks` | 8254 timer tick counter |
| `halt` | `cli` then `hlt` loop |

`kernel/mm/vmm.c` and `kernel/sched/scheduler.c` now call `hal_mmu_switch`,
`hal_mmu_root` and `hal_context_switch` instead of touching `CR3` or the
assembly symbol directly.

## riscv32 backend

The riscv32 backend implements the same contract on a bare RV32IMA machine
without S-mode:

| HAL operation | riscv32 implementation |
| --- | --- |
| `trap_init` | `mtvec` points at `trap_entry` |
| `cpu_verify` | `satp.MODE == SV32`, `mtvec != 0`, `satp` equals the boot value |
| `mmu_root` / `mmu_switch` | SV32 root table physical address / `satp` write |
| `irq_enable` / `irq_disable` | `mstatus.MIE` set / clear |
| `irq_save` / `irq_restore` | read and restore `mstatus.MIE` |
| `context_switch` | `riscv_context_switch` saves `ra`, `s0`-`s11` and `sp` |
| `ticks` | CLINT timer compare interrupt counter |
| `halt` | `ecall` with `a7 = 93`, the emulator host exit |

## Boot sequence

```
_start                     clear .bss
   |                       set sp to kernel_stack_top
   |                       install mtvec = trap_entry
   |                       sv32_boot_init() builds the page tables
   |                       write satp, enabling SV32
   v
kmain                      probe the HAL, verify paging, run self-tests
```

`sv32_boot_init` builds three levels of tables:

| Index | Mapping | Flags |
| --- | --- | --- |
| 0 | 4 MiB identity megapage covering kernel text, data and BSS | V R W X A D (M-mode only) |
| `0x40000000 >> 22` | user code page table, leaf 0 maps `__utext_start` | V R X U A D |
| `0x80000000 >> 22` | user data page table, leaf 0 and 1 map user RAM and stack | V R W U A D |

The emulator only translates addresses in U-mode, so the identity megapage
keeps machine-mode access direct while the user mappings exercise SV32 for
real. Page tables live in low physical RAM, well clear of the kernel image.

## Trap handling

`trap_entry` switches to a dedicated trap stack, lays down a 144-byte frame
matching `struct trap_frame`, saves `mepc`, `mstatus` and `mcause`, then calls
`trap_dispatch`.

`trap_dispatch` handles three cases:

1. Machine timer interrupt (`mcause = 0x80000007`): re-arms `mtimecmp` and
   increments the tick counter.
2. Expected page fault: records the fault for the self-test.
3. User `ecall` (`mcause = 8`): inspects `a0`/`a7` and records the test stage.

Unexpected traps are counted so the self-test can assert that none occurred.

## User mode

`riscv_user_enter` writes `mepc`, `mstatus.MPP = 00`, the user stack into `sp`
and the data page base into `s0`, then executes `mret`. User code runs from the
`.utext` section mapped at `0x40000000` with U permission; the same page is
also visible in machine mode so the kernel can copy and inspect it.

## Building

```
make build                        # x86_64 boot image (build/z-os.img)
python tools/build.py build --arch riscv   # riscv32 flat binary
make build ARCH=riscv             # same, through the Makefile
make run-riscv                    # build then boot in rv32emu
python tools/verify_arch.py       # check both architectures
```

The riscv toolchain is located through `RISCV_TOOLCHAIN_BIN`, then a known
local path (`D:\Code\.toolchains\riscv32\mingw64\bin`), then `PATH`.

The riscv build writes to `build/riscv/` so it coexists with the x86_64 image
in `build/`. Both targets can be built in either order.
