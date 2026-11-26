typedef unsigned long long u64;
typedef long long i64;
typedef int i32;
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

struct mouse_user_state {
    i32 x;
    i32 y;
    u32 buttons;
    u32 present;
};

#define ZO_SIGINT 2
#define ZO_SIGUSR1 10
#define ZO_SIGUSR2 12
#define ZO_SIGTERM 15

#define ZO_EINTR 4
#define ZO_ECHILD 10
#define ZO_EAGAIN 11

#define ZO_OPEN_READ 1
#define ZO_OPEN_WRITE 2
#define ZO_SIG_BLOCK 1
#define ZO_SIG_UNBLOCK 2
#define ZO_SIG_SET 3

#define ZO_SYS_WRITE 1
#define ZO_SYS_FORK 6
#define ZO_SYS_WAIT 8
#define ZO_SYS_SIGNAL 18
#define ZO_SYS_KILL 19
#define ZO_SYS_SIGRETURN 20
#define ZO_SYS_SIGPROCMASK 21
#define ZO_SYS_GUI_ENTER 25
#define ZO_SYS_GUI_EXIT 26
#define ZO_SYS_SOCKET 27
#define ZO_SYS_BIND 28
#define ZO_SYS_SENDTO 29
#define ZO_SYS_RECVFROM 30
#define ZO_SYS_SOCKET_CLOSE 31
#define ZO_SYS_CONNECT 32
#define ZO_SYS_ACCEPT 33
#define ZO_SYS_RECV 34
#define ZO_SYS_SEND 35
#define ZO_SYS_DNS 36
#define ZO_SYS_GET_TEST_PORT 37

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
    return zsys3(ZO_SYS_WRITE, 1, (long)text, (long)zstrlen(text));
}

static inline long zmark(long marker) {
    return zsys3(12, marker, 0, 0);
}

static inline long zfork(void) {
    return zsys3(ZO_SYS_FORK, 0, 0, 0);
}

static inline long zwait(long pid, long status_address) {
    return zsys3(ZO_SYS_WAIT, pid, status_address, 0);
}

static inline long zwaitpid(long pid, long status_address, long timeout_ms) {
    return zsys3(ZO_SYS_WAIT, pid, status_address, timeout_ms);
}

static inline long zgetpid(void) {
    return zsys3(3, 0, 0, 0);
}

static inline long zyield(void) {
    return zsys3(5, 0, 0, 0);
}

static inline long zopen(const char *path, long flags) {
    return zsys3(13, (long)path, flags, 0);
}

static inline long zclose(long descriptor) {
    return zsys3(14, descriptor, 0, 0);
}

static inline long zread(long descriptor, void *buffer, long length) {
    return zsys3(15, descriptor, (long)buffer, length);
}

static inline long zwrite_fd(long descriptor, const void *buffer, long length) {
    return zsys3(ZO_SYS_WRITE, descriptor, (long)buffer, length);
}

static inline long zdup(long descriptor) {
    return zsys3(22, descriptor, 0, 0);
}

static inline long zgetuid(void) {
    return zsys3(16, 0, 0, 0);
}

static inline long zgeteuid(void) {
    return zsys3(17, 0, 0, 0);
}

__attribute__((naked, unused, noreturn)) static void zsig_trampoline(void) {
    __asm__ volatile("movq %rsp, %rdi\n\tmovq $20, %rax\n\tint $0x80\n\tud2");
}

static inline long zsignal(long signum, void (*handler)(long)) {
    return zsys3(ZO_SYS_SIGNAL, signum, (long)handler, (long)zsig_trampoline);
}

static inline long zkill(long pid, long signum) {
    return zsys3(ZO_SYS_KILL, pid, signum, 0);
}

static inline long zsigprocmask(long mask, long operation) {
    return zsys3(ZO_SYS_SIGPROCMASK, mask, operation, 0);
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

static inline long zfb_text(long x, long y, long color, const char *text) {
    return zsys5(23, x, y, color, (long)text, 0);
}

static inline long zgui_enter(void)
{
    return zsys3(ZO_SYS_GUI_ENTER, 0, 0, 0);
}

static inline long zgui_exit(void)
{
    return zsys3(ZO_SYS_GUI_EXIT, 0, 0, 0);
}

static inline long zsocket(void)
{
    return zsys3(ZO_SYS_SOCKET, 2, 2, 0);
}

static inline long zsocket_stream(void)
{
    return zsys3(ZO_SYS_SOCKET, 2, 1, 0);
}

static inline long zbind(long descriptor, long port)
{
    return zsys3(ZO_SYS_BIND, descriptor, port, 0);
}

static inline long zsendto(long descriptor, long address, long port, const void *data, long length)
{
    return zsys5(ZO_SYS_SENDTO, descriptor, address, port, length, (long)data);
}

static inline long zrecvfrom(long descriptor, void *buffer, long capacity, long timeout_ms)
{
    return zsys5(ZO_SYS_RECVFROM, descriptor, (long)buffer, capacity, timeout_ms, 0);
}

static inline long zsocket_close(long descriptor)
{
    return zsys3(ZO_SYS_SOCKET_CLOSE, descriptor, 0, 0);
}

static inline long zconnect(long descriptor, long address, long port)
{
    return zsys3(ZO_SYS_CONNECT, descriptor, address, port);
}

static inline long zaccept(long descriptor, long timeout_ms)
{
    return zsys3(ZO_SYS_ACCEPT, descriptor, timeout_ms, 0);
}

static inline long zrecv_tcp(long descriptor, void *buffer, long capacity, long timeout_ms)
{
    return zsys5(ZO_SYS_RECV, descriptor, (long)buffer, capacity, timeout_ms, 0);
}

static inline long zsend_tcp(long descriptor, const void *data, long length)
{
    return zsys3(ZO_SYS_SEND, descriptor, (long)data, length);
}

static inline long zdns(const char *name, unsigned long *address)
{
    return zsys3(ZO_SYS_DNS, (long)name, (long)address, 0);
}

static inline long zgettestport(void)
{
    return zsys3(ZO_SYS_GET_TEST_PORT, 0, 0, 0);
}

static inline long zmemeq(const void *first, const void *second, unsigned long length)
{
    const unsigned char *left = (const unsigned char *)first;
    const unsigned char *right = (const unsigned char *)second;
    for (unsigned long index = 0; index < length; index++) {
        if (left[index] != right[index]) {
            return 0;
        }
    }
    return 1;
}

static inline long zmouse(struct mouse_user_state *state) {
    return zsys3(24, (long)state, 0, 0);
}

static __attribute__((noreturn)) void zexit(long status) {
    zsys3(2, status, 0, 0);
    for (;;) {
        __asm__ volatile("pause");
    }
}
