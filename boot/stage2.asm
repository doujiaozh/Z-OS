BITS 16
ORG 0x8000

KERNEL_LBA equ 33
KERNEL_SECTORS equ 640
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
    call enable_a20
    call probe_memory
    call load_kernel
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

load_kernel:
    cmp byte [boot_drive], 0x80
    jb .floppy
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .floppy
    cmp bx, 0xAA55
    jne .floppy
    test cx, 1
    jz .floppy
    mov word [sector_index], 0
.lba_loop:
    movzx eax, word [sector_index]
    cmp ax, KERNEL_SECTORS
    jae .done
    mov edx, eax
    shl edx, 9
    add edx, STAGING
    mov ecx, edx
    shr ecx, 4
    and edx, 0x0F
    mov [dap_segment], cx
    mov [dap_offset], dx
    mov eax, KERNEL_LBA
    movzx edx, word [sector_index]
    add eax, edx
    mov dword [dap_lba], eax
    mov dword [dap_lba + 4], 0
    mov eax, KERNEL_SECTORS
    movzx edx, word [sector_index]
    sub eax, edx
    cmp eax, 32
    jbe .count_ready
    mov eax, 32
.count_ready:
    mov [dap_count], ax
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .lba_error
    movzx eax, word [dap_count]
    add [sector_index], ax
    jmp .lba_loop
.lba_error:
    mov si, disk_message
    call print
    jmp start.hang
.floppy:
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

align 4
dap:
    db 0x10
    db 0
dap_count:
    dw 0
dap_offset:
    dw 0
dap_segment:
    dw 0
dap_lba:
    dq 0


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

    mov esi, STAGING
    mov edi, KERNEL_ENTRY
    mov ecx, KERNEL_SECTORS * 512
    rep movsb

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

    call check_long_mode
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
    jz long_mode_error
    mov eax, 0x1000
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000001
    mov cr0, eax
    mov eax, 0x1000
    mov cr3, eax
    jmp 0x18:long_entry
.pae_error:
    mov bl, '7'
    mov esi, long_mode_error_text
    mov ecx, long_mode_error_text_length
    jmp report_error

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
    mov bl, '6'
    mov esi, long_mode_error_text
    mov ecx, long_mode_error_text_length
    jmp report_error

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
    mov eax, [0x3000]
    cmp eax, 0x00000083
    jne .directory_error
    mov eax, [0x3008]
    cmp eax, 0x00200083
    jne .page_error
    ret
.table_error:
    mov bl, '1'
    jmp paging_error
.directory_pointer_error:
    mov bl, '2'
    jmp paging_error
.directory_error:
    mov bl, '3'
    jmp paging_error
.page_error:
    mov bl, '4'
    jmp paging_error

long_mode_error:
    mov bl, '5'
    mov esi, long_mode_error_text
    mov ecx, long_mode_error_text_length
    jmp report_error

paging_error:
    mov esi, paging_error_text
    mov ecx, paging_error_text_length
report_error:
    mov edi, 0xB8000
    mov ah, 0x4F
.copy_text:
    lodsb
    stosw
    loop .copy_text
    mov al, bl
    stosw
    add edi, 2
    mov al, 'H'
    stosw
    mov al, 'A'
    stosw
    mov al, 'L'
    stosw
    mov al, 'T'
    stosw
.halt:
    cli
    hlt
    jmp .halt


BITS 64
long_entry:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000
    xor rbp, rbp
    mov rdi, BOOTINFO
    mov rax, KERNEL_ENTRY
    jmp rax

BITS 16
boot_drive db 0
sector db 0
head db 0
cylinder db 0
retries db 0
sector_index dw 0
e820_count db 0
disk_message db "Z-OS stage2 disk error", 13, 10, 0
paging_error_text db "Z-OS STAGE2 PAGING ERROR "
paging_error_text_length equ $ - paging_error_text
long_mode_error_text db "Z-OS STAGE2 LONG MODE ERROR "
long_mode_error_text_length equ $ - long_mode_error_text

align 8
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

times 32 * 512 - ($ - $$) db 0
