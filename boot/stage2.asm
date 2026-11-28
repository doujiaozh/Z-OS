BITS 16
ORG 0x8000

KERNEL_LBA equ 33
KERNEL_SECTORS equ 960
STAGING equ 0x10000
BOOTINFO equ 0x5000
BOOTINFO_MAGIC equ 0x5A4F5331
KERNEL_ENTRY equ 0x100000

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    mov byte [use_ata], 0
    cmp dl, 0x80
    jb .bios_only
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .bios_only
    cmp bx, 0xAA55
    jne .bios_only
    test cx, 1
    jz .bios_only
    mov byte [use_ata], 1
.bios_only:
    call enable_a20
    call probe_memory
    cmp byte [use_ata], 0
    jne .skip_stage
    call load_kernel_bios
.skip_stage:
    mov ax, 0x0003
    int 0x10
    call enter_protected
.hang:
    cli
    hlt
    jmp .hang

enable_a20:
    in al, 0x92
    test al, 0x02
    jnz .done
    or al, 0x02
    and al, 0xFE
    out 0x92, al
.done:
    ret

probe_memory:
    mov dword [BOOTINFO], BOOTINFO_MAGIC
    mov dword [BOOTINFO + 4], 1
    mov dword [BOOTINFO + 8], 0
    mov dword [BOOTINFO + 12], 0
    mov byte [e820_count], 0
    xor ebx, ebx
.loop:
    movzx eax, byte [e820_count]
    imul eax, eax, 24
    add eax, BOOTINFO + 16
    mov di, ax
    xor ax, ax
    mov es, ax
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    inc byte [e820_count]
    cmp byte [e820_count], 32
    jae .done
    test ebx, ebx
    jnz .loop
.done:
    movzx eax, byte [e820_count]
    mov [BOOTINFO + 8], eax
    mov dword [BOOTINFO + 12], 1
    ret

load_kernel_bios:
    mov word [sector_index], 0
.read_one:
    movzx eax, word [sector_index]
    cmp ax, KERNEL_SECTORS
    jae .done
    mov edx, eax
    shl edx, 9
    add edx, STAGING
    mov eax, edx
    shr eax, 4
    mov es, ax
    and edx, 0x0F
    mov bx, dx

    movzx eax, word [sector_index]
    add eax, KERNEL_LBA
    xor edx, edx
    mov ecx, 18
    div ecx
    inc dl
    mov [sector], dl
    xor edx, edx
    mov ecx, 2
    div ecx
    mov [cylinder], al
    mov [head], dl

    mov byte [retries], 3
.try:
    mov ax, 0x0201
    mov ch, [cylinder]
    mov cl, [sector]
    mov dh, [head]
    mov dl, [boot_drive]
    int 0x13
    jnc .next
    xor ah, ah
    mov dl, [boot_drive]
    int 0x13
    dec byte [retries]
    jnz .try
    mov si, disk_message
    call print
    jmp start.hang
.next:
    inc word [sector_index]
    jmp .read_one
.done:
    ret

print:
    mov ah, 0x0E
    mov bx, 0x0007
.next:
    lodsb
    test al, al
    jz .done
    int 0x10
    jmp .next
.done:
    ret

enter_protected:
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_entry

BITS 32
protected_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    cmp byte [use_ata], 0
    jne .ata_path
    mov esi, STAGING
    mov edi, KERNEL_ENTRY
    mov ecx, KERNEL_SECTORS * 512
    rep movsb
    jmp .loaded
.ata_path:
    call ata_load_kernel
.loaded:
    call check_long_mode
    mov edi, 0x1000
    xor eax, eax
    mov ecx, 0x3000 / 4
    rep stosd
    mov dword [0x1000], 0x2003
    mov dword [0x2000], 0x3003
    mov edi, 0x3000
    mov eax, 0x83
    mov ecx, 512
.map:
    mov [edi], eax
    add eax, 0x200000
    add edi, 8
    loop .map

    call validate_paging
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov eax, cr4
    test eax, 0x20
    jz .pae_error
    mov ecx, 0xC0000080
    rdmsr
    bts eax, 8
    wrmsr
    mov ecx, 0xC0000080
    rdmsr
    test eax, 0x100
    jz .long_error
    mov eax, 0x1000
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    mov eax, 0x1000
    mov cr3, eax
    jmp 0x18:long_entry
.pae_error:
    mov al, '7'
    call pm_halt_char
.long_error:
    mov al, '6'
    call pm_halt_char

ata_load_kernel:
    pushad
    mov dword [ata_lba], KERNEL_LBA
    mov dword [ata_target], KERNEL_ENTRY
    mov dword [ata_remaining], KERNEL_SECTORS
.sector_loop:
    cmp dword [ata_remaining], 0
    je .done
    mov edx, 0x1F6
    mov eax, [ata_lba]
    shr eax, 24
    and al, 0x0F
    or al, 0xE0
    out dx, al
    mov dx, 0x3F6
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    mov edx, 0x1F2
    mov al, 1
    out dx, al
    mov eax, [ata_lba]
    mov edx, 0x1F3
    out dx, al
    shr eax, 8
    inc dx
    out dx, al
    shr eax, 8
    inc dx
    out dx, al
    mov dx, 0x1F7
    mov al, 0x20
    out dx, al
.wait:
    in al, dx
    test al, 0x80
    jnz .wait
    test al, 0x21
    jnz .error
    test al, 0x08
    jz .wait
    mov edi, [ata_target]
    mov ecx, 256
    mov dx, 0x1F0
    rep insw
    add dword [ata_target], 512
    inc dword [ata_lba]
    dec dword [ata_remaining]
    jmp .sector_loop
.error:
    mov al, 'A'
    call pm_halt_char
.done:
    popad
    ret

ata_wait:
    mov dx, 0x3F6
    in al, dx
    in al, dx
    in al, dx
    in al, dx
    mov dx, 0x1F7
.wait:
    in al, dx
    test al, 0x80
    jnz .wait
    ret

pm_halt_char:
    push eax
    mov edi, 0xB8000
    mov [edi], al
    mov al, 0x0F
    mov [edi + 1], al
    pop eax
.halt:
    cli
    hlt
    jmp .halt

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .unsupported
    mov eax, 0x80000001
    cpuid
    test edx, 0x20000000
    jz .unsupported
    ret
.unsupported:
    mov al, '6'
    call pm_halt_char

validate_paging:
    mov edi, 0x1000
    test edi, 0x0FFF
    jnz .table_error
    mov edi, 0x2000
    test edi, 0x0FFF
    jnz .directory_pointer_error
    mov edi, 0x3000
    test edi, 0x0FFF
    jnz .directory_error
    mov eax, [0x1000]
    cmp eax, 0x00002003
    jne .table_error
    mov eax, [0x2000]
    cmp eax, 0x00003003
    jne .directory_pointer_error
    ret
.table_error:
    mov al, '4'
    call pm_halt_char
.directory_pointer_error:
    mov al, '5'
    call pm_halt_char
.directory_error:
    mov al, '3'
    call pm_halt_char

BITS 64
long_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rax, 0x90000
    mov rsp, rax
    mov rax, KERNEL_ENTRY
    mov rdi, BOOTINFO
    xor rbp, rbp
    jmp rax

BITS 16
gdt_start:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
    dq 0x00AF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

disk_message db "Z-OS stage2 disk error", 13, 10, 0
boot_drive db 0
use_ata db 0
e820_count db 0
sector_index dw 0
sector db 0
cylinder db 0
head db 0
retries db 0
align 4
ata_lba dd 0
ata_target dd 0
ata_remaining dd 0

times 16384 - ($ - $$) db 0
