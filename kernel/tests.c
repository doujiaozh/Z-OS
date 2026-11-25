#include "tests.h"
#include "console.h"
#include "elf.h"
#include "heap.h"
#include "input.h"
#include "io.h"
#include "kernel.h"
#include "lib.h"
#include "pmm.h"
#include "power.h"
#include "process.h"
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

static int test_display_initialize(struct display_device *device) {
    (void)device;
    return 0;
}

static int test_display_set_mode(struct display_device *device, const struct framebuffer *framebuffer) {
    (void)device;
    (void)framebuffer;
    return 0;
}

static int test_display_present(struct display_device *device, u32 x, u32 y, u32 width, u32 height) {
    (void)device;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    return 0;
}

static void test_display_shutdown(struct display_device *device) {
    (void)device;
}

static const struct display_operations test_display_operations = {
    "test-display",
    test_display_initialize,
    test_display_set_mode,
    test_display_present,
    test_display_shutdown
};

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

static void test_elf(void) {
    u64 image_size = (u64)(user_program_end - user_program_start);
    u64 entry = 0;
    u32 segments = 0;
    int result = elf_validate(user_program_start, image_size, &entry, &segments);
    check(result == ELF_OK, "elf validates embedded user image");
    check(result == ELF_OK && entry >= ELF_USER_BASE && entry < ELF_USER_LIMIT && segments >= 2,
          "elf entry point and segments are valid");
    u8 bad_header[sizeof(struct elf64_header)];
    memcpy(bad_header, user_program_start, sizeof(bad_header));
    bad_header[0] = 0;
    check(elf_validate(bad_header, sizeof(bad_header), NULL, NULL) == ELF_ERROR_INVALID,
          "elf rejects bad magic");
    check(elf_validate(user_program_start, sizeof(struct elf64_header) - 1, NULL, NULL) == ELF_ERROR_INVALID,
          "elf rejects truncated header");
    u8 *mutated = kmalloc((usize)image_size);
    if (mutated == NULL) {
        check(false, "elf rejects writable executable segment");
        check(false, "elf rejects out-of-range segment");
        return;
    }
    memcpy(mutated, user_program_start, (usize)image_size);
    struct elf64_header *mutated_header = (struct elf64_header *)mutated;
    struct elf64_program_header *mutated_program =
        (struct elf64_program_header *)(mutated + mutated_header->program_offset);
    mutated_program->flags = ELF_FLAG_READ | ELF_FLAG_WRITE | ELF_FLAG_EXECUTE;
    check(elf_validate(mutated, image_size, NULL, NULL) == ELF_ERROR_FLAGS,
          "elf rejects writable executable segment");
    mutated_program->flags = ELF_FLAG_READ | ELF_FLAG_EXECUTE;
    mutated_program->virtual_address = 0x50000000ULL;
    check(elf_validate(mutated, image_size, NULL, NULL) == ELF_ERROR_RANGE,
          "elf rejects out-of-range segment");
    kfree(mutated);
}

static void test_processes(void) {
    u64 image_size = (u64)(user_program_end - user_program_start);
    struct process *first = process_create("process-a", 0, user_program_start, image_size);
    struct process *second = process_create("process-b", 0, user_program_start, image_size);
    check(first != NULL && second != NULL, "process table creates independent processes");
    if (first == NULL || second == NULL) {
        return;
    }
    check(first->pid != second->pid && first->parent_pid == 0 && second->parent_pid == 0,
          "process pids and parents are valid");
    check(first->space.pml4_phys != second->space.pml4_phys,
          "processes have distinct pml4 roots");
    u64 first_code = vmm_user_physical(&first->space, ELF_USER_BASE);
    u64 second_code = vmm_user_physical(&second->space, ELF_USER_BASE);
    check(first_code != 0 && second_code != 0 && first_code != second_code,
          "same ELF maps to distinct physical frames");
    u64 first_high = process_high_half_entry(first);
    u64 second_high = process_high_half_entry(second);
    check(first_high == second_high && first_high == vmm_high_pml4_entry(),
          "kernel high-half mapping is identical across processes");
    check(vmm_get_physical(KERNEL_HIGH_BASE + 0x100000) == 0x100000,
          "kernel high-half alias resolves the kernel image");
    u64 scratch_frame = pmm_alloc_frame();
    check(scratch_frame != 0 && vmm_map_user_page(&first->space, 0x40080000, scratch_frame,
                                                 PTE_PRESENT | PTE_WRITABLE | PTE_USER) == 0,
          "process address space maps a user page");
    check(vmm_user_physical(&first->space, 0x40080000) == scratch_frame,
          "process address space reports its user frame");
    check(vmm_user_physical(&second->space, 0x40080000) == 0,
          "user mappings are invisible across address spaces");
    u64 first_pid = first->pid;
    u64 second_pid = second->pid;
    u64 first_exit = 0;
    u64 second_exit = 0;
    int first_result = process_run(first_pid, &first_exit);
    int second_result = process_run(second_pid, &second_exit);
    check(first_result == 0 && first_exit == 0, "first process runs ELF in its own address space");
    check(second_result == 0 && second_exit == 0, "second process runs ELF in its own address space");
    u64 status = 0;
    int waited = process_wait(0, first_pid, &status);
    check(waited == (int)first_pid && status == 0, "wait reaps the first process");
    waited = process_wait(0, second_pid, &status);
    check(waited == (int)second_pid && status == 0, "wait reaps the second process");
    struct process *parent = process_create("process-parent", 0, user_program_start, image_size);
    check(parent != NULL, "process table creates a fork parent");
    if (parent == NULL) {
        return;
    }
    u64 child_pid = 0;
    int fork_result = process_fork(parent->pid, &child_pid);
    struct process *child = process_find(child_pid);
    check(fork_result == PROCESS_ERROR_NONE && child != NULL && child->parent_pid == parent->pid,
          "fork creates a child with the correct parent");
    if (child == NULL) {
        process_exit(parent->pid, 0);
        process_wait(0, parent->pid, &status);
        return;
    }
    check(child->space.pml4_phys != parent->space.pml4_phys && child->entry == parent->entry,
          "fork child has an independent address space");
    check(process_exec(child_pid, user_program_start, image_size) == PROCESS_ERROR_NONE,
          "exec replaces a process image");
    check(process_exit(child_pid, 0x33) == PROCESS_ERROR_NONE,
          "process exit records a status");
    status = 0;
    waited = process_wait(parent->pid, child_pid, &status);
    check(waited == (int)child_pid && status == 0x33,
          "wait returns the child exit status");
    process_exit(parent->pid, 0);
    process_wait(0, parent->pid, &status);
    check(process_count() == 0, "process table drains after reaping");
}

static void test_display(void) {
    static struct display_device device;
    memset(&device, 0, sizeof(device));
    strncpy(device.name, "test-display", sizeof(device.name) - 1);
    device.operations = &test_display_operations;
    device.framebuffer.address = 0xFD000000ULL;
    device.framebuffer.size = 1024 * 768 * 4;
    device.framebuffer.width = 1024;
    device.framebuffer.height = 768;
    device.framebuffer.pitch = 4096;
    device.framebuffer.bits_per_pixel = 32;
    device.framebuffer.red_size = 8;
    device.framebuffer.red_position = 16;
    device.framebuffer.green_size = 8;
    device.framebuffer.green_position = 8;
    device.framebuffer.blue_size = 8;
    device.framebuffer.blue_position = 0;
    device.framebuffer.format = FRAMEBUFFER_FORMAT_RGB;
    check(display_register(&device) == 0, "display registers a framebuffer device");
    check(display_find("test-display") == &device, "display lookup finds the device");
    check(display_primary() == &device && display_count() == 1, "display primary and count are valid");
    check(display_unregister(&device) == 0 && display_count() == 0, "display unregisters cleanly");
}

void selftest_thread(void *argument) {
    (void)argument;
    kprintf("ZTEST BEGIN\n");
    test_memory();
    test_vfs();
    test_formatting();
    test_time();
    test_threads();
    test_elf();
    test_user_mode();
    test_processes();
    test_display();
    kprintf("ZTEST SUMMARY passed=%u failed=%u\n", tests_passed, tests_failed);
    if (tests_failed == 0) {
        kprintf("ZTEST RESULT PASS\n");
    } else {
        kprintf("ZTEST RESULT FAIL\n");
    }
    sched_sleep_ms(200);
    qemu_debug_exit(tests_failed == 0 ? 0x10 : 0x11);
}
