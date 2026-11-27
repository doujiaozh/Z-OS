#include "tests.h"
#include "arp.h"
#include "ata.h"
#include "compositor.h"
#include "console.h"
#include "cred.h"
#include "dns.h"
#include "desktop.h"
#include "dhcp.h"
#include "e1000.h"
#include "eth.h"
#include "icmp.h"
#include "ipv4.h"
#include "net.h"
#include "elf.h"
#include "fd.h"
#include "font.h"
#include "heap.h"
#include "kstack.h"
#include "http.h"
#include "input.h"
#include "interrupts.h"
#include "io.h"
#include "kernel.h"
#include "lib.h"
#include "pmm.h"
#include "protect.h"
#include "fb.h"
#include "gfx.h"
#include "power.h"
#include "process.h"
#include "sched.h"
#include "security.h"
#include "signal.h"
#include "socket.h"
#include "tcp.h"
#include "waitq.h"
#include "mouse.h"
#include "rtc.h"
#include "string.h"
#include "syscall.h"
#include "thread.h"
#include "timer.h"
#include "user.h"
#include "udp.h"
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
            security_stac();
            trampoline = *(u64 *)(usize)frame.rsp;
            security_clac();
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
          gfx_count_color(&surface, 0, 0, 64, 64, 0x00FFFFFFU) > 0 &&
          gfx_count_color(&surface, 0, 0, 64, 64, 0x00000000U) > 0,
          "mouse renders an outlined cursor into a surface");
    check(mouse_restore(&surface) == 0 &&
          gfx_count_color(&surface, 0, 0, 64, 64, 0x00FFFFFFU) == 0,
          "mouse restores the background under the cursor");
    mouse_move_to(30, 30);
    mouse_render(&surface);
    mouse_restore(&surface);
    check(gfx_checksum(&surface, 0, 0, 64, 64) == blank_checksum,
          "mouse cursor restore leaves the surface unchanged after a move");
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

static void test_vga_text_restore(void)
{
    check(fb_set_mode(800, 600, 32) == 0, "vga restore test enters graphics mode");
    struct gfx_surface *buffer = fb_double_buffer();
    struct gfx_surface *vram = fb_surface();
    check(buffer != NULL && vram != NULL && buffer != vram,
          "framebuffer exposes a separate double buffer surface");
    if (buffer == NULL || vram == NULL) {
        fb_restore_text();
        console_clear();
        return;
    }
    u32 outside_before = fb_pixel(5, 5);
    gfx_clear(buffer, 0x00112233U);
    gfx_fill_rect(buffer, 10, 20, 30, 40, 0x00AABBCCU);
    fb_present_rect(10, 20, 30, 40);
    check(fb_pixel(20, 30) == 0x00AABBCCU, "dirty present copies the changed rectangle to vram");
    check(fb_pixel(5, 5) == outside_before, "dirty present leaves pixels outside the rectangle untouched");
    fb_present();
    check(fb_pixel(5, 5) == 0x00112233U, "full present copies the whole frame to vram");
    desktop_init(buffer);
    desktop_dirty_reset();
    desktop_dirty_mark(100, 50, 10, 10);
    desktop_dirty_mark(120, 80, 10, 10);
    i32 dirty_x = 0;
    i32 dirty_y = 0;
    u32 dirty_width = 0;
    u32 dirty_height = 0;
    bool dirty_ok = desktop_dirty_take(&dirty_x, &dirty_y, &dirty_width, &dirty_height);
    check(dirty_ok && dirty_x == 100 && dirty_y == 50 && dirty_width >= 30 && dirty_height >= 40,
          "desktop dirty rectangles merge into one damage region");
    fb_restore_text();
    console_clear();
    check(!fb_double_buffer_ready(), "text mode restore releases the double buffer");
    check(fb_vga_crtc_register(0x00) == 0x5F && fb_vga_crtc_register(0x01) == 0x4F,
          "text mode restore programs the crtc horizontal timing");
    check(fb_vga_crtc_register(0x06) == 0xBF && fb_vga_crtc_register(0x12) == 0x8F,
          "text mode restore programs the crtc vertical timing");
    check(fb_vga_sequencer_register(0x01) == 0x00 && fb_vga_sequencer_register(0x04) == 0x02,
          "text mode restore programs the sequencer clocking and memory mode");
    check(fb_vga_graphics_register(0x05) == 0x10 && fb_vga_graphics_register(0x06) == 0x0E,
          "text mode restore programs the graphics controller mode registers");
    check((fb_vga_attribute_register(0x10) & 0x01) == 0,
          "text mode restore leaves the attribute controller in text mode");
    check(fb_vga_font_byte('A', 0) == (u8)font_glyph_row('A', 0),
          "text mode restore reloads the font plane");
    console_clear();
    kprintf("VGA RESTORE OK\n");
    check((fb_vga_text_cell(0, 0) & 0x00FF) == 'V',
          "text mode restore leaves a usable text buffer for the shell");
}
static void test_network(void)
{
    const struct net_config *config = net_config_get();
    check(e1000_present(), "e1000 pci device is detected");
    check(config->ready && config->ip == NET_IPV4(10, 0, 2, 15),
          "network stack configures the guest ipv4 address");
    bool mac_valid = false;
    for (u32 index = 0; index < 6; index++) {
        if (config->mac[index] != 0) {
            mac_valid = true;
        }
    }
    check(mac_valid, "e1000 reports a hardware mac address");
    check(net_htons(0x1234) == 0x3412 && net_ntohs(0x3412) == 0x1234,
          "network stack swaps 16 bit byte order");
    check(net_htonl(0x01020304) == 0x04030201 && net_ntohl(0x04030201) == 0x01020304,
          "network stack swaps 32 bit byte order");
    u8 header[20];
    memset(header, 0, sizeof(header));
    header[0] = 0x45;
    header[2] = 0;
    header[3] = 20;
    header[8] = 64;
    header[9] = 17;
    header[12] = 10;
    header[13] = 0;
    header[14] = 2;
    header[15] = 15;
    header[16] = 10;
    header[17] = 0;
    header[18] = 2;
    header[19] = 2;
    u16 checksum = (u16)net_checksum(header, 20);
    check(checksum != 0, "ipv4 checksum computes a non-zero value");
    header[10] = (u8)(checksum >> 8);
    header[11] = (u8)(checksum & 0xFF);
    check(net_checksum(header, 20) == 0, "ipv4 checksum verifies a complete header");
    struct ipv4_header_view view;
    check(ipv4_parse_header(header, 20, &view) && view.protocol == 17 &&
          view.source == NET_IPV4(10, 0, 2, 15) && view.destination == NET_IPV4(10, 0, 2, 2),
          "ipv4 header parser extracts addresses and protocol");
    u8 corrupted[20];
    memcpy(corrupted, header, 20);
    corrupted[15] ^= 0xFF;
    check(!ipv4_parse_header(corrupted, 20, &view), "ipv4 header parser rejects bad checksums");
    check(net_parse_ip("10.0.2.2") == NET_IPV4(10, 0, 2, 2),
          "network stack parses a dotted ipv4 address");
    check(net_parse_ip("999.1.1.1") == 0 && net_parse_ip("10.0.2") == 0,
          "network stack rejects malformed ipv4 addresses");
    char ip_text[20];
    net_format_ip(NET_IPV4(192, 168, 1, 1), ip_text, sizeof(ip_text));
    check(strcmp(ip_text, "192.168.1.1") == 0, "network stack formats an ipv4 address");
    u8 frame[64];
    memset(frame, 0, sizeof(frame));
    memcpy(frame, config->mac, 6);
    frame[6] = 0x52;
    frame[7] = 0x54;
    frame[8] = 0x00;
    frame[9] = 0x12;
    frame[10] = 0x34;
    frame[11] = 0x56;
    frame[12] = 0x08;
    frame[13] = 0x06;
    struct eth_header eth;
    check(eth_parse_header(frame, 14, &eth) && eth.type == ETH_TYPE_ARP,
          "ethernet parser extracts the frame type");
    check(!eth_parse_header(frame, 8, &eth), "ethernet parser rejects short frames");
    arp_cache_clear();
    u8 gateway_mac[6] = {0x52, 0x55, 0x0A, 0x00, 0x02, 0x02};
    check(arp_cache_insert(NET_IPV4(10, 0, 2, 2), gateway_mac), "arp cache stores a new entry");
    u8 resolved[6];
    check(arp_cache_lookup(NET_IPV4(10, 0, 2, 2), resolved) && memcmp(resolved, gateway_mac, 6) == 0,
          "arp cache returns the stored mac address");
    check(!arp_cache_lookup(NET_IPV4(10, 0, 2, 99), resolved), "arp cache reports misses");
    check(arp_cache_remove(NET_IPV4(10, 0, 2, 2)) && arp_cache_count() == 0,
          "arp cache removes entries");
    u8 arp_frame[14 + ARP_PACKET_SIZE];
    memset(arp_frame, 0, sizeof(arp_frame));
    memcpy(arp_frame, config->mac, 6);
    memset(arp_frame + 6, 0x52, 6);
    arp_frame[12] = 0x08;
    arp_frame[13] = 0x06;
    u8 *arp_packet = arp_frame + 14;
    arp_packet[0] = 0;
    arp_packet[1] = 1;
    arp_packet[2] = 0x08;
    arp_packet[3] = 0x00;
    arp_packet[4] = 6;
    arp_packet[5] = 4;
    arp_packet[7] = ARP_OPERATION_REPLY;
    memcpy(arp_packet + 8, gateway_mac, 6);
    arp_packet[14] = 10;
    arp_packet[15] = 0;
    arp_packet[16] = 2;
    arp_packet[17] = 2;
    memcpy(arp_packet + 18, config->mac, 6);
    arp_packet[24] = 10;
    arp_packet[25] = 0;
    arp_packet[26] = 2;
    arp_packet[27] = 15;
    u32 replies_before = arp_replies_received();
    arp_receive(arp_frame, sizeof(arp_frame));
    check(arp_replies_received() == replies_before + 1 && arp_cache_count() == 1,
          "arp receive caches addresses from reply frames");
    u32 requests_before = arp_requests_sent();
    check(arp_send_request(NET_IPV4(10, 0, 2, 2)) == 0 &&
          arp_requests_sent() == requests_before + 1,
          "arp sends broadcast requests through the e1000");
    udp_unbind(4500);
    int udp_index = udp_bind(4500);
    check(udp_index >= 0 && udp_socket_used((u32)udp_index),
          "udp binds a local port");
    check(udp_bind(4500) < 0, "udp rejects duplicate port binds");
    const char *payload = "z-os-udp-payload";
    check(udp_inject_datagram(4500, NET_IPV4(10, 0, 2, 2), 9000, payload,
                              (u32)strlen(payload)) == 0,
          "udp queues injected datagrams");
    char received[64];
    memset(received, 0, sizeof(received));
    u32 source_ip = 0;
    u16 source_port = 0;
    int received_length = udp_recv((u32)udp_index, received, sizeof(received), 100,
                                   &source_ip, &source_port);
    check(received_length == (int)strlen(payload) && strcmp(received, payload) == 0 &&
          source_ip == NET_IPV4(10, 0, 2, 2) && source_port == 9000,
          "udp delivers datagram payloads with source information");
    check(udp_recv((u32)udp_index, received, sizeof(received), 30, NULL, NULL) < 0,
          "udp receive times out when the queue is empty");
    check(udp_unbind(4500) == 0 && !udp_socket_used((u32)udp_index),
          "udp releases bound ports");
    socket_init();
    int descriptor = socket_create(SOCKET_AF_INET, SOCKET_SOCK_DGRAM, 0);
    check(descriptor > 0, "socket layer creates a udp descriptor");
    check(socket_bind(descriptor, 0, 4600) == 0 && socket_bound_port(descriptor) == 4600,
          "socket layer binds a local port");
    check(udp_inject_datagram(4600, NET_IPV4(10, 0, 2, 2), 9010, "socket-data", 11) == 0,
          "socket layer receives injected datagrams");
    char socket_buffer[32];
    memset(socket_buffer, 0, sizeof(socket_buffer));
    int socket_length = socket_recvfrom(descriptor, socket_buffer, sizeof(socket_buffer), 100,
                                        &source_ip, &source_port);
    check(socket_length == 11 && strcmp(socket_buffer, "socket-data") == 0,
          "socket recvfrom returns the queued payload");
    check(socket_close(descriptor) == 0 && !socket_valid(descriptor),
          "socket close releases the descriptor");
    check(socket_recvfrom(descriptor, socket_buffer, sizeof(socket_buffer), 10, NULL, NULL) < 0,
          "socket rejects operations after close");
    arp_cache_clear();
    arp_cache_insert(config->ip, config->mac);
    u8 icmp_packet[16];
    memset(icmp_packet, 0, sizeof(icmp_packet));
    icmp_packet[0] = ICMP_TYPE_ECHO_REQUEST;
    icmp_packet[4] = 0x12;
    icmp_packet[5] = 0x34;
    icmp_packet[6] = 0;
    icmp_packet[7] = 1;
    memcpy(icmp_packet + 8, "Z-OSPING", 8);
    u16 icmp_checksum = (u16)net_checksum(icmp_packet, 16);
    icmp_packet[2] = (u8)(icmp_checksum >> 8);
    icmp_packet[3] = (u8)(icmp_checksum & 0xFF);
    u32 answered_before = icmp_requests_answered();
    icmp_receive(icmp_packet, 16, config->ip);
    check(icmp_requests_answered() == answered_before + 1,
          "icmp answers incoming echo requests");
    u32 frames_before = net_frames_received();
    net_receive_frame(arp_frame, sizeof(arp_frame));
    check(net_frames_received() == frames_before + 1,
          "network receive path accounts incoming frames");
}
static u32 tcp_build_segment(u8 *segment, u16 source_port, u16 destination_port,
                             u32 sequence, u32 acknowledgement, u8 flags,
                             const u8 *payload, u32 payload_length)
{
    memset(segment, 0, TCP_HEADER_SIZE + payload_length);
    segment[0] = (u8)(source_port >> 8);
    segment[1] = (u8)(source_port & 0xFF);
    segment[2] = (u8)(destination_port >> 8);
    segment[3] = (u8)(destination_port & 0xFF);
    segment[4] = (u8)(sequence >> 24);
    segment[5] = (u8)(sequence >> 16);
    segment[6] = (u8)(sequence >> 8);
    segment[7] = (u8)(sequence & 0xFF);
    segment[8] = (u8)(acknowledgement >> 24);
    segment[9] = (u8)(acknowledgement >> 16);
    segment[10] = (u8)(acknowledgement >> 8);
    segment[11] = (u8)(acknowledgement & 0xFF);
    segment[12] = (u8)((TCP_HEADER_SIZE / 4) << 4);
    segment[13] = flags;
    segment[14] = 0x08;
    segment[15] = 0x00;
    if (payload != NULL && payload_length > 0) {
        memcpy(segment + TCP_HEADER_SIZE, payload, payload_length);
    }
    u32 length = TCP_HEADER_SIZE + payload_length;
    u16 checksum = (u16)tcp_checksum(NET_IPV4(10, 0, 2, 2), net_config_get()->ip, segment, length);
    segment[16] = (u8)(checksum >> 8);
    segment[17] = (u8)(checksum & 0xFF);
    return length;
}

static void test_tcp(void)
{
    const struct net_config *config = net_config_get();
    u8 segment[TCP_HEADER_SIZE + 32];
    memset(segment, 0, sizeof(segment));
    const u8 payload[8] = {'Z', '-', 'O', 'S', 'T', 'C', 'P', '!'};
    u32 length = tcp_build_segment(segment, 1234, 80, 1000, 2000, TCP_FLAG_ACK, payload, 8);
    check(length == TCP_HEADER_SIZE + 8 &&
          tcp_checksum(NET_IPV4(10, 0, 2, 2), config->ip, segment, length) == 0,
          "tcp checksum verifies a complete segment");
    struct tcp_header_view view;
    check(tcp_parse_header(segment, length, &view) && view.source_port == 1234 &&
          view.destination_port == 80 && view.sequence == 1000 &&
          view.acknowledgement == 2000 && (view.flags & TCP_FLAG_ACK) != 0,
          "tcp header parser extracts sequence and flags");
    check(tcp_seq_less(0xFFFFFFF0U, 0x00000010U) && tcp_seq_less(0x10, 0x20) &&
          !tcp_seq_less(0x20, 0x10), "tcp sequence comparison handles wrap around");
    check(tcp_window_clamp(1000, 200, 2000, 536) == 536 &&
          tcp_window_clamp(1000, 900, 1000, 536) == 100 &&
          tcp_window_clamp(100, 100, 1000, 536) == 0,
          "tcp sliding window clamps to the smallest bound");
    tcp_init();
    tcp_test_set_transmit(false);
    int listener = tcp_listen(4000);
    check(listener > 0 && tcp_state_of(listener) == TCP_STATE_LISTEN,
          "tcp listen creates a listening connection");
    check(tcp_listen(4000) < 0, "tcp rejects duplicate listen ports");
    u32 syn_length = tcp_build_segment(segment, 9000, 4000, 1000, 0, TCP_FLAG_SYN, NULL, 0);
    tcp_receive(segment, syn_length, NET_IPV4(10, 0, 2, 2));
    check(tcp_connection_count() == 2, "tcp syn creates a pending connection");
    int child = -1;
    for (u32 index = 0; index < TCP_MAX_CONNECTIONS; index++) {
        int candidate = (int)index + 1;
        if (candidate != listener && tcp_state_of(candidate) != TCP_STATE_CLOSED &&
            tcp_local_port(candidate) == 4000) {
            child = candidate;
        }
    }
    check(child > 0 && tcp_state_of(child) == TCP_STATE_SYN_RECV,
          "tcp syn moves the connection to syn received");
    u32 child_sequence = tcp_test_send_sequence(child);
    u32 ack_length = tcp_build_segment(segment, 9000, 4000, 1001, child_sequence, TCP_FLAG_ACK, NULL, 0);
    tcp_receive(segment, ack_length, NET_IPV4(10, 0, 2, 2));
    check(tcp_state_of(child) == TCP_STATE_ESTABLISHED,
          "tcp handshake completes into the established state");
    int accepted = tcp_accept(listener, 100);
    check(accepted == child && tcp_state_of(accepted) == TCP_STATE_ESTABLISHED,
          "tcp accept returns the established connection");
    u32 receive_sequence = tcp_test_receive_sequence(accepted);
    u32 data_length = tcp_build_segment(segment, 9000, 4000, receive_sequence, child_sequence,
                                        TCP_FLAG_ACK | TCP_FLAG_PSH, (const u8 *)"HELLO", 5);
    tcp_receive(segment, data_length, NET_IPV4(10, 0, 2, 2));
    char received[16];
    memset(received, 0, sizeof(received));
    int received_length = tcp_recv(accepted, received, sizeof(received), 100);
    check(received_length == 5 && strcmp(received, "HELLO") == 0,
          "tcp receive reassembles in order payload data");
    u32 fin_sequence = tcp_test_receive_sequence(accepted);
    u32 fin_length = tcp_build_segment(segment, 9000, 4000, fin_sequence, child_sequence,
                                       TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    tcp_receive(segment, fin_length, NET_IPV4(10, 0, 2, 2));
    check(tcp_state_of(accepted) == TCP_STATE_CLOSE_WAIT,
          "tcp fin moves the connection to close wait");
    check(tcp_recv(accepted, received, sizeof(received), 100) == 0,
          "tcp receive reports end of stream after fin");
    check(tcp_close(accepted) == 0, "tcp close tears down the accepted connection");
    check(tcp_close(listener) == 0, "tcp close tears down the listener");
    u32 errors_before = tcp_checksum_errors();
    u8 corrupted[TCP_HEADER_SIZE + 8];
    memcpy(corrupted, segment, sizeof(corrupted));
    corrupted[TCP_HEADER_SIZE] ^= 0xFF;
    tcp_receive(corrupted, TCP_HEADER_SIZE + 8, NET_IPV4(10, 0, 2, 2));
    check(tcp_checksum_errors() == errors_before + 1,
          "tcp receive rejects segments with bad checksums");
    int resetting = tcp_test_open_syn_sent(NET_IPV4(10, 0, 2, 2), 8080);
    check(resetting > 0 && tcp_state_of(resetting) == TCP_STATE_SYN_SENT,
          "tcp active open starts in syn sent");
    u32 rst_length = tcp_build_segment(segment, 8080, (u16)tcp_local_port(resetting), 5000, 0,
                                       TCP_FLAG_RST, NULL, 0);
    tcp_receive(segment, rst_length, NET_IPV4(10, 0, 2, 2));
    check(tcp_state_of(resetting) == TCP_STATE_CLOSED,
          "tcp reset closes a pending connection");
    tcp_test_set_transmit(true);
    u8 query[DNS_MAX_MESSAGE];
    memset(query, 0, sizeof(query));
    u32 query_length = dns_build_query("example.com", query, sizeof(query));
    check(query_length == 29 && query[12] == 7 && query[20] == 3 &&
          memcmp(query + 13, "example", 7) == 0 && memcmp(query + 21, "com", 3) == 0,
          "dns builds a valid a record query");
    u8 response[DNS_MAX_MESSAGE];
    memset(response, 0, sizeof(response));
    memcpy(response, query, 12);
    response[2] = 0x81;
    response[3] = 0x80;
    response[6] = 0;
    response[7] = 1;
    u32 response_length = 12;
    memcpy(response + response_length, query + 12, query_length - 12);
    response_length += query_length - 12;
    response[response_length++] = 0xC0;
    response[response_length++] = 0x0C;
    response[response_length++] = 0;
    response[response_length++] = 1;
    response[response_length++] = 0;
    response[response_length++] = 1;
    response[response_length++] = 0;
    response[response_length++] = 0;
    response[response_length++] = 0;
    response[response_length++] = 60;
    response[response_length++] = 0;
    response[response_length++] = 4;
    response[response_length++] = 93;
    response[response_length++] = 184;
    response[response_length++] = 216;
    response[response_length++] = 34;
    u32 resolved = 0;
    check(dns_parse_response(response, response_length, 0x2000, &resolved) &&
          resolved == NET_IPV4(93, 184, 216, 34),
          "dns parses an a record answer with name compression");
    check(!dns_parse_response(response, response_length, 0x2001, &resolved),
          "dns rejects responses with mismatched identifiers");
}
static void test_internet(void)
{
    struct net_config saved_config = *net_config_get();
    u8 message[DHCP_MAX_MESSAGE];
    memset(message, 0, sizeof(message));
    u32 length = dhcp_build_discover(message, sizeof(message), 0x12345678U);
    check(length > 240 && message[0] == 1 && message[1] == 1 && message[2] == 6,
          "dhcp discover builds a client message header");
    check(message[236] == 0x63 && message[237] == 0x82 && message[238] == 0x53 &&
          message[239] == 0x63, "dhcp discover carries the magic cookie");
    bool discover_type = false;
    for (u32 index = 240; index + 2 < length; index++) {
        if (message[index] == DHCP_OPTION_MESSAGE_TYPE && message[index + 1] == 1 &&
            message[index + 2] == DHCP_MESSAGE_DISCOVER) {
            discover_type = true;
        }
    }
    check(discover_type, "dhcp discover requests an offer");
    memset(message, 0, sizeof(message));
    length = dhcp_build_request(message, sizeof(message), 0x12345678U, NET_IPV4(10, 0, 2, 20),
                                NET_IPV4(10, 0, 2, 2));
    bool requested_option = false;
    bool server_option = false;
    bool request_type = false;
    for (u32 index = 240; index + 2 < length; index++) {
        if (message[index] == DHCP_OPTION_REQUESTED_IP && message[index + 1] == 4) {
            requested_option = true;
        }
        if (message[index] == DHCP_OPTION_SERVER_ID && message[index + 1] == 4) {
            server_option = true;
        }
        if (message[index] == DHCP_OPTION_MESSAGE_TYPE && message[index + 1] == 1 &&
            message[index + 2] == DHCP_MESSAGE_REQUEST) {
            request_type = true;
        }
    }
    check(requested_option && server_option && request_type,
          "dhcp request carries the requested address and server");
    u8 offer[DHCP_HEADER_SIZE + 64];
    memset(offer, 0, sizeof(offer));
    offer[0] = 2;
    offer[1] = 1;
    offer[2] = 6;
    offer[4] = 0x12;
    offer[5] = 0x34;
    offer[6] = 0x56;
    offer[7] = 0x78;
    offer[16] = 10;
    offer[17] = 0;
    offer[18] = 2;
    offer[19] = 20;
    offer[236] = 0x63;
    offer[237] = 0x82;
    offer[238] = 0x53;
    offer[239] = 0x63;
    u32 position = DHCP_HEADER_SIZE;
    offer[position++] = DHCP_OPTION_MESSAGE_TYPE;
    offer[position++] = 1;
    offer[position++] = DHCP_MESSAGE_OFFER;
    offer[position++] = DHCP_OPTION_SUBNET_MASK;
    offer[position++] = 4;
    offer[position++] = 255;
    offer[position++] = 255;
    offer[position++] = 255;
    offer[position++] = 0;
    offer[position++] = DHCP_OPTION_ROUTER;
    offer[position++] = 4;
    offer[position++] = 10;
    offer[position++] = 0;
    offer[position++] = 2;
    offer[position++] = 2;
    offer[position++] = DHCP_OPTION_DNS;
    offer[position++] = 4;
    offer[position++] = 10;
    offer[position++] = 0;
    offer[position++] = 2;
    offer[position++] = 3;
    offer[position++] = DHCP_OPTION_SERVER_ID;
    offer[position++] = 4;
    offer[position++] = 10;
    offer[position++] = 0;
    offer[position++] = 2;
    offer[position++] = 2;
    offer[position++] = DHCP_OPTION_LEASE_TIME;
    offer[position++] = 4;
    offer[position++] = 0;
    offer[position++] = 0;
    offer[position++] = 0x0E;
    offer[position++] = 0x10;
    offer[position++] = DHCP_OPTION_END;
    struct dhcp_lease lease;
    u8 message_type = 0;
    check(dhcp_parse_offer(offer, position, 0x12345678U, &lease, &message_type) &&
          message_type == DHCP_MESSAGE_OFFER && lease.address == NET_IPV4(10, 0, 2, 20) &&
          lease.netmask == NET_IPV4(255, 255, 255, 0) &&
          lease.gateway == NET_IPV4(10, 0, 2, 2) && lease.dns == NET_IPV4(10, 0, 2, 3) &&
          lease.server == NET_IPV4(10, 0, 2, 2) && lease.lease_seconds == 3600,
          "dhcp parses offer addresses and lease options");
    check(!dhcp_parse_offer(offer, position, 0x12345679U, &lease, &message_type),
          "dhcp rejects offers with another transaction identifier");
    check(dhcp_apply_lease(&lease) && net_config_get()->ip == NET_IPV4(10, 0, 2, 20),
          "dhcp applies the received lease to the network configuration");
    *(struct net_config *)net_config_get() = saved_config;
    arp_cache_clear();
    dns_cache_clear();
    check(dns_cache_insert("example.com", NET_IPV4(93, 184, 216, 34), 60, false),
          "dns cache stores positive answers");
    u32 cached_address = 0;
    bool cached_negative = false;
    check(dns_cache_lookup("example.com", &cached_address, &cached_negative) &&
          !cached_negative && cached_address == NET_IPV4(93, 184, 216, 34),
          "dns cache lookup returns stored addresses");
    check(!dns_cache_lookup("missing.example", &cached_address, &cached_negative),
          "dns cache reports misses for unknown names");
    check(dns_cache_insert("bad.example", 0, 30, true), "dns cache stores negative answers");
    check(dns_cache_lookup("bad.example", &cached_address, &cached_negative) && cached_negative,
          "dns cache lookup reports negative entries");
    check(dns_cache_count() == 2 && dns_cache_negative_count() == 1,
          "dns cache tracks positive and negative entry counts");
    dns_cache_expire_all();
    check(dns_cache_count() == 0, "dns cache expires entries");
    dns_cache_clear();
    check(dns_cache_insert("cached.example", NET_IPV4(1, 2, 3, 4), 60, false),
          "dns cache accepts a warm entry for resolution");
    u32 warm_address = 0;
    check(dns_resolve("cached.example", &warm_address, 100) == 0 &&
          warm_address == NET_IPV4(1, 2, 3, 4) && dns_cache_hits() == 1 &&
          dns_cache_misses() == 0,
          "dns resolve returns cached answers without a query");
    check(dns_cache_insert("blocked.example", 0, 30, true),
          "dns cache accepts a negative entry for resolution");
    check(dns_resolve("blocked.example", &warm_address, 100) != 0 && dns_cache_hits() == 2,
          "dns resolve honors cached negative answers");
    const char *http_ok = "HTTP/1.0 200 OK\r\nContent-Length: 11\r\nContent-Type: text/plain\r\n"
                          "\r\nhello world";
    struct http_response response;
    check(http_parse_response(http_ok, (u32)strlen(http_ok), &response) == 0 &&
          response.status == 200 && response.body_length == 11 &&
          response.declared_length == 11 && response.complete,
          "http parses status line and content length");
    const char *http_missing = "HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    check(http_parse_response(http_missing, (u32)strlen(http_missing), &response) == 0 &&
          response.status == 404 && response.body_length == 0,
          "http parses error responses");
    const char *http_truncated = "HTTP/1.0 200 OK\r\nContent-Length: 100\r\n\r\npartial";
    check(http_parse_response(http_truncated, (u32)strlen(http_truncated), &response) == 0 &&
          !response.complete && response.body_length == 7,
          "http marks short bodies as incomplete");
    check(http_parse_response("garbage", 7, &response) != 0,
          "http rejects responses without a status line");
    char host[64];
    char path[64];
    u16 port = 0;
    check(http_parse_url("http:" "/" "/10.0.2.2:8080/index.html", host, sizeof(host), &port, path,
                         sizeof(path)) == 0 &&
          strcmp(host, "10.0.2.2") == 0 && port == 8080 && strcmp(path, "/index.html") == 0,
          "http parses a url with a port and path");
    check(http_parse_url("http:" "/" "/example.com", host, sizeof(host), &port, path, sizeof(path)) == 0 &&
          strcmp(host, "example.com") == 0 && port == 80 && strcmp(path, "/") == 0,
          "http parses a url without a path");
    check(http_parse_url("https:" "/" "/example.com/", host, sizeof(host), &port, path, sizeof(path)) != 0,
          "http rejects non http schemes");
    socket_init();
    int owner_socket = socket_create_for_owner(777, SOCKET_AF_INET, SOCKET_SOCK_DGRAM, 0);
    check(owner_socket > 0 && socket_owner(owner_socket) == 777 &&
          socket_owner_count(777) == 1,
          "sockets record the owning process");
    check(socket_bind(owner_socket, 0, 4900) == 0, "owned socket binds a udp port");
    socket_cleanup_process(777);
    check(!socket_valid(owner_socket) && socket_owner_count(777) == 0,
          "process cleanup closes owned sockets");
    check(udp_bind(4900) >= 0, "process cleanup releases the udp port");
    udp_unbind(4900);
    u32 tcp_before = tcp_connection_count();
    int owner_listener = socket_create_for_owner(778, SOCKET_AF_INET, SOCKET_SOCK_STREAM, 0);
    check(owner_listener > 0 && socket_bind(owner_listener, 0, 4901) == 0 &&
          tcp_connection_count() == tcp_before + 1,
          "owned stream socket opens a tcp listener");
    socket_cleanup_process(778);
    check(tcp_connection_count() == tcp_before && !socket_valid(owner_listener),
          "process cleanup closes owned tcp connections");
    socket_cleanup_process(999);
    check(socket_owner_count(999) == 0, "process cleanup ignores unknown owners");
}
static void test_platform(void)
{
    check(security_smep_supported() == security_smep_active(),
          "smep activation matches cpu capability");
    check(security_smap_supported() == security_smap_active(),
          "smap activation matches cpu capability");
    check((read_cr4() & SECURITY_CR4_SMEP) != 0 || !security_smep_supported(),
          "cr4 smep bit reflects the cpu capability");
    check((read_cr4() & SECURITY_CR4_SMAP) != 0 || !security_smap_supported(),
          "cr4 smap bit reflects the cpu capability");
    check(heap_kaslr_enabled(), "heap kaslr is enabled at boot");
    check(heap_kaslr_slot() < 64, "heap kaslr slot stays inside its bounds");
    check(heap_base() >= 0x60000000ULL && heap_base() < 0x60400000ULL,
          "heap base stays inside the reserved randomisation window");
    check(heap_base() == 0x60000000ULL + heap_kaslr_slot() * 0x10000ULL,
          "heap base matches the selected kaslr slot");
    struct task_base thread_task;
    struct task_base process_task;
    task_model_init();
    task_base_init(&thread_task, TASK_KIND_THREAD, 7);
    check(task_base_is_thread(&thread_task) && thread_task.state == TASK_STATE_READY &&
          thread_task.quantum == 1, "task base initialises thread descriptors");
    task_base_init(&process_task, TASK_KIND_PROCESS, 9);
    check(task_base_is_process(&process_task) && task_base_threads() == 1 &&
          task_base_processes() == 1, "task base counts threads and processes");
    check(task_base_total() == 2 && task_base_threads() == 1, "task base reports unified totals");
    task_base_set_state(&thread_task, TASK_STATE_BLOCKED);
    task_base_set_quantum(&thread_task, 5);
    check(thread_task.state == TASK_STATE_BLOCKED && thread_task.quantum == 5 &&
          strcmp(task_state_name(TASK_STATE_BLOCKED), "blocked") == 0,
          "task base tracks state and quantum");
    check(strcmp(task_kind_name(TASK_KIND_PROCESS), "process") == 0,
          "task base names process descriptors");
    socket_init();
    int udp_descriptor = socket_create_for_owner(900, SOCKET_AF_INET, SOCKET_SOCK_DGRAM, 0);
    check(udp_descriptor > 0 && socket_bind(udp_descriptor, 0, 4950) == 0,
          "select test socket binds a port");
    u32 ready_read = 0;
    u32 ready_write = 0;
    check(socket_select(1U << udp_descriptor, 0, 50, &ready_read, &ready_write) == 0 &&
          ready_read == 0, "select times out on an idle socket");
    check(udp_inject_datagram(4950, NET_IPV4(10, 0, 2, 2), 8000, "sel", 3) == 0,
          "select test queues a datagram");
    check(socket_select(1U << udp_descriptor, 0, 100, &ready_read, &ready_write) > 0 &&
          (ready_read & (1U << udp_descriptor)) != 0,
          "select reports readable sockets");
    check(socket_write_ready(udp_descriptor), "udp sockets report writable");
    check(socket_set_nonblocking(udp_descriptor, true) == 0 &&
          socket_is_nonblocking(udp_descriptor), "nonblocking mode can be enabled");
    char datagram[16];
    memset(datagram, 0, sizeof(datagram));
    check(socket_recvfrom(udp_descriptor, datagram, sizeof(datagram), 0, NULL, NULL) == 3 &&
          memcmp(datagram, "sel", 3) == 0, "nonblocking receive returns queued data");
    check(socket_recvfrom(udp_descriptor, datagram, sizeof(datagram), 0, NULL, NULL) ==
          -SOCKET_EAGAIN, "nonblocking receive reports eagain when empty");
    socket_set_nonblocking(udp_descriptor, false);
    check(socket_select(0, 1U << udp_descriptor, 20, &ready_read, &ready_write) >= 0 &&
          (ready_write & (1U << udp_descriptor)) != 0, "select reports writable sockets");
    check(socket_select(0xFFFFFFFFU, 0, 10, &ready_read, &ready_write) >= 0,
          "select ignores descriptor bits outside the table");
    check(!tcp_accept_pending(-1), "tcp accept readiness rejects invalid identifiers");
    socket_cleanup_process(900);
    check(!socket_valid(udp_descriptor), "select test socket is reclaimed");
}
static void test_hardening(void)
{
    extern u64 __stack_chk_guard;
    u64 used_before = kstack_used();
    u64 first_stack = kstack_alloc(16 * 1024);
    check(first_stack != 0 && kstack_is_managed(first_stack) && kstack_used() == used_before + 1,
          "guarded kernel stack allocation is tracked");
    u64 guard = kstack_guard_address(first_stack);
    check(guard + 4096 == first_stack && vmm_get_physical(guard) == 0,
          "kernel stack guard page is unmapped");
    u64 first_flags = vmm_query_flags(first_stack);
    check((first_flags & PTE_PRESENT) != 0 && (first_flags & PTE_NO_EXECUTE) != 0,
          "kernel stack pages are non executable");
    check(kstack_free(first_stack) && kstack_used() == used_before &&
          vmm_get_physical(first_stack) == 0 && vmm_get_physical(guard) == 0,
          "kernel stack free releases pages and keeps the guard");
    u64 second_stack = kstack_alloc(32 * 1024);
    u64 third_stack = kstack_alloc(16 * 1024);
    check(second_stack != 0 && third_stack != 0 && second_stack != third_stack &&
          kstack_used() == used_before + 2, "kernel stacks allocate distinct guarded slots");
    kstack_free(second_stack);
    kstack_free(third_stack);
    check(security_canary() != 0, "stack canary is initialised from boot entropy");
    check(__stack_chk_guard == security_canary(), "stack guard symbol matches the runtime canary");
    check(security_nx_supported() == security_nx_active(),
          "nx activation matches cpu capability");
    check(kernel_protect_active() && kernel_protected_pages() > 0,
          "kernel section protection is active");
    u64 text_flags = vmm_query_flags((u64)(usize)&security_init);
    check((text_flags & PTE_PRESENT) != 0 && (text_flags & PTE_WRITABLE) == 0 &&
          (text_flags & PTE_NO_EXECUTE) == 0,
          "kernel text is read only and executable");
    u64 data_flags = vmm_query_flags((u64)(usize)&__stack_chk_guard);
    check((data_flags & PTE_WRITABLE) != 0 && (data_flags & PTE_NO_EXECUTE) != 0,
          "kernel data is writable and non executable");
    check((read_cr0() & (1ULL << 16)) != 0, "write protect bit is enabled in cr0");
    void *heap_block = kmalloc(64);
    check(heap_block != NULL && (vmm_query_flags((u64)(usize)heap_block) & PTE_NO_EXECUTE) != 0,
          "heap pages are non executable");
    kfree(heap_block);
    check(KSTACK_WINDOW_BASE >= HEAP_BASE_DEFAULT + HEAP_WINDOW_SIZE,
          "guarded stack window sits above the heap window");
    socket_init();
    int descriptor = socket_create_for_owner(910, SOCKET_AF_INET, SOCKET_SOCK_DGRAM, 0);
    check(descriptor > 0 && socket_bind(descriptor, 0, 4975) == 0, "poll test socket binds");
    struct pollfd_user entry;
    entry.fd = descriptor;
    entry.events = SOCKET_POLLIN;
    entry.revents = 0xFFFF;
    check(syscall_poll_register(&entry, 1, 50) == 0 && entry.revents == 0,
          "poll reports a timeout on an idle socket");
    check(udp_inject_datagram(4975, NET_IPV4(10, 0, 2, 2), 8200, "poll", 4) == 0,
          "poll test queues a datagram");
    entry.revents = 0;
    check(syscall_poll_register(&entry, 1, 500) >= 1 &&
          (entry.revents & SOCKET_POLLIN) != 0, "poll reports readable datagram sockets");
    entry.revents = 0;
    entry.events = SOCKET_POLLOUT;
    check(syscall_poll_register(&entry, 1, 50) >= 1 &&
          (entry.revents & SOCKET_POLLOUT) != 0, "poll reports writable datagram sockets");
    struct pollfd_user entries[2];
    entries[0].fd = descriptor;
    entries[0].events = SOCKET_POLLIN;
    entries[0].revents = 0;
    entries[1].fd = -1;
    entries[1].events = SOCKET_POLLIN;
    entries[1].revents = 0xFFFF;
    check(syscall_poll_register(entries, 2, 100) >= 1 && entries[1].revents == 0,
          "poll ignores negative descriptor entries");
    entries[0].fd = -1;
    check(syscall_poll_register(entries, 2, 20) == 0, "poll times out when every entry is idle");
    struct pollfd_user invalid;
    invalid.fd = 999;
    invalid.events = SOCKET_POLLIN;
    invalid.revents = 0;
    check(syscall_poll_register(&invalid, 1, 20) >= 1 &&
          (invalid.revents & SOCKET_POLLERR) != 0, "poll reports invalid descriptors as errors");
    socket_cleanup_process(910);
    check(!socket_valid(descriptor), "poll test socket is reclaimed");
}
static void test_isolation(void)
{
    extern u8 __data_start[];
    extern u8 __bss_start[];
    u32 sample = 0;
    kprintf("RWXDBG count=%u sample=%08x\n", vmm_audit_rwx(&sample), sample);
    check(vmm_audit_rwx(NULL) == 0, "page tables contain no writable executable pages");
    check(vmm_audit_rwx(&sample) == 0 && sample == 0,
          "rwx audit reports a zero sample address");
    check(((u64)(usize)__data_start & 0xFFF) == 0 &&
          ((u64)(usize)__bss_start & 0xFFF) == 0,
          "kernel data and bss sections are page aligned");
    check((vmm_query_flags(0x8000000ULL) & PTE_NO_EXECUTE) != 0,
          "identity mapped low memory is non executable");
    check((vmm_query_flags(0x20000000ULL) & PTE_NO_EXECUTE) != 0,
          "identity mapped high memory is non executable");
    check((vmm_query_flags(0xFFFFFFFF80000000ULL) & PTE_NO_EXECUTE) != 0,
          "kernel direct map is non executable");
    check((vmm_query_flags(0xFFFFFFFF40000000ULL) & PTE_NO_EXECUTE) != 0,
          "device memory window is non executable");
    check(kernel_protected_pages() >= 20, "kernel protection covers image pages");
    u64 first_stack = kstack_alloc(16 * 1024);
    check(first_stack != 0, "isolation test allocates a guarded stack");
    u64 guard = kstack_guard_address(first_stack);
    check(kstack_guard_fault(guard) && kstack_guard_fault(guard + 0x9000ULL),
          "guard fault classifier recognises guard pages");
    check(kstack_guard_fault(first_stack) == false &&
          kstack_guard_fault(first_stack + PAGE_SIZE) == false,
          "guard fault classifier rejects mapped stack pages");
    check(!kstack_guard_fault(0x200000ULL) && !kstack_guard_fault(0x60000000ULL),
          "guard fault classifier rejects kernel and heap addresses");
    check(kstack_free(first_stack), "isolation test releases the guarded stack");
    check(KSTACK_WINDOW_BASE >= HEAP_BASE_DEFAULT + HEAP_WINDOW_SIZE,
          "guarded stack window is separated from the heap window");
    check(kstack_total() == KSTACK_SLOT_COUNT, "guarded stack slot capacity is fixed");
    check((read_cr0() & (1ULL << 16)) != 0, "write protect remains enabled");
    socket_init();
    check(socket_events(999) == SOCKET_POLLERR, "socket events report invalid descriptors");
    int descriptor = socket_create_for_owner(920, SOCKET_AF_INET, SOCKET_SOCK_DGRAM, 0);
    check(descriptor > 0 && socket_bind(descriptor, 0, 4980) == 0,
          "isolation poll socket binds a port");
    check(udp_inject_datagram(4980, NET_IPV4(10, 0, 2, 2), 8300, "iso", 3) == 0,
          "isolation poll socket queues a datagram");
    check(socket_poll_events((u32)descriptor, 0) == 0,
          "poll event masks filter unrequested readiness");
    struct pollfd_user entry;
    entry.fd = descriptor;
    entry.events = 0;
    entry.revents = 0;
    check(syscall_poll_register(&entry, 1, 20) == 0 && entry.revents == 0,
          "poll with an empty event mask times out");
    check(syscall_poll_register(&entry, 0, 10) == 0, "poll with zero entries returns immediately");
    check(kstack_alloc(64 * 1024) == 0, "guarded stack rejects oversized requests");
    check(!kstack_free(0x12345000ULL), "guarded stack free rejects unknown addresses");
    socket_cleanup_process(920);
    check(vmm_audit_rwx(NULL) == 0, "rwx audit remains clean after allocation churn");
    check(heap_kaslr_enabled() && heap_kaslr_slot() >= 1 && heap_kaslr_slot() < 64,
          "heap kaslr selects a non zero bounded slot");
    check(heap_base() == HEAP_BASE_DEFAULT + heap_kaslr_slot() * 0x10000ULL,
          "heap base derives from the kaslr slot");
    check((vmm_query_flags(heap_base()) & PTE_NO_EXECUTE) != 0,
          "heap base page is non executable");
    u64 stack_probe = kstack_alloc(16 * 1024);
    check(stack_probe != 0 && (vmm_query_flags(stack_probe) & PTE_NO_EXECUTE) != 0,
          "guarded stack pages are non executable");
    kstack_free(stack_probe);
    check(vmm_audit_rwx(NULL) == 0, "rwx audit stays clean after guarded stack churn");
    check((vmm_query_flags(0x100000ULL) & PTE_WRITABLE) == 0,
          "kernel image text page is read only");
    check((vmm_query_flags(0x100000ULL) & PTE_NO_EXECUTE) == 0,
          "kernel image text page is executable");
    static u32 data_probe = 1;
    check((vmm_query_flags((u64)(usize)&data_probe) & PTE_NO_EXECUTE) != 0,
          "kernel data page is non executable");
    void *probe_block = kmalloc(64);
    check(probe_block != NULL && (vmm_query_flags((u64)(usize)probe_block) & PTE_WRITABLE) != 0 &&
          (vmm_query_flags((u64)(usize)probe_block) & PTE_NO_EXECUTE) != 0,
          "heap allocation pages are writable and non executable");
    kfree(probe_block);
    socket_init();
    int probe_socket = socket_create_for_owner(930, SOCKET_AF_INET, SOCKET_SOCK_DGRAM, 0);
    check(probe_socket > 0 && socket_bind(probe_socket, 0, 4990) == 0,
          "audit probe socket binds a port");
    socket_cleanup_process(930);
    check(vmm_audit_rwx(NULL) == 0, "rwx audit stays clean after socket churn");
    check((vmm_query_flags(0x1000ULL) & PTE_NO_EXECUTE) != 0,
          "boot page tables are non executable");
    check((vmm_query_flags(0x0ULL) & PTE_NO_EXECUTE) != 0,
          "real mode identity page is non executable");
    pmm_free_frame(pmm_alloc_frame());
    check(vmm_audit_rwx(NULL) == 0, "rwx audit stays clean after mapping probes");
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
    test_vga_text_restore();
    test_network();
    test_tcp();
    test_internet();
    test_platform();
    test_hardening();
    test_isolation();
    kprintf("ZTEST SUMMARY passed=%u failed=%u\n", tests_passed, tests_failed);
    if (tests_failed == 0) {
        kprintf("ZTEST RESULT PASS\n");
    } else {
        kprintf("ZTEST RESULT FAIL\n");
    }
    sched_sleep_ms(200);
    qemu_debug_exit(tests_failed == 0 ? 0x10 : 0x11);
}
