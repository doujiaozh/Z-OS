#include "tests.h"
#include "console.h"
#include "heap.h"
#include "input.h"
#include "io.h"
#include "kernel.h"
#include "lib.h"
#include "pmm.h"
#include "power.h"
#include "rtc.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "user.h"
#include "vfs.h"
#include "vmm.h"

static u32 tests_passed = 0;
static u32 tests_failed = 0;

static volatile u64 test_worker_counter = 0;
static volatile u32 test_workers_done = 0;

static void check(bool condition, const char *name) {
    if (condition) {
        tests_passed++;
        kprintf("[PASS] %s\n", name);
    } else {
        tests_failed++;
        kprintf("[FAIL] %s\n", name);
    }
}

static void test_worker(void *argument) {
    u64 iterations = (u64)argument;
    for (u64 index = 0; index < iterations; index++) {
        __atomic_add_fetch(&test_worker_counter, 1, __ATOMIC_RELAXED);
        if ((index & 127) == 0) {
            sched_yield();
        }
    }
    __atomic_add_fetch(&test_workers_done, 1, __ATOMIC_RELEASE);
}

static void test_memory(void) {
    struct pmm_stats before = pmm_get_stats();
    u64 frames[8];
    bool frames_valid = true;
    for (u32 index = 0; index < 8; index++) {
        frames[index] = pmm_alloc_frame();
        if (frames[index] == 0 || (frames[index] & (PAGE_SIZE - 1)) != 0) {
            frames_valid = false;
        }
        for (u32 other = 0; other < index; other++) {
            if (frames[other] == frames[index]) {
                frames_valid = false;
            }
        }
    }
    check(frames_valid, "pmm allocates distinct aligned frames");
    for (u32 index = 0; index < 8; index++) {
        if (frames[index] != 0) {
            pmm_free_frame(frames[index]);
        }
    }
    struct pmm_stats after = pmm_get_stats();
    check(after.free_frames == before.free_frames, "pmm free restores free frame count");

    u64 contiguous = pmm_alloc_contiguous(4);
    check(contiguous != 0 && (contiguous & (PAGE_SIZE - 1)) == 0, "pmm contiguous allocation");
    if (contiguous != 0) {
        pmm_free_contiguous(contiguous, 4);
    }
    check(pmm_get_stats().free_frames == before.free_frames, "pmm contiguous free restores count");

    struct heap_stats heap_before = heap_get_stats();
    void *small = kmalloc(24);
    void *page = kmalloc(4096);
    void *large = kmalloc(200000);
    bool pointers_valid = small != NULL && page != NULL && large != NULL;
    check(pointers_valid, "heap allocates small, page, and large blocks");
    if (pointers_valid) {
        memset(small, 0x11, 24);
        memset(page, 0x22, 4096);
        memset(large, 0x33, 200000);
        check(((u8 *)small)[23] == 0x11 && ((u8 *)page)[4095] == 0x22 && ((u8 *)large)[199999] == 0x33,
              "heap allocations are writable and isolated");
        page = krealloc(page, 8192);
        check(page != NULL && ((u8 *)page)[4095] == 0x22, "heap realloc preserves content");
        kfree(small);
        kfree(page);
        kfree(large);
        struct heap_stats heap_after = heap_get_stats();
        check(heap_after.used_bytes == heap_before.used_bytes, "heap free returns to baseline");
        check(heap_after.allocation_count >= heap_before.allocation_count + 4, "heap allocation counter advances");
    } else {
        kfree(small);
        kfree(page);
        kfree(large);
    }

    u64 frame = pmm_alloc_frame();
    check(frame != 0, "vmm test frame allocated");
    if (frame != 0) {
        bool mapped = vmm_map_page(0x70000000, frame, PTE_PRESENT | PTE_WRITABLE) == 0;
        check(mapped, "vmm maps a virtual page");
        if (mapped) {
            *(volatile u64 *)0x70000000 = 0x1122334455667788ULL;
            check(*(volatile u64 *)0x70000000 == 0x1122334455667788ULL, "vmm mapped page is accessible");
            check(vmm_get_physical(0x70000000) == frame, "vmm translates virtual to physical");
            check(vmm_unmap_page(0x70000000) == 0, "vmm unmaps a virtual page");
            check(!vmm_is_mapped(0x70000000), "vmm reports unmapped page");
        }
        pmm_free_frame(frame);
    }
}

static void test_vfs(void) {
    struct vfs_node *root = vfs_root();
    vfs_remove(root, "/tmp/ztest");
    vfs_remove(root, "/tmp/ztest-renamed");
    vfs_remove(root, "/tmp/ztest-dir");
    check(vfs_mkdir(root, "/tmp/ztest-dir") == VFS_OK, "vfs creates a directory");
    check(vfs_remove(root, "/tmp/ztest-dir") == VFS_OK, "vfs removes an empty directory");
    check(vfs_create(root, "/tmp/ztest") == VFS_OK, "vfs creates a file");
    struct vfs_node *node = vfs_lookup(root, "/tmp/ztest");
    check(node != NULL, "vfs resolves a created path");
    if (node != NULL) {
        check(vfs_write(node, 0, "hello", 5) == 5, "vfs writes file data");
        char buffer[32];
        check(vfs_read(node, 0, buffer, 5) == 5 && memcmp(buffer, "hello", 5) == 0, "vfs reads file data");
        check(vfs_append(node, " world", 6) == 6, "vfs appends file data");
        struct vfs_stat stat;
        check(vfs_stat(node, &stat) == VFS_OK && stat.size == 11, "vfs reports file size");
        check(vfs_rename(root, "/tmp/ztest", "/tmp/ztest-renamed") == VFS_OK, "vfs renames a file");
        check(vfs_lookup(root, "/tmp/ztest") == NULL, "vfs old path is gone after rename");
        node = vfs_lookup(root, "/tmp/ztest-renamed");
        check(node != NULL, "vfs resolves renamed path");
        if (node != NULL) {
            check(vfs_read(node, 6, buffer, 5) == 5 && memcmp(buffer, "world", 5) == 0, "vfs renamed file content");
        }
        check(vfs_remove(root, "/tmp/ztest-renamed") == VFS_OK, "vfs removes a file");
    }
    struct vfs_node *null_device = vfs_lookup(root, "/dev/null");
    check(null_device != NULL, "vfs exposes /dev/null");
    if (null_device != NULL) {
        check(vfs_write(null_device, 0, "discard", 7) == 7, "device write accepts data");
        check(vfs_read(null_device, 0, (char[8]){0}, 1) == 0, "device read returns end of file");
    }
}

static void test_formatting(void) {
    char buffer[128];
    int length = ksnprintf(buffer, sizeof(buffer), "%s %d %d %x %02x %llx",
                           "Z", 42, -7, 0x2A, 0xF, 0x123456789ABCDEF0ULL);
    check(length > 0 && strcmp(buffer, "Z 42 -7 2a 0f 123456789abcdef0") == 0, "formatted output");
    char small[8];
    ksnprintf(small, sizeof(small), "%s", "truncated");
    check(strcmp(small, "truncat") == 0, "formatted output truncates safely");
    check(strcmp(trim((char[]){' ', ' ', 'x', ' ', 0}), "x") == 0, "string trim");
}

static void test_time(void) {
    struct rtc_time time;
    rtc_read(&time);
    check(time.year >= 2020 && time.year <= 2100, "rtc year is sane");
    check(time.month >= 1 && time.month <= 12, "rtc month is sane");
    check(time.day >= 1 && time.day <= 31, "rtc day is sane");
    check(time.hour < 24 && time.minute < 60 && time.second < 60, "rtc time is sane");
    u64 before = timer_uptime_ms();
    sched_sleep_ms(120);
    u64 after = timer_uptime_ms();
    check(after >= before + 80, "timer advances across sleep");
}

static void test_threads(void) {
    test_worker_counter = 0;
    test_workers_done = 0;
    u64 first = sched_create_thread("test-worker-1", test_worker, (void *)10000);
    u64 second = sched_create_thread("test-worker-2", test_worker, (void *)10000);
    u64 third = sched_create_thread("test-worker-3", test_worker, (void *)10000);
    check(first != 0 && second != 0 && third != 0, "scheduler creates kernel threads");
    if (first != 0 && second != 0 && third != 0) {
        u64 deadline = timer_uptime_ms() + 5000;
        while (__atomic_load_n(&test_workers_done, __ATOMIC_ACQUIRE) < 3 && timer_uptime_ms() < deadline) {
            sched_sleep_ms(10);
        }
        check(__atomic_load_n(&test_workers_done, __ATOMIC_ACQUIRE) == 3, "all worker threads complete");
        check(__atomic_load_n(&test_worker_counter, __ATOMIC_ACQUIRE) == 30000, "atomic worker counter is correct");
    }
}

static void test_user_mode(void) {
    u64 exit_code = 0;
    int result = user_run_demo(&exit_code);
    check(result == 0 && exit_code == 0, "ring-3 user program exits through syscall");
}

void selftest_thread(void *argument) {
    (void)argument;
    kprintf("ZTEST BEGIN\n");
    test_memory();
    test_vfs();
    test_formatting();
    test_time();
    test_threads();
    test_user_mode();
    kprintf("ZTEST SUMMARY passed=%u failed=%u\n", tests_passed, tests_failed);
    if (tests_failed == 0) {
        kprintf("ZTEST RESULT PASS\n");
    } else {
        kprintf("ZTEST RESULT FAIL\n");
    }
    sched_sleep_ms(200);
    qemu_debug_exit(tests_failed == 0 ? 0x10 : 0x11);
}
