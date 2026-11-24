BITS 64
ORG 0x40000000

_start:
    mov rax, 1
    mov rdi, 1
    lea rsi, [rel message]
    mov rdx, message_end - message
    int 0x80
    cmp rax, message_end - message
    jne .fail

    mov rax, 1
    mov rdi, 1
    lea rsi, [rel second]
    mov rdx, second_end - second
    int 0x80

    mov rax, 2
    xor rdi, rdi
    int 0x80

.fail:
    mov rax, 2
    mov rdi, 7
    int 0x80

.hang:
    jmp .hang

message db "Hello from Z-OS ring 3", 10
message_end:
second db "User syscall return path verified", 10
second_end:
