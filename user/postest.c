#include "userlib.h"

static const char start_message[] = "postest: posix primitives test started\n";
static const char pass_message[] = "postest: fd, credentials, signals, wait verified\n";
static const char fail_message[] = "postest: posix validation failed\n";

static volatile long handler_count;
static volatile long delay_counter;

static void signal_handler(long signum) {
    (void)signum;
    handler_count++;
}

static void delay(u64 iterations) {
    for (u64 index = 0; index < iterations; index++) {
        delay_counter++;
    }
}

static __attribute__((noreturn)) void fail(long code) {
    zwrite(fail_message);
    zmark(0x3F);
    zexit(code);
}

__attribute__((noreturn)) void _start(void) {
    char buffer[8];
    long status = 0;
    long result = 0;
    zwrite(start_message);

    if (zgetuid() != 0 || zgeteuid() != 0) {
        fail(2);
    }
    zmark(0x30);

    long descriptor = zopen("/README", ZO_OPEN_READ);
    if (descriptor < 3) {
        fail(3);
    }
    if (zread(descriptor, buffer, 8) != 8) {
        fail(4);
    }
    if (buffer[0] != 'Z' || buffer[1] != '-' || buffer[2] != 'O' || buffer[3] != 'S') {
        fail(5);
    }
    zmark(0x31);

    long child = zfork();
    if (child < 0) {
        fail(6);
    }
    if (child == 0) {
        if (zread(descriptor, buffer, 4) != 4) {
            zmark(0x3F);
            zexit(7);
        }
        zclose(descriptor);
        if (zread(descriptor, buffer, 4) != -1) {
            zmark(0x3F);
            zexit(8);
        }
        zmark(0x32);
        zexit(0);
    }
    long reaped = zwaitpid(child, (long)&status, 0);
    if (reaped != child || status != 0) {
        fail(9);
    }
    if (zread(descriptor, buffer, 4) != 4) {
        fail(10);
    }
    zmark(0x33);
    zclose(descriptor);
    if (zread(descriptor, buffer, 4) != -1) {
        fail(11);
    }
    zmark(0x34);

    child = zfork();
    if (child < 0) {
        fail(12);
    }
    if (child == 0) {
        delay(20000000);
        zexit(0);
    }
    result = zwaitpid(child, (long)&status, 1);
    if (result != -ZO_EAGAIN) {
        fail(13);
    }
    zmark(0x35);
    reaped = zwaitpid(child, (long)&status, 0);
    if (reaped != child || status != 0) {
        fail(14);
    }

    if (zsignal(ZO_SIGUSR1, signal_handler) != 0) {
        fail(15);
    }
    if (zsignal(ZO_SIGUSR2, signal_handler) != 0) {
        fail(16);
    }
    long self_pid = zgetpid();
    long victim = zfork();
    if (victim < 0) {
        fail(17);
    }
    if (victim == 0) {
        delay(40000000);
        zexit(0);
    }
    long killer = zfork();
    if (killer < 0) {
        fail(18);
    }
    if (killer == 0) {
        delay(400000);
        zkill(self_pid, ZO_SIGUSR1);
        zexit(0);
    }
    result = zwaitpid(victim, (long)&status, 0);
    if (result != -ZO_EINTR) {
        fail(19);
    }
    if (handler_count != 1) {
        fail(20);
    }
    zmark(0x36);

    zsigprocmask(1L << ZO_SIGUSR2, ZO_SIG_BLOCK);
    zkill(self_pid, ZO_SIGUSR2);
    if (handler_count != 1) {
        fail(21);
    }
    zmark(0x37);
    zsigprocmask(1L << ZO_SIGUSR2, ZO_SIG_UNBLOCK);
    zsys3(4, 0, 0, 0);
    if (handler_count != 2) {
        fail(22);
    }
    zmark(0x38);

    reaped = zwaitpid(victim, (long)&status, 0);
    if (reaped != victim || status != 0) {
        fail(23);
    }
    reaped = zwaitpid(killer, (long)&status, 0);
    if (reaped != killer || status != 0) {
        fail(24);
    }
    zwrite(pass_message);
    zexit(0);
}
