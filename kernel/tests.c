#include "tests.h"
#include "ata.h"
#include "compositor.h"
#include "console.h"
#include "cred.h"
#include "desktop.h"
#include "elf.h"
#include "fd.h"
#include "font.h"
#include "heap.h"
#include "input.h"
#include "interrupts.h"
#include "io.h"
#include "kernel.h"
#include "lib.h"
#include "pmm.h"
#include "fb.h"
#include "gfx.h"
#include "power.h"
#include "process.h"
#include "sched.h"
#include "signal.h"
#include "waitq.h"
#include "mouse.h"
#include "rtc.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "user.h"
#include "vfs.h"
#include "vmm.h"
#include "zfs.h"
#include "wm.h"

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

static u64 run_program(const char *name, const u8 *image_start, const u8 *image_end, u64 *status)
{
    u64 image_size = (u64)(image_end - image_start);
    struct process *process = process_create(name, 0, image_start, image_size);
    if (process == NULL) {
        return 0;
    }
    u64 pid = process->pid;
    u32 completed = 0;
    int result = sched_run_batch(&pid, 1, &completed);
    u64 reaped_pid = 0;
    u64 exit_status = 0xFFFFFFFFFFFFFFFFULL;
    process_reap_child(0, pid, &reaped_pid, &exit_status);
    if (result != SCHED_ERROR_NONE || completed != 1 || reaped_pid != pid) {
        if (status != NULL) {
            *status = 0xFFFFFFFFFFFFFFFFULL;
        }
        return pid;
    }
    if (status != NULL) {
        *status = exit_status;
    }
    return pid;
}

static void test_user_mode(void)
{
    u64 status = 0xFFFFFFFFFFFFFFFFULL;
    u64 pid = run_program("user-init", user_program_start, user_program_end, &status);
    check(pid != 0, "user image creates a schedulable process");
    check(status == 0, "user program exits with status zero under the process scheduler");
    check(process_count() == 0, "scheduler run leaves no stale process entries");
}

static void test_address_spaces(void)
{
    u64 image_size = (u64)(user_program_end - user_program_start);
    struct process *first = process_create("space-a", 0, user_program_start, image_size);
    struct process *second = process_create("space-b", 0, user_program_start, image_size);
    check(first != NULL && second != NULL, "address space test creates two processes");
    if (first == NULL || second == NULL) {
        process_reap_zombies();
        return;
    }
    check(first->pid != second->pid && first->parent_pid == 0 && second->parent_pid == 0,
          "process identifiers and parents are valid");
    check(first->space.pml4_phys != second->space.pml4_phys, "processes own distinct pml4 roots");
    u64 first_code = vmm_user_physical(&first->space, ELF_USER_BASE);
    u64 second_code = vmm_user_physical(&second->space, ELF_USER_BASE);
    check(first_code != 0 && second_code != 0 && first_code != second_code,
          "identical elf images map to distinct physical frames");
    u64 first_high = process_high_half_entry(first);
    u64 second_high = process_high_half_entry(second);
    check(first_high == second_high && first_high == vmm_high_pml4_entry(),
          "kernel high-half entry is shared by all processes");
    check(vmm_get_physical(KERNEL_HIGH_BASE + 0x10000) == 0x10000,
          "kernel high-half alias resolves the kernel image");
    u64 scratch_frame = pmm_alloc_frame();
    check(scratch_frame != 0 && vmm_map_user_page(&first->space, 0x40080000, scratch_frame,
                                                 PTE_PRESENT | PTE_WRITABLE | PTE_USER) == 0,
          "process address space maps a user page");
    check(vmm_user_physical(&first->space, 0x40080000) == scratch_frame,
          "process address space reports its own frame");
    check(vmm_user_physical(&second->space, 0x40080000) == 0,
          "user mappings stay invisible across address spaces");
    check(first->kernel_stack_base != 0 && second->kernel_stack_base != 0 &&
          first->kernel_stack_base != second->kernel_stack_base &&
          first->kernel_stack_top != first->kernel_stack_base,
          "each process owns a private kernel stack");
    process_kill(first->pid, 0);
    process_kill(second->pid, 0);
    process_reap_zombies();
    check(process_count() == 0, "address space test reaps both processes");
}

static void test_preemption(void)
{
    sched_mark_reset();
    u64 image_size = (u64)(user_multi_end - user_multi_start);
    struct process *first = process_create("multi-a", 0, user_multi_start, image_size);
    struct process *second = process_create("multi-b", 0, user_multi_start, image_size);
    check(first != NULL && second != NULL, "preemptive scheduler creates two user processes");
    if (first == NULL || second == NULL) {
        process_reap_zombies();
        return;
    }
    check(sched_quantum_ticks() == PROCESS_QUANTUM_TICKS && sched_quantum_ticks() > 0,
          "process time quantum is configured");
    u64 pids[2];
    pids[0] = first->pid;
    pids[1] = second->pid;
    u64 switches_before = sched_preemption_count();
    u32 completed = 0;
    int result = sched_run_batch(pids, 2, &completed);
    check(result == SCHED_ERROR_NONE && completed == 2,
          "two user processes complete under time slicing");
    check(sched_marker_total(0x10) == 24, "both processes executed all time-sliced rounds");
    u64 first_seen = 0;
    bool both_seen = false;
    bool interleaved = false;
    u64 previous_pid = 0;
    u32 mark_total = sched_mark_count();
    for (u32 index = 0; index < mark_total; index++) {
        u64 pid = 0;
        u64 marker = 0;
        if (!sched_mark_get(index, &pid, &marker)) {
            continue;
        }
        if (marker != 0x10) {
            continue;
        }
        if (first_seen == 0) {
            first_seen = pid;
        } else if (pid != first_seen) {
            both_seen = true;
        }
        if (previous_pid != 0 && previous_pid != pid) {
            interleaved = true;
        }
        previous_pid = pid;
    }
    check(both_seen, "both processes appear in the execution trace");
    check(interleaved, "time slicing interleaves both processes");
    check(sched_preemption_count() > switches_before, "timer interrupts preempted running processes");
    check(sched_switch_count_of(first->pid) > 0 && sched_switch_count_of(second->pid) > 0,
          "both processes recorded forced context switches");
    u64 first_status = 0xFFFFFFFFFFFFFFFFULL;
    u64 second_status = 0xFFFFFFFFFFFFFFFFULL;
    process_reap_child(0, first->pid, NULL, &first_status);
    process_reap_child(0, second->pid, NULL, &second_status);
    check(first_status == 0 && second_status == 0, "both sliced processes exited cleanly");
    check(process_count() == 0, "preemptive run reaps every process");
}

static u32 mark_order(u64 marker)
{
    u32 total = sched_mark_count();
    for (u32 index = 0; index < total; index++) {
        u64 pid = 0;
        u64 value = 0;
        if (!sched_mark_get(index, &pid, &value)) {
            continue;
        }
        if (value == marker) {
            return index + 1;
        }
    }
    return 0;
}

static u64 mark_pid(u64 marker)
{
    u32 total = sched_mark_count();
    for (u32 index = 0; index < total; index++) {
        u64 pid = 0;
        u64 value = 0;
        if (!sched_mark_get(index, &pid, &value)) {
            continue;
        }
        if (value == marker) {
            return pid;
        }
    }
    return 0;
}

static void test_fork_wait(void)
{
    sched_mark_reset();
    u64 image_size = (u64)(user_forktest_end - user_forktest_start);
    struct process *process = process_create("forktest", 0, user_forktest_start, image_size);
    check(process != NULL, "fork test program creates a process");
    if (process == NULL) {
        return;
    }
    u64 pid = process->pid;
    u32 completed = 0;
    int result = sched_run_batch(&pid, 1, &completed);
    check(result == SCHED_ERROR_NONE && completed == 1, "fork and wait program completes");
    u64 status = 0xFFFFFFFFFFFFFFFFULL;
    u64 reaped_pid = 0;
    process_reap_child(0, pid, &reaped_pid, &status);
    check(reaped_pid == pid && status == 0, "fork parent exits with the success status");
    u64 parent_pid = mark_pid(0x20);
    u64 child_pid = mark_pid(0x21);
    check(parent_pid != 0 && child_pid != 0 && parent_pid != child_pid,
          "fork child resumes at the parent call point");
    check(sched_marker_total(0x25) > 0, "fork child runs on a private memory copy");
    check(sched_marker_total(0x2F) == 0, "fork child never corrupts the parent memory");
    u32 wait_enter = mark_order(0x23);
    u32 child_done = mark_order(0x22);
    u32 wait_return = mark_order(0x24);
    check(wait_enter != 0 && child_done > wait_enter && wait_return > child_done,
          "blocking wait resumes only after the child exits");
    check(process_count() == 0, "fork tree is fully reaped");
}

static void test_graphics(void)
{
    check(fb_init() == 0, "framebuffer driver detects a pci vga device");
    check(fb_device_found() && fb_pci_address() != 0, "framebuffer pci bar address is valid");
    u32 saved[4];
    u32 written[4];
    u32 readback[4];
    written[0] = 0x11223344U;
    written[1] = 0x55667788U;
    written[2] = 0x99AABBCCU;
    written[3] = 0xDDEEFF11U;
    memset(saved, 0, sizeof(saved));
    memset(readback, 0, sizeof(readback));
    bool vram_ok = false;
    if (fb_read_test_pixels(0x400000, saved, 4) == 0 &&
        fb_write_test_pixels(0x400000, written, 4) == 0 &&
        fb_read_test_pixels(0x400000, readback, 4) == 0) {
        vram_ok = memcmp(readback, written, sizeof(written)) == 0;
        fb_write_test_pixels(0x400000, saved, 4);
    }
    check(vram_ok, "framebuffer vram stores and returns pixel data");

    check(fb_set_mode(800, 600, 32) == 0, "framebuffer driver applies a graphics mode");
    check(fb_mode_active() && fb_current_mode().width == 800 && fb_current_mode().height == 600,
          "framebuffer mode dimensions are applied");
    struct gfx_surface *surface = fb_surface();
    check(surface != NULL && surface->valid && surface->width == 800, "framebuffer exposes a graphics surface");
    fb_fill(0, 0, 800, 600, 0x00102030U);
    fb_fill(64, 64, 96, 64, 0x0000FF00U);
    check(fb_pixel(0, 0) == 0x00102030U && fb_pixel(96, 96) == 0x0000FF00U,
          "framebuffer pixel fill and readback agree");
    check(gfx_count_color(surface, 0, 0, 800, 600, 0x0000FF00U) == 96 * 64,
          "gfx surface fills a rectangle exactly");
    gfx_fill_rect(surface, -16, -16, 32, 32, 0x00112233U);
    check(gfx_get_pixel(surface, 0, 0) == 0x00112233U &&
          gfx_count_color(surface, 0, 0, 16, 16, 0x00112233U) == 256,
          "gfx surface clips drawing to surface bounds");
    check(gfx_checksum(surface, 0, 0, 32, 32) != 0, "gfx surface checksum covers drawn pixels");

    static u32 window_pixels[64 * 64];
    struct gfx_surface target;
    gfx_init_surface(&target, window_pixels, 64, 64, 64, GFX_FORMAT_RGB32);
    gfx_clear(&target, 0);
    wm_init();
    struct wm_window *lower = wm_create(2, 2, 20, 16, 0x0000AA00U);
    check(lower != NULL && wm_count() == 1, "wm creates a window");
    if (lower != NULL) {
        wm_fill(lower->id, 0, 0, 20, 16, 0x00FF0000U);
    }
    struct wm_window *upper = wm_create(10, 8, 20, 16, 0x000000FFU);
    check(upper != NULL && wm_count() == 2, "wm creates a second window");
    if (upper != NULL) {
        wm_fill(upper->id, 0, 0, 20, 16, 0x000000FFU);
    }
    check(wm_draw_to(&target) == 0, "wm composites windows into a surface");
    check(gfx_get_pixel(&target, 3, 3) == 0x00FF0000U, "wm draws the first window");
    check(gfx_get_pixel(&target, 11, 9) == 0x000000FFU, "wm draws overlapping windows in z order");
    if (lower != NULL) {
        check(wm_zorder_at(0) == lower->id && wm_find(lower->id) == lower, "wm tracks window z order");
        check(wm_destroy(lower->id) == 0 && wm_count() == 1, "wm destroys a window");
        gfx_clear(&target, 0);
        wm_draw_to(&target);
        check(gfx_get_pixel(&target, 3, 3) == 0 &&
              gfx_get_pixel(&target, 11, 9) == 0x000000FFU,
              "wm excludes destroyed windows from composition");
    }
    if (upper != NULL) {
        wm_destroy(upper->id);
    }
    check(wm_count() == 0, "wm window table drains");

    u64 status = 0xFFFFFFFFFFFFFFFFULL;
    u64 pid = run_program("guitest", user_guitest_start, user_guitest_end, &status);
    check(pid != 0 && status == 0, "user graphics program draws and exits cleanly");
    check(fb_pixel(36, 36) == 0x00FF0000U && fb_pixel(204, 68) == 0x0000FF00U &&
          fb_pixel(364, 100) == 0x000000FFU,
          "user graphics output is present in the framebuffer");
    fb_restore_text();
    console_clear();
    check(!fb_mode_active(), "framebuffer restores text mode after graphics use");
    check(process_count() == 0, "graphics process is reaped");
}

static u32 test_compositor_flush_count = 0;

static void test_compositor_flush(struct compositor *compositor)
{
    if (compositor != NULL) {
        test_compositor_flush_count++;
    }
}

static void test_fd(void)
{
    struct fd_table table;
    fd_table_init(&table);
    check(fd_count(&table) == 3, "fd table starts with three standard descriptors");
    struct vfs_node *readme = vfs_lookup(vfs_root(), "/README");
    check(readme != NULL, "fd test locates a file");
    int descriptor = fd_alloc(&table, FD_TYPE_VFS, FD_FLAG_READ, readme);
    check(descriptor == 3, "fd allocation returns the lowest free descriptor");
    char buffer[8];
    int received = fd_read(&table, descriptor, buffer, 4);
    check(received == 4 && buffer[0] == 'Z', "fd read returns file data at the current offset");
    check(fd_read(&table, descriptor, buffer, 4) == 4, "fd offset advances across reads");
    check(fd_write(&table, descriptor, "x", 1) == FD_ERROR_MODE,
          "fd rejects writes on a read-only descriptor");
    check(fd_close(&table, descriptor) == 0 && fd_count(&table) == 3, "fd close releases the descriptor");
    check(fd_close(&table, descriptor) == FD_ERROR_INVALID, "fd rejects closing a closed descriptor");
    struct fd_table copy;
    fd_copy(&copy, &table);
    int copy_descriptor = fd_alloc(&copy, FD_TYPE_VFS, FD_FLAG_READ, readme);
    check(copy_descriptor == 3 && fd_close(&copy, copy_descriptor) == 0 && fd_count(&table) == 3,
          "fd table copies keep independent descriptor state");
    int duplicated = fd_dup(&table, 1);
    check(duplicated == 3 && fd_close(&table, duplicated) == 0, "fd dup allocates an independent descriptor");
    u64 image_size = (u64)(user_program_end - user_program_start);
    struct process *parent = process_create("fd-parent", 0, user_program_start, image_size);
    check(parent != NULL, "fd test creates a parent process");
    if (parent != NULL) {
        int inherited = fd_alloc(&parent->fds, FD_TYPE_VFS, FD_FLAG_READ, readme);
        struct registers frame;
        memset(&frame, 0, sizeof(frame));
        frame.rip = ELF_USER_BASE;
        frame.rsp = VMM_USER_STACK_TOP;
        frame.cs = 0x1B;
        frame.ss = 0x23;
        frame.rflags = 0x202;
        u64 child_pid = 0;
        int fork_result = process_fork_frame(parent->pid, &frame, &child_pid);
        struct process *child = process_find(child_pid);
        check(fork_result == PROCESS_ERROR_NONE && child != NULL, "fd test forks a child process");
        if (child != NULL) {
            check(fd_get(&child->fds, inherited, NULL) == 0 &&
                  fd_count(&child->fds) == fd_count(&parent->fds),
                  "fork inherits the parent descriptor table");
            fd_close(&child->fds, inherited);
            check(fd_get(&child->fds, inherited, NULL) != 0 &&
                  fd_get(&parent->fds, inherited, NULL) == 0,
                  "child descriptor close leaves the parent descriptor open");
            process_kill(child->pid, 0);
        }
        process_kill(parent->pid, 0);
        process_reap_zombies();
    }
    check(process_count() == 0, "fd test reaps every process");
}

static void test_credentials(void)
{
    struct credentials root;
    cred_init_root(&root);
    check(cred_is_root(&root) && cred_can_access(&root, 1000, 1000, 0, 0, 0, CRED_MODE_READ),
          "root credentials bypass permission bits");
    struct credentials user;
    cred_init(&user, 1000, 1000);
    check(!cred_is_root(&user) && cred_effective_uid(&user) == 1000, "user credentials initialize correctly");
    check(cred_can_access(&user, 1000, 1000, 6, 0, 0, 6), "owner mode grants read and write");
    check(!cred_can_access(&user, 2000, 2000, 6, 6, 4, CRED_MODE_WRITE),
          "other mode denies write access");
    check(cred_can_access(&user, 2000, 1000, 6, CRED_MODE_READ, 0, CRED_MODE_READ),
          "group mode grants read access");
    check(!cred_can_access(&user, 2000, 3000, 6, 6, CRED_MODE_READ, 6),
          "access is denied across owner, group and other classes");
    bool escalated = cred_seteuid(&user, 0);
    check(!escalated && !cred_is_root(&user), "unprivileged credentials cannot escalate to root");
    struct credentials inherited;
    cred_inherit(&user, &inherited);
    check(inherited.uid == user.uid && inherited.euid == user.euid,
          "credentials are inherited across fork");
    struct credentials admin;
    cred_init_root(&admin);
    check(cred_switch_user(&admin, 1000, 1000) && admin.uid == 1000 && !admin.privileged,
          "root credentials can switch to an unprivileged user");
    check(cred_effective_gid(&user) == 1000, "effective group identifier is reported");
}

static void test_signals(void)
{
    struct signal_state state;
    signal_init(&state);
    check(state.pending == 0 && state.blocked == 0 && state.installed == 0, "signal state starts empty");
    check(signal_install(&state, SIGNAL_SIGUSR1, 0x40000100ULL, 0x40000200ULL) == 0 && state.installed == 1,
          "signal handler installs for an allowed signal");
    check(signal_install(&state, SIGNAL_SIGKILL, 0x40000100ULL, 0x40000200ULL) == 0 && state.installed == 1,
          "signal handler cannot be installed for sigkill");
    check(signal_raise(&state, SIGNAL_SIGUSR1) && signal_pending_unblocked(&state) != 0,
          "signal raise marks a deliverable signal");
    check(signal_mask(&state, 1ULL << SIGNAL_SIGUSR1, 1) == 0 && !signal_has_deliverable(&state),
          "blocked signals stay pending instead of delivering");
    check(signal_mask(&state, 1ULL << SIGNAL_SIGUSR1, 2) == (1ULL << SIGNAL_SIGUSR1) &&
          signal_has_deliverable(&state),
          "unblocking releases the pending signal");
    u64 image_size = (u64)(user_program_end - user_program_start);
    struct process *process = process_create("signal-test", 0, user_program_start, image_size);
    check(process != NULL, "signal test creates a process");
    if (process != NULL) {
        u64 stack_frame = pmm_alloc_frame();
        bool mapped = stack_frame != 0 &&
                      vmm_map_user_page(&process->space, 0x40080000, stack_frame,
                                        PTE_PRESENT | PTE_WRITABLE | PTE_USER) == 0;
        check(mapped, "signal test maps a user stack page");
        process_set_signal(process, SIGNAL_SIGUSR1, 0x40000010ULL, 0x40000020ULL);
        signal_raise(&process->signals, SIGNAL_SIGUSR1);
        struct registers frame;
        memset(&frame, 0, sizeof(frame));
        frame.rip = ELF_USER_BASE;
        frame.rsp = 0x40080F00ULL;
        frame.cs = 0x1B;
        frame.ss = 0x23;
        frame.rflags = 0x202;
        int delivered = signal_deliver(process, &frame);
        check(delivered == 1 && frame.rip == 0x40000010ULL && frame.rsp < 0x40080F00ULL,
              "signal delivery redirects the user frame to the handler");
        u64 trampoline = 0;
        u64 active_pml4 = vmm_pml4_phys();
        if (vmm_switch_address_space(&process->space) == 0) {
            trampoline = *(u64 *)(usize)frame.rsp;
            write_cr3(active_pml4);
        }
        check(trampoline == 0x40000020ULL, "signal frame stores the user sigreturn trampoline");
        check(process->signals.delivered == 1, "signal delivery counter advances");
        check(!process_has_pending_signal(process), "delivered signal clears the pending bit");
        struct registers saved;
        memset(&saved, 0, sizeof(saved));
        saved.rip = ELF_USER_BASE;
        saved.rsp = 0x40080E00ULL;
        saved.cs = 0x1B;
        saved.ss = 0x23;
        check(signal_restore(&frame, &saved, sizeof(saved)) && frame.rip == ELF_USER_BASE &&
              frame.rsp == 0x40080E00ULL,
              "sigreturn restores the saved user register frame");
        process_kill(process->pid, 0);
        process_reap_zombies();
    }
    check(process_count() == 0, "signal test reaps its process");
}

static void test_wait_queue(void)
{
    waitq_init();
    check(waitq_count() == 0, "wait queue starts empty");
    u64 image_size = (u64)(user_program_end - user_program_start);
    struct process *waiter = process_create("waitq-waiter", 0, user_program_start, image_size);
    check(waiter != NULL, "wait queue test creates a waiting process");
    if (waiter == NULL) {
        return;
    }
    process_dequeue_ready(waiter);
    waiter->state = PROCESS_BLOCKED;
    check(waitq_block(waiter->pid, 0, 0) == 0 && waitq_count() == 1,
          "wait queue records a blocked parent");
    check(waitq_deadline(waiter->pid) == 0, "wait queue reports an infinite deadline");
    check(waitq_wake_target(4242) == 1 && waiter->state == PROCESS_READY,
          "wait queue wakes a parent when a child exits");
    process_dequeue_ready(waiter);
    waiter->state = PROCESS_BLOCKED;
    u64 now = timer_ticks();
    waitq_block(waiter->pid, 0, now + 1);
    check(waitq_expire(now + 1) == 1 && waiter->state == PROCESS_READY,
          "wait queue expires a timed wait");
    process_dequeue_ready(waiter);
    waiter->state = PROCESS_BLOCKED;
    waitq_block(waiter->pid, 0, 0);
    waitq_remove(waiter->pid);
    check(waitq_count() == 0, "wait queue removal releases the entry");
    process_kill(waiter->pid, 0);
    process_reap_zombies();
    check(process_count() == 0, "wait queue test reaps its process");
}

static void test_font(void)
{
    check(font_glyph_pixels(' ') == 0, "font space glyph is blank");
    check(font_glyph_pixels('A') > 8, "font letter glyph contains pixels");
    u32 populated_rows = 0;
    for (u32 row = 0; row < FONT_CHAR_HEIGHT; row++) {
        if (font_glyph_row('Z', row) != 0) {
            populated_rows++;
        }
    }
    check(font_glyph_pixels('Z') > 8 && populated_rows > 4, "font glyph bitmap spans multiple rows");
    check(font_text_width("Z-OS") == 32, "font text width advances eight pixels per character");
    check(font_text_width("") == 0, "font width of empty text is zero");
    static u32 font_pixels[64 * 32];
    static u32 other_pixels[64 * 32];
    struct gfx_surface surface;
    struct gfx_surface other;
    gfx_init_surface(&surface, font_pixels, 64, 32, 64, GFX_FORMAT_RGB32);
    gfx_init_surface(&other, other_pixels, 64, 32, 64, GFX_FORMAT_RGB32);
    gfx_clear(&surface, 0);
    gfx_clear(&other, 0);
    font_draw_text(&surface, 0, 0, "Z-OS", 0x00FFFFFFU);
    font_draw_text(&other, 0, 0, "Z-OS", 0x00FFFFFFU);
    u32 lit = gfx_count_color(&surface, 0, 0, 64, 32, 0x00FFFFFFU);
    check(lit > 20, "font draws visible text into a graphics surface");
    check(gfx_checksum(&surface, 0, 0, 64, 32) == gfx_checksum(&other, 0, 0, 64, 32),
          "font rendering is deterministic");
    static u32 third_pixels[64 * 32];
    struct gfx_surface third;
    gfx_init_surface(&third, third_pixels, 64, 32, 64, GFX_FORMAT_RGB32);
    gfx_clear(&third, 0);
    font_draw_text(&third, 0, 0, "Z-OS", 0x00FFFFFFU);
    font_draw_text(&third, 0, 16, "line", 0x00FFFFFFU);
    check(gfx_checksum(&third, 0, 0, 64, 32) != gfx_checksum(&surface, 0, 0, 64, 32),
          "font text placement changes rendered output");
}

static void test_mouse(void)
{
    mouse_init();
    struct mouse_state state;
    mouse_get_state(&state);
    check(state.initialized, "mouse driver initializes the ps2 auxiliary device");
    mouse_set_bounds(320, 200);
    mouse_move_to(100, 100);
    u8 packet[3];
    packet[0] = 0x08;
    packet[1] = 10;
    packet[2] = 4;
    mouse_process_packet(packet);
    mouse_get_state(&state);
    check(state.x == 110 && state.y == 96, "mouse decodes relative movement packets");
    u32 packets_before = state.packets;
    packet[0] = 0x09;
    packet[1] = 0;
    packet[2] = 0;
    mouse_process_packet(packet);
    mouse_get_state(&state);
    check(state.packets == packets_before + 1 && (state.buttons & MOUSE_BUTTON_LEFT) != 0,
          "mouse decodes button state changes");
    static u32 cursor_pixels[64 * 64];
    struct gfx_surface surface;
    gfx_init_surface(&surface, cursor_pixels, 64, 64, 64, GFX_FORMAT_RGB32);
    gfx_clear(&surface, 0x00010203U);
    u32 blank_checksum = gfx_checksum(&surface, 0, 0, 64, 64);
    mouse_move_to(20, 20);
    check(mouse_render(&surface) == 0 &&
          gfx_count_color(&surface, 0, 0, 64, 64, 0x00FFFFFFU) > 0,
          "mouse renders a cursor into a surface");
    check(mouse_restore(&surface) == 0 &&
          gfx_count_color(&surface, 0, 0, 64, 64, 0x00FFFFFFU) == 0,
          "mouse restores the background under the cursor");
    check(gfx_checksum(&surface, 0, 0, 64, 64) == blank_checksum,
          "mouse cursor restore leaves the surface unchanged");
    mouse_move_to(319, 199);
    packet[0] = 0x28;
    packet[1] = 100;
    packet[2] = 0xF0;
    mouse_process_packet(packet);
    mouse_get_state(&state);
    check(state.x == 319 && state.y == 199, "mouse clamps movement to the surface bounds");
}

static void test_compositor(void)
{
    static u32 compositor_pixels[64 * 64];
    struct gfx_surface target;
    gfx_init_surface(&target, compositor_pixels, 64, 64, 64, GFX_FORMAT_RGB32);
    gfx_clear(&target, 0);
    struct compositor compositor;
    check(compositor_init(&compositor, &target, test_compositor_flush) == 0 &&
          compositor_layer_count(&compositor) == 0,
          "compositor initializes with a target surface");
    struct compositor_layer *lower = compositor_add_layer(&compositor, 2, 2, 20, 16, 0x00AA0000U);
    check(lower != NULL && compositor_layer_count(&compositor) == 1, "compositor adds a layer");
    if (lower != NULL) {
        gfx_fill_rect(&lower->surface, 0, 0, 20, 16, 0x00FF0000U);
    }
    struct compositor_layer *upper = compositor_add_layer(&compositor, 10, 8, 20, 16, 0x000000FFU);
    check(upper != NULL && compositor_layer_count(&compositor) == 2, "compositor adds a second layer");
    check(compositor_present(&compositor) == 0 && compositor.composites == 1,
          "compositor presents the composed frame");
    check(gfx_get_pixel(&target, 3, 3) == 0x00FF0000U, "compositor draws the first layer");
    check(gfx_get_pixel(&target, 11, 9) == 0x000000FFU, "compositor stacks later layers on top");
    check(compositor.flush_count == 1 && test_compositor_flush_count == 1,
          "compositor invokes the flush callback");
    compositor_damage(&compositor, 1, 1, 8, 8);
    compositor_damage(&compositor, 4, 4, 8, 8);
    check(compositor_damage_count(&compositor) == 2, "compositor records damage rectangles");
    compositor_clear_damage(&compositor);
    check(compositor_damage_count(&compositor) == 0, "compositor clears damage after present");
    if (lower != NULL) {
        check(compositor_remove_layer(&compositor, lower->id) == 0 &&
              compositor_layer_count(&compositor) == 1,
              "compositor removes a layer");
    }
    compositor_present(&compositor);
    check(gfx_get_pixel(&target, 3, 3) == 0x00101820U &&
          gfx_get_pixel(&target, 11, 9) == 0x000000FFU,
          "compositor excludes removed layers from output");
    if (upper != NULL) {
        compositor_remove_layer(&compositor, upper->id);
    }
    check(compositor_layer_count(&compositor) == 0, "compositor layer table drains");
}

static void test_posix_user_program(void)
{
    sched_mark_reset();
    u64 status = 0xFFFFFFFFFFFFFFFFULL;
    u64 pid = run_program("postest", user_postest_start, user_postest_end, &status);
    check(pid != 0 && status == 0, "posix user program validates fd, signals and wait");
    check(sched_marker_total(0x31) > 0 && sched_marker_total(0x32) > 0 &&
          sched_marker_total(0x33) > 0 && sched_marker_total(0x34) > 0,
          "user descriptor inheritance marks are observed");
    check(sched_marker_total(0x35) > 0, "user wait timeout path is observed");
    check(sched_marker_total(0x36) > 0 && sched_marker_total(0x37) > 0 &&
          sched_marker_total(0x38) > 0,
          "user signal delivery, blocking and interrupt paths are observed");
    check(sched_marker_total(0x3F) == 0, "posix user program reported no failure marker");
    process_reap_zombies();
    check(process_count() == 0, "posix user program processes are reaped");
}

static void test_extended_graphics(void)
{
    check(fb_set_mode(800, 600, 32) == 0, "extended graphics applies the framebuffer mode");
    u64 status = 0xFFFFFFFFFFFFFFFFULL;
    u64 pid = run_program("guitest2", user_guitest2_start, user_guitest2_end, &status);
    check(pid != 0 && status == 0, "user graphics program renders text and reports mouse state");
    u32 text_lit = 0;
    for (u32 y = 84; y < 100; y++) {
        for (u32 x = 60; x < 240; x++) {
            if (fb_pixel(x, y) == 0x00E8EEF7U) {
                text_lit++;
            }
        }
    }
    check(text_lit > 40, "framebuffer contains rendered font pixels");
    check(fb_pixel(45, 45) == 0x00F2C14EU, "framebuffer contains the rendered title bar");
    check(fb_pixel(100, 240) == 0x00D9453DU, "framebuffer contains the accent rectangle");
    fb_restore_text();
    console_clear();
    check(!fb_mode_active(), "extended graphics restores text mode");
    process_reap_zombies();
    check(process_count() == 0, "extended graphics processes are reaped");
}

static void test_storage(void)
{
    check(ata_present(), "ata primary master device is detected");
    check(ata_sector_count() >= 4096, "ata device reports a usable sector count");
    u8 original[512];
    u8 pattern[512];
    u8 readback[512];
    u32 lba = ata_sector_count() - 8;
    bool io_ok = false;
    if (ata_read_sectors(lba, 1, original) == 0) {
        for (u32 index = 0; index < 512; index++) {
            pattern[index] = (u8)(index * 7 + 3);
        }
        if (ata_write_sectors(lba, 1, pattern) == 0) {
            memset(readback, 0, sizeof(readback));
            if (ata_read_sectors(lba, 1, readback) == 0 && memcmp(pattern, readback, 512) == 0) {
                io_ok = true;
            }
        }
        ata_write_sectors(lba, 1, original);
    }
    check(io_ok, "ata sector write and readback match");
    check(zfs_format() == ZFS_ERROR_NONE, "zfs formats the ata disk");
    check(zfs_mount() == ZFS_ERROR_NONE && zfs_mounted(), "zfs mounts its superblock");
    const char *payload = "persistent-storage-roundtrip";
    check(zfs_write_file("selftest.txt", payload, (u32)strlen(payload)) == ZFS_ERROR_NONE,
          "zfs writes a file to disk");
    char buffer[128];
    memset(buffer, 0, sizeof(buffer));
    int read = zfs_read_file("selftest.txt", buffer, sizeof(buffer) - 1);
    check(read == (int)strlen(payload) && strcmp(buffer, payload) == 0, "zfs reads file data back");
    struct zfs_dirent dirents[ZFS_MAX_FILES];
    memset(dirents, 0, sizeof(dirents));
    u32 count = zfs_list(dirents, ZFS_MAX_FILES);
    check(count == 1 && strcmp(dirents[0].name, "selftest.txt") == 0, "zfs lists the stored file");
    check(zfs_mount() == ZFS_ERROR_NONE && zfs_file_count() == 1, "zfs reloads metadata from disk");
    memset(buffer, 0, sizeof(buffer));
    read = zfs_read_file("selftest.txt", buffer, sizeof(buffer) - 1);
    check(read > 0 && strcmp(buffer, payload) == 0, "zfs data survives a metadata reload");
    check(zfs_mount_vfs() == ZFS_ERROR_NONE, "zfs attaches its files under /disk");
    struct vfs_node *node = vfs_lookup(vfs_root(), "/disk/selftest.txt");
    check(node != NULL && vfs_is_file(node), "vfs exposes the mounted zfs file");
    if (node != NULL) {
        char vfs_buffer[128];
        memset(vfs_buffer, 0, sizeof(vfs_buffer));
        int vfs_count = vfs_read(node, 0, vfs_buffer, sizeof(vfs_buffer) - 1);
        check(vfs_count == (int)strlen(payload) && strcmp(vfs_buffer, payload) == 0,
              "vfs file content matches the zfs payload");
    } else {
        check(false, "vfs file content matches the zfs payload");
    }
    check(zfs_remove_file("selftest.txt") == ZFS_ERROR_NONE, "zfs removes a file");
    check(zfs_file_count() == 0, "zfs removes the directory entry");
}

static void test_mouse_events(void)
{
    mouse_event_init();
    check(mouse_event_count() == 0, "mouse event queue starts empty");
    mouse_inject_event(MOUSE_EVENT_MOVE, 120, 90, 4, -3, 0);
    struct mouse_event event;
    bool popped = mouse_pop_event(&event);
    check(popped && event.type == MOUSE_EVENT_MOVE && event.x == 120 && event.y == 90 &&
          event.delta_x == 4 && event.delta_y == -3, "mouse move events carry coordinates");
    mouse_inject_event(MOUSE_EVENT_BUTTON, 120, 90, 0, 0, MOUSE_BUTTON_LEFT);
    popped = mouse_pop_event(&event);
    check(popped && event.type == MOUSE_EVENT_BUTTON && (event.buttons & MOUSE_BUTTON_LEFT) != 0,
          "mouse button events carry button state");
    check(mouse_event_count() == 0 && !mouse_pop_event(&event), "mouse event queue drains in order");
    mouse_inject_event(MOUSE_EVENT_MOVE, 10, 10, 1, 1, 0);
    mouse_inject_event(MOUSE_EVENT_BUTTON, 10, 10, 0, 0, MOUSE_BUTTON_RIGHT);
    check(mouse_pop_event(&event) && event.type == MOUSE_EVENT_MOVE, "mouse queue preserves fifo order");
    check(mouse_pop_event(&event) && event.type == MOUSE_EVENT_BUTTON, "mouse queue delivers queued buttons");
    u8 packet[3];
    packet[0] = 0x08;
    packet[1] = 6;
    packet[2] = 2;
    mouse_process_packet(packet);
    check(mouse_event_count() > 0, "hardware packets generate mouse events");
    while (mouse_pop_event(&event)) {
    }
}

static void test_text_cursor(void)
{
    mouse_set_gui_mode(false);
    mouse_move_to(80, 96);
    mouse_text_cursor_restore();
    mouse_text_cursor_update();
    bool visible = mouse_text_cursor_visible();
    i32 column = mouse_text_cursor_column();
    i32 row = mouse_text_cursor_row();
    volatile u16 *vga = (volatile u16 *)0xB8000;
    u16 cell = vga[(u32)row * 80 + (u32)column];
    mouse_text_cursor_restore();
    bool restored_visible = mouse_text_cursor_visible();
    check(visible, "text mode mouse cursor is drawn");
    check(column == 10 && row == 6, "text cursor tracks the mouse position");
    check((cell & 0x0F00) == 0x0F00 && (cell & 0x00FF) == 0x00DB, "text cursor cell uses the overlay glyph");
    check(!restored_visible, "text cursor restores the console cell");
    mouse_set_gui_mode(true);
    mouse_move_to(200, 200);
    mouse_text_cursor_update();
    check(!mouse_text_cursor_visible(), "graphics mode suppresses the text cursor");
    mouse_set_gui_mode(false);
    mouse_move_to(752, 568);
}

static void test_wm_frames(void)
{
    static u32 frame_pixels[400 * 300];
    struct gfx_surface target;
    gfx_init_surface(&target, frame_pixels, 400, 300, 400, GFX_FORMAT_RGB32);
    gfx_clear(&target, 0);
    wm_init();
    struct wm_window *window = wm_create(40, 30, 200, 120, 0x00F0F4FAU);
    check(window != NULL, "wm creates a titled window");
    if (window == NULL) {
        return;
    }
    wm_set_title(window, "Files");
    gfx_fill_rect(&window->surface, 0, 0, 200, 120, 0x00F0F4FAU);
    check(wm_compose(&target) == 0, "wm composes framed windows");
    check(gfx_get_pixel(&target, 45, 35) == 0x002F6FB5U, "wm draws the window title bar");
    check(gfx_get_pixel(&target, 40 + 200 - 12, 30 + 4) == 0x00C43B3BU, "wm draws the close button");
    check(wm_close_hit(window, 40 + 200 - 10, 30 + 8), "wm close hit test matches the button");
    check(!wm_close_hit(window, 60, 60), "wm close hit test rejects the window body");
    check(wm_title_hit(window, 60, 40) && !wm_title_hit(window, 60, 100),
          "wm title hit test separates title and body");
    check(wm_window_at_point(45, 35) == window, "wm finds the window under a point");
    struct wm_window *second = wm_create(80, 60, 120, 80, 0x00E6ECF6U);
    check(second != NULL, "wm creates a second stacked window");
    if (second != NULL) {
        wm_set_title(second, "Terminal");
        wm_raise(window->id);
        check(wm_zorder_at(wm_count() - 1) == window->id, "wm raise moves the window to the top");
        wm_destroy(second->id);
    }
    wm_destroy(window->id);
    check(wm_count() == 0, "wm frame test reaps its windows");
}

static void test_desktop(void)
{
    static u32 desktop_pixels[800 * 600];
    struct gfx_surface surface;
    gfx_init_surface(&surface, desktop_pixels, 800, 600, 800, GFX_FORMAT_RGB32);
    desktop_init(&surface);
    desktop_render();
    check(gfx_get_pixel(&surface, 5, 5) == DESKTOP_COLOR_STATUS, "desktop renders the status bar");
    check(gfx_get_pixel(&surface, 5, 590) == DESKTOP_COLOR_TASKBAR, "desktop renders the taskbar");
    check(gfx_get_pixel(&surface, 400, 300) == DESKTOP_COLOR_WALLPAPER, "desktop renders the wallpaper");
    check(desktop_window_count() == 0, "desktop starts without open windows");
    u32 icon_index = 99;
    check(desktop_icon_at(30, 60, &icon_index) && icon_index == 0,
          "desktop icon hit test finds the first icon");
    struct mouse_event event;
    memset(&event, 0, sizeof(event));
    u32 clicks = desktop_click_count();
    event.type = MOUSE_EVENT_BUTTON;
    event.x = 60;
    event.y = 70;
    event.buttons = MOUSE_BUTTON_LEFT;
    check(desktop_handle_mouse(&event), "desktop handles an icon click event");
    check(desktop_click_count() == clicks + 1 && desktop_app_is_open(0),
          "desktop icon click opens the files window");
    check(desktop_window_count() == 1, "desktop tracks the opened window");
    desktop_render();
    struct wm_window *window = wm_window_at_point(200, 120);
    check(window != NULL, "desktop window is present in the composited scene");
    if (window != NULL) {
        i32 original_x = window->x;
        event.type = MOUSE_EVENT_BUTTON;
        event.x = window->x + 40;
        event.y = window->y + 6;
        event.buttons = MOUSE_BUTTON_LEFT;
        desktop_handle_mouse(&event);
        check(desktop_dragging_id() == (i32)window->id, "desktop starts dragging from the title bar");
        event.type = MOUSE_EVENT_MOVE;
        event.x = window->x + 90;
        event.y = window->y + 46;
        event.buttons = MOUSE_BUTTON_LEFT;
        desktop_handle_mouse(&event);
        check(window->x == original_x + 50, "desktop drag moves the window by the pointer delta");
        event.type = MOUSE_EVENT_BUTTON;
        event.buttons = 0;
        desktop_handle_mouse(&event);
        check(desktop_dragging_id() == 0, "desktop ends the drag on button release");
        i32 close_x = window->x + (i32)window->width - 10;
        i32 close_y = window->y + 8;
        event.type = MOUSE_EVENT_BUTTON;
        event.x = close_x;
        event.y = close_y;
        event.buttons = MOUSE_BUTTON_LEFT;
        desktop_handle_mouse(&event);
        check(!desktop_app_is_open(0) && desktop_window_count() == 0,
              "desktop close button closes the window");
    } else {
        check(false, "desktop starts dragging from the title bar");
        check(false, "desktop drag moves the window by the pointer delta");
        check(false, "desktop ends the drag on button release");
        check(false, "desktop close button closes the window");
    }
    desktop_init(&surface);
    desktop_render();
    memset(&event, 0, sizeof(event));
    event.type = MOUSE_EVENT_BUTTON;
    event.x = 740;
    event.y = 600 - DESKTOP_TASKBAR_HEIGHT + 12;
    event.buttons = MOUSE_BUTTON_LEFT;
    desktop_handle_mouse(&event);
    check(desktop_should_exit(), "desktop taskbar exit button requests session exit");
}

static void test_gui_mode(void)
{
    check(fb_set_mode(800, 600, 32) == 0, "gui mode applies the framebuffer mode");
    struct gfx_surface *surface = fb_surface();
    check(surface != NULL, "gui mode exposes the framebuffer surface");
    if (surface != NULL) {
        desktop_init(surface);
        desktop_render();
        check(fb_pixel(5, 5) == DESKTOP_COLOR_STATUS, "gui desktop renders its status bar to the framebuffer");
        check(fb_pixel(5, 590) == DESKTOP_COLOR_TASKBAR, "gui desktop renders its taskbar to the framebuffer");
        check(fb_pixel(400, 300) == DESKTOP_COLOR_WALLPAPER, "gui desktop renders its wallpaper to the framebuffer");
    } else {
        check(false, "gui desktop renders its status bar to the framebuffer");
        check(false, "gui desktop renders its taskbar to the framebuffer");
        check(false, "gui desktop renders its wallpaper to the framebuffer");
    }
    mouse_set_gui_mode(true);
    check(mouse_gui_mode(), "gui mode marks the mouse subsystem for graphics rendering");
    mouse_set_gui_mode(false);
    fb_restore_text();
    console_clear();
    check(!fb_mode_active(), "gui exit restores text mode");
}

void selftest_thread(void *argument)
{
    (void)argument;
    kprintf("ZTEST BEGIN\n");
    test_memory();
    test_vfs();
    test_formatting();
    test_time();
    test_threads();
    test_elf();
    test_user_mode();
    test_address_spaces();
    test_preemption();
    test_fork_wait();
    test_display();
    test_graphics();
    test_fd();
    test_credentials();
    test_signals();
    test_wait_queue();
    test_font();
    test_mouse();
    test_compositor();
    test_posix_user_program();
    test_extended_graphics();
    test_storage();
    test_mouse_events();
    test_text_cursor();
    test_wm_frames();
    test_desktop();
    test_gui_mode();
    kprintf("ZTEST SUMMARY passed=%u failed=%u\n", tests_passed, tests_failed);
    if (tests_failed == 0) {
        kprintf("ZTEST RESULT PASS\n");
    } else {
        kprintf("ZTEST RESULT FAIL\n");
    }
    sched_sleep_ms(200);
    qemu_debug_exit(tests_failed == 0 ? 0x10 : 0x11);
}
