#include "tests.h"
#include "console.h"
#include "elf.h"
#include "heap.h"
#include "input.h"
#include "io.h"
#include "kernel.h"
#include "lib.h"
#include "pmm.h"
#include "fb.h"
#include "gfx.h"
#include "power.h"
#include "process.h"
#include "sched.h"
#include "rtc.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "user.h"
#include "vfs.h"
#include "vmm.h"
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
    kprintf("ZTEST SUMMARY passed=%u failed=%u\n", tests_passed, tests_failed);
    if (tests_failed == 0) {
        kprintf("ZTEST RESULT PASS\n");
    } else {
        kprintf("ZTEST RESULT FAIL\n");
    }
    sched_sleep_ms(200);
    qemu_debug_exit(tests_failed == 0 ? 0x10 : 0x11);
}
