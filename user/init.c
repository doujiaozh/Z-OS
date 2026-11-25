typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned char u8;

static long syscall3(long number, long first, long second, long third) {
    __asm__ volatile("int $0x80"
                     : "+a"(number)
                     : "D"(first), "S"(second), "d"(third)
                     : "rcx", "r11", "memory");
    return number;
}

static u64 string_length(const char *text) {
    u64 length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

static long user_write(const char *text) {
    return syscall3(1, 1, (long)text, (long)string_length(text));
}

static __attribute__((noreturn)) void user_exit(long status) {
    syscall3(2, status, 0, 0);
    for (;;) {
        __asm__ volatile("pause");
    }
}

static const char hello_message[] = "Hello from Z-OS ring 3\n";
static const char syscall_message[] = "User syscall return path verified\n";
static const char checksum_message[] = "C ELF64 user program checksum verified\n";
static const char bss_message[] = "ELF64 BSS zero check verified\n";
static const char failure_message[] = "ELF64 user program validation failed\n";
static const char payload[] = "Z-OS ELF64 payload";

static volatile u64 writable_probe = 0x1122334455667788ULL;
static volatile u8 zero_probe[32];

static u32 payload_checksum(void) {
    u32 value = 2166136261U;
    for (u32 index = 0; payload[index] != 0; index++) {
        value ^= (u8)payload[index];
        value *= 16777619U;
    }
    return value;
}

static int bss_is_zero(void) {
    for (u32 index = 0; index < sizeof(zero_probe); index++) {
        if (zero_probe[index] != 0) {
            return 0;
        }
    }
    return 1;
}

static int writable_segment_works(void) {
    u64 original = writable_probe;
    writable_probe = original ^ 0x0102030405060708ULL;
    return writable_probe == (original ^ 0x0102030405060708ULL);
}

__attribute__((noreturn)) void _start(void) {
    if (payload_checksum() != 0x5B3CF743U || !bss_is_zero() || !writable_segment_works()) {
        user_write(failure_message);
        user_exit(7);
    }
    if (user_write(hello_message) != (long)(sizeof(hello_message) - 1)) {
        user_exit(8);
    }
    if (user_write(syscall_message) != (long)(sizeof(syscall_message) - 1)) {
        user_exit(9);
    }
    if (user_write(checksum_message) != (long)(sizeof(checksum_message) - 1)) {
        user_exit(10);
    }
    if (user_write(bss_message) != (long)(sizeof(bss_message) - 1)) {
        user_exit(11);
    }
    user_exit(0);
}
