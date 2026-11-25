typedef unsigned long long u64;
typedef long long i64;
typedef unsigned int u32;
typedef unsigned char u8;

struct fb_user_info {
    u64 physical;
    u32 width;
    u32 height;
    u32 pitch;
    u32 bits_per_pixel;
    u32 format;
    u32 active;
};

static inline long zsys3(long number, long first, long second, long third) {
    __asm__ volatile("int $0x80"
                     : "+a"(number)
                     : "D"(first), "S"(second), "d"(third)
                     : "rcx", "r11", "memory");
    return number;
}

static inline long zsys5(long number, long first, long second, long third, long fourth, long fifth) {
    register long argument4 __asm__("r10") = fourth;
    register long argument5 __asm__("r8") = fifth;
    __asm__ volatile("int $0x80"
                     : "+a"(number)
                     : "D"(first), "S"(second), "d"(third), "r"(argument4), "r"(argument5)
                     : "rcx", "r11", "memory");
    return number;
}

static inline u64 zstrlen(const char *text) {
    u64 length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

static inline long zwrite(const char *text) {
    return zsys3(1, 1, (long)text, (long)zstrlen(text));
}

static inline long zmark(long marker) {
    return zsys3(12, marker, 0, 0);
}

static inline long zfork(void) {
    return zsys3(6, 0, 0, 0);
}

static inline long zwait(long pid, long status_address) {
    return zsys3(8, pid, status_address, 0);
}

static inline long zgetpid(void) {
    return zsys3(3, 0, 0, 0);
}

static inline long zyield(void) {
    return zsys3(5, 0, 0, 0);
}

static inline long zfb_info(struct fb_user_info *info) {
    return zsys3(9, (long)info, 0, 0);
}

static inline long zfb_rect(long x, long y, long width, long height, long color) {
    return zsys5(10, x, y, width, height, color);
}

static inline long zfb_pixel(long x, long y) {
    return zsys3(11, x, y, 0);
}

static __attribute__((noreturn)) void zexit(long status) {
    zsys3(2, status, 0, 0);
    for (;;) {
        __asm__ volatile("pause");
    }
}
