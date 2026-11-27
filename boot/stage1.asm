BITS 16
ORG 0x7C00

STAGE2_SEG equ 0x0800
STAGE2_SECTORS equ 32

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl
    cmp dl, 0x80
    jb floppy_boot
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc floppy_boot
    cmp bx, 0xAA55
    jne floppy_boot
    test cx, 1
    jz floppy_boot
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc boot_error
    mov dl, [boot_drive]
    jmp STAGE2_SEG:0

floppy_boot:
    mov ax, STAGE2_SEG
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, 17
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [boot_drive]
    int 0x13
    jc boot_error
    mov ax, STAGE2_SEG
    mov es, ax
    mov bx, 17 * 512
    mov ah, 0x02
    mov al, 15
    mov ch, 0
    mov cl, 1
    mov dh, 1
    mov dl, [boot_drive]
    int 0x13
    jc boot_error
    mov dl, [boot_drive]
    jmp STAGE2_SEG:0

boot_error:
    mov si, error_message
    mov ah, 0x0E
    mov bx, 0x0007
.next:
    lodsb
    test al, al
    jz .halt
    int 0x10
    jmp .next
.halt:
    cli
    hlt
    jmp .halt

align 4
dap:
    db 0x10
    db 0
    dw STAGE2_SECTORS
    dw 0
    dw STAGE2_SEG
    dq 1

boot_drive db 0
error_message db "Z-OS stage1 disk error", 13, 10, 0

times 0x1BE - ($ - $$) db 0
db 0x80
db 0, 0, 0
db 0x7F
db 0, 0, 0
dd 2048
dd 1046528
times 510 - ($ - $$) db 0
dw 0xAA55
