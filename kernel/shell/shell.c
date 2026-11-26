#include "shell.h"
#include "ata.h"
#include "console.h"
#include "arp.h"
#include "desktop.h"
#include "e1000.h"
#include "icmp.h"
#include "ipv4.h"
#include "net.h"
#include "socket.h"
#include "udp.h"
#include "syscall.h"
#include "fb.h"
#include "font.h"
#include "gfx.h"
#include "mouse.h"
#include "heap.h"
#include "input.h"
#include "kernel.h"
#include "lib.h"
#include "pmm.h"
#include "power.h"
#include "process.h"
#include "rtc.h"
#include "sched.h"
#include "string.h"
#include "thread.h"
#include "timer.h"
#include "user.h"
#include "vfs.h"
#include "wm.h"
#include "zfs.h"

#define SHELL_LINE_MAX 256
#define SHELL_ARG_MAX 16
#define SHELL_HISTORY_MAX 8

static struct vfs_node *shell_cwd = NULL;
static char shell_cwd_path[VFS_PATH_MAX];
static char shell_history[SHELL_HISTORY_MAX][SHELL_LINE_MAX];
static u32 shell_history_count = 0;

static void print_prompt(void) {
    if (shell_cwd == NULL || vfs_build_path(shell_cwd, shell_cwd_path, sizeof(shell_cwd_path)) != VFS_OK) {
        strcpy(shell_cwd_path, "/");
    }
    kprintf("z-os:%s# ", shell_cwd_path);
}

static void add_history(const char *line) {
    if (line[0] == 0) {
        return;
    }
    if (shell_history_count > 0 && strcmp(shell_history[shell_history_count - 1], line) == 0) {
        return;
    }
    if (shell_history_count == SHELL_HISTORY_MAX) {
        for (u32 index = 1; index < SHELL_HISTORY_MAX; index++) {
            strcpy(shell_history[index - 1], shell_history[index]);
        }
        shell_history_count--;
    }
    strncpy(shell_history[shell_history_count], line, SHELL_LINE_MAX - 1);
    shell_history[shell_history_count][SHELL_LINE_MAX - 1] = 0;
    shell_history_count++;
}

static int shell_read_line(char *buffer, usize capacity) {
    usize length = 0;
    buffer[0] = 0;
    print_prompt();
    for (;;) {
        int key = input_getchar();
        if (key == '\n' || key == '\r') {
            console_putc('\n');
            buffer[length] = 0;
            return (int)length;
        }
        if (key == 3) {
            console_write("^C\n");
            buffer[0] = 0;
            return -1;
        }
        if (key == 12) {
            console_clear();
            print_prompt();
            console_write_n(buffer, length);
            continue;
        }
        if (key == '\b' || key == 127) {
            if (length > 0) {
                length--;
                console_write("\b \b");
            }
            continue;
        }
        if (key == KEY_UP || key == KEY_DOWN || key == KEY_LEFT || key == KEY_RIGHT ||
            key == KEY_HOME || key == KEY_END || key == KEY_DELETE) {
            continue;
        }
        if (key < 32 || key > 126) {
            continue;
        }
        if (length + 1 < capacity) {
            buffer[length++] = (char)key;
            console_putc((char)key);
        }
    }
}

static int tokenize(char *line, char *arguments[SHELL_ARG_MAX]) {
    int count = 0;
    char *cursor = line;
    while (*cursor != 0 && count < SHELL_ARG_MAX) {
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor == 0) {
            break;
        }
        if (*cursor == '"') {
            cursor++;
            arguments[count++] = cursor;
            while (*cursor != 0 && *cursor != '"') {
                cursor++;
            }
            if (*cursor == '"') {
                *cursor++ = 0;
            }
        } else {
            arguments[count++] = cursor;
            while (*cursor != 0 && *cursor != ' ' && *cursor != '\t') {
                cursor++;
            }
            if (*cursor != 0) {
                *cursor++ = 0;
            }
        }
    }
    if (count < SHELL_ARG_MAX) {
        arguments[count] = NULL;
    }
    return count;
}

static void report_vfs_error(int code) {
    switch (code) {
        case VFS_ERROR_NOT_FOUND:
            kprintf("error: path not found\n");
            break;
        case VFS_ERROR_EXISTS:
            kprintf("error: path already exists\n");
            break;
        case VFS_ERROR_NO_SPACE:
            kprintf("error: no space available\n");
            break;
        case VFS_ERROR_NOT_EMPTY:
            kprintf("error: directory is not empty\n");
            break;
        case VFS_ERROR_TYPE:
            kprintf("error: operation is not valid for this node type\n");
            break;
        case VFS_ERROR_INVALID:
            kprintf("error: invalid path\n");
            break;
        default:
            kprintf("error: operation failed (%d)\n", code);
            break;
    }
}

static void join_arguments(int argc, char *arguments[], int start, char *buffer, usize size, bool newline) {
    usize position = 0;
    for (int index = start; index < argc; index++) {
        if (index > start) {
            if (position + 1 >= size) {
                break;
            }
            buffer[position++] = ' ';
        }
        usize length = strlen(arguments[index]);
        for (usize offset = 0; offset < length && position + 1 < size; offset++) {
            buffer[position++] = arguments[index][offset];
        }
    }
    if (newline && position + 1 < size) {
        buffer[position++] = '\n';
    }
    buffer[position] = 0;
}

static void command_help(void) {
    kprintf("Z-OS shell commands\n");
    kprintf("  help                 show this command list\n");
    kprintf("  about, version       operating system identity\n");
    kprintf("  uname                kernel and machine information\n");
    kprintf("  clear                clear the VGA console\n");
    kprintf("  echo <text>          print text\n");
    kprintf("  pwd                  print working directory\n");
    kprintf("  cd <path>            change working directory\n");
    kprintf("  ls [path]            list a directory\n");
    kprintf("  cat <path>           print a file\n");
    kprintf("  mkdir <path>         create a directory\n");
    kprintf("  rmdir <path>         remove an empty directory\n");
    kprintf("  touch <path>         create an empty file\n");
    kprintf("  write <path> <text>  replace a file with text\n");
    kprintf("  append <path> <text> append text to a file\n");
    kprintf("  rm <path>            remove a file or empty directory\n");
    kprintf("  mv <old> <new>       rename or move a path\n");
    kprintf("  cp <source> <dest>   copy a file\n");
    kprintf("  stat <path>          show node metadata\n");
    kprintf("  hexdump <path>       display file bytes\n");
    kprintf("  df                   filesystem capacity summary\n");
    kprintf("  mem, free            physical and kernel memory summary\n");
    kprintf("  uptime               time since boot\n");
    kprintf("  date                 real-time clock\n");
    kprintf("  ps, threads          list kernel threads and processes\n");
    kprintf("  sleep <ms>           sleep for milliseconds\n");
    kprintf("  history              show command history\n");
    kprintf("  rand [bytes]         print random bytes\n");
    kprintf("  usertest             run the ELF64 ring-3 program as a process\n");
    kprintf("  schedtest            run two user processes under time slicing\n");
    kprintf("  forktest             run the fork and blocking wait program\n");
    kprintf("  postest              run the posix fd, signal and wait test program\n");
    kprintf("  guitest2             run the font, mouse and compositor test program\n");
    kprintf("  gfx [demo|test|off]  framebuffer, gfx and window manager control\n");
    kprintf("  diskinfo             ata block device and zfs status\n");
    kprintf("  format               format the ata disk with zfs\n");
    kprintf("  zmount               mount zfs and populate /disk\n");
    kprintf("  zls                  list zfs files on disk\n");
    kprintf("  zsave <name> <text>  write a file to zfs\n");
    kprintf("  zcat <name>          read a file from zfs\n");
    kprintf("  zrm <name>           remove a file from zfs\n");
    kprintf("  zsync                write /disk contents back to zfs\n");
    kprintf("  gui                  enter the graphical desktop session\n");
    kprintf("  exit                 leave the graphical desktop session\n");
    kprintf("  mouse                report ps2 mouse state and packet counters\n");
    kprintf("  netinfo              report e1000, mac, ip and protocol counters\n");
    kprintf("  arp <ip>             resolve an ipv4 address to a mac address\n");
    kprintf("  ping <ip>            send four icmp echo requests\n");
    kprintf("  udp-echo <port>      bind a udp port and echo one datagram\n");
    kprintf("  reboot               reset the machine\n");
    kprintf("  shutdown             power off or halt\n");
}

static void command_about(void) {
    kprintf("Z-OS %s %s\n", Z_OS_VERSION, Z_OS_CODENAME);
    kprintf("A compact x86_64 operating system with a preemptive kernel,\n");
    kprintf("per-process address spaces, a shared high-half map, static ELF64\n");
    kprintf("processes, a framebuffer graphics stack, ring-3 syscalls, and a\n");
    kprintf("window manager foundation.\n");
}

static void command_uname(void) {
    kprintf("Z-OS %s x86_64 BIOS long-mode\n", Z_OS_VERSION);
}

static void command_ls(int argc, char *arguments[]) {
    const char *path = argc > 1 ? arguments[1] : ".";
    struct vfs_node *node = vfs_lookup(shell_cwd, path);
    if (node == NULL) {
        report_vfs_error(VFS_ERROR_NOT_FOUND);
        return;
    }
    struct vfs_stat stat;
    if (vfs_stat(node, &stat) != VFS_OK) {
        report_vfs_error(VFS_ERROR_INVALID);
        return;
    }
    if (stat.type != VFS_TYPE_DIRECTORY) {
        kprintf("- %8llu  %s\n", stat.size, vfs_node_name(node));
        return;
    }
    struct vfs_dirent entries[64];
    u32 count = vfs_list(node, entries, 64);
    for (u32 index = 0; index < count; index++) {
        if (entries[index].type == VFS_TYPE_DIRECTORY) {
            kprintf("d %8llu  %s/\n", entries[index].size, entries[index].name);
        } else if (entries[index].type == VFS_TYPE_DEVICE) {
            kprintf("c %8llu  %s\n", entries[index].size, entries[index].name);
        } else {
            kprintf("- %8llu  %s\n", entries[index].size, entries[index].name);
        }
    }
    if (count == 0) {
        kprintf("directory is empty\n");
    }
}

static void command_cd(int argc, char *arguments[]) {
    const char *path = argc > 1 ? arguments[1] : "/";
    struct vfs_node *node = vfs_lookup(shell_cwd, path);
    if (node == NULL) {
        report_vfs_error(VFS_ERROR_NOT_FOUND);
        return;
    }
    if (!vfs_is_directory(node)) {
        report_vfs_error(VFS_ERROR_TYPE);
        return;
    }
    shell_cwd = node;
}

static void command_cat(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: cat <path>\n");
        return;
    }
    struct vfs_node *node = vfs_lookup(shell_cwd, arguments[1]);
    if (node == NULL) {
        report_vfs_error(VFS_ERROR_NOT_FOUND);
        return;
    }
    if (vfs_is_directory(node)) {
        report_vfs_error(VFS_ERROR_TYPE);
        return;
    }
    char buffer[512];
    u64 offset = 0;
    char last = '\n';
    for (;;) {
        int count = vfs_read(node, offset, buffer, sizeof(buffer));
        if (count < 0) {
            report_vfs_error(count);
            return;
        }
        if (count == 0) {
            break;
        }
        console_write_n(buffer, (usize)count);
        last = buffer[count - 1];
        offset += (u64)count;
        if (count < (int)sizeof(buffer)) {
            break;
        }
    }
    if (last != '\n') {
        console_putc('\n');
    }
}

static struct vfs_node *ensure_file(const char *path) {
    struct vfs_node *node = vfs_lookup(shell_cwd, path);
    if (node != NULL) {
        return node;
    }
    int result = vfs_create(shell_cwd, path);
    if (result != VFS_OK) {
        report_vfs_error(result);
        return NULL;
    }
    return vfs_lookup(shell_cwd, path);
}

static void command_write(int argc, char *arguments[], bool append) {
    if (argc < 2) {
        kprintf("usage: %s <path> <text>\n", append ? "append" : "write");
        return;
    }
    struct vfs_node *node = ensure_file(arguments[1]);
    if (node == NULL) {
        return;
    }
    if (vfs_is_directory(node)) {
        report_vfs_error(VFS_ERROR_TYPE);
        return;
    }
    char text[512];
    join_arguments(argc, arguments, 2, text, sizeof(text), true);
    int result;
    if (append) {
        result = vfs_append(node, text, strlen(text));
    } else {
        result = vfs_truncate(node);
        if (result == VFS_OK) {
            result = vfs_write(node, 0, text, strlen(text));
        }
    }
    if (result < 0) {
        report_vfs_error(result);
    }
}

static void command_mkdir(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: mkdir <path>\n");
        return;
    }
    int result = vfs_mkdir(shell_cwd, arguments[1]);
    if (result != VFS_OK) {
        report_vfs_error(result);
    }
}

static void command_touch(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: touch <path>\n");
        return;
    }
    int result = vfs_create(shell_cwd, arguments[1]);
    if (result != VFS_OK && result != VFS_ERROR_EXISTS) {
        report_vfs_error(result);
    }
}

static void command_rm(int argc, char *arguments[], bool require_directory) {
    if (argc < 2) {
        kprintf("usage: %s <path>\n", require_directory ? "rmdir" : "rm");
        return;
    }
    struct vfs_node *node = vfs_lookup(shell_cwd, arguments[1]);
    if (node == NULL) {
        report_vfs_error(VFS_ERROR_NOT_FOUND);
        return;
    }
    if (require_directory && !vfs_is_directory(node)) {
        report_vfs_error(VFS_ERROR_TYPE);
        return;
    }
    int result = vfs_remove(shell_cwd, arguments[1]);
    if (result != VFS_OK) {
        report_vfs_error(result);
    }
}

static void command_mv(int argc, char *arguments[]) {
    if (argc < 3) {
        kprintf("usage: mv <old> <new>\n");
        return;
    }
    int result = vfs_rename(shell_cwd, arguments[1], arguments[2]);
    if (result != VFS_OK) {
        report_vfs_error(result);
    }
}

static void command_cp(int argc, char *arguments[]) {
    if (argc < 3) {
        kprintf("usage: cp <source> <destination>\n");
        return;
    }
    struct vfs_node *source = vfs_lookup(shell_cwd, arguments[1]);
    if (source == NULL) {
        report_vfs_error(VFS_ERROR_NOT_FOUND);
        return;
    }
    if (!vfs_is_file(source)) {
        report_vfs_error(VFS_ERROR_TYPE);
        return;
    }
    struct vfs_stat source_stat;
    vfs_stat(source, &source_stat);
    if (source_stat.size > VFS_MAX_FILE_SIZE) {
        report_vfs_error(VFS_ERROR_NO_SPACE);
        return;
    }
    u8 *buffer = kmalloc(source_stat.size == 0 ? 1 : (usize)source_stat.size);
    if (buffer == NULL) {
        report_vfs_error(VFS_ERROR_NO_SPACE);
        return;
    }
    int count = vfs_read(source, 0, buffer, source_stat.size);
    if (count < 0) {
        kfree(buffer);
        report_vfs_error(count);
        return;
    }
    struct vfs_node *destination = ensure_file(arguments[2]);
    if (destination == NULL) {
        kfree(buffer);
        return;
    }
    if (!vfs_is_file(destination)) {
        kfree(buffer);
        report_vfs_error(VFS_ERROR_TYPE);
        return;
    }
    vfs_truncate(destination);
    int result = vfs_write(destination, 0, buffer, (u64)count);
    kfree(buffer);
    if (result < 0) {
        report_vfs_error(result);
    }
}

static void command_stat(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: stat <path>\n");
        return;
    }
    struct vfs_node *node = vfs_lookup(shell_cwd, arguments[1]);
    if (node == NULL) {
        report_vfs_error(VFS_ERROR_NOT_FOUND);
        return;
    }
    struct vfs_stat stat;
    vfs_stat(node, &stat);
    const char *type = stat.type == VFS_TYPE_DIRECTORY ? "directory" : (stat.type == VFS_TYPE_DEVICE ? "device" : "file");
    kprintf("name:  %s\n", vfs_node_name(node));
    kprintf("type:  %s\n", type);
    kprintf("size:  %llu bytes\n", stat.size);
    kprintf("nodes: %u\n", stat.child_count);
    if (stat.type == VFS_TYPE_DEVICE) {
        kprintf("dev:   %u\n", stat.device_id);
    }
}

static void command_mem(void) {
    struct pmm_stats pmm = pmm_get_stats();
    struct heap_stats heap = heap_get_stats();
    struct vfs_stats fs = vfs_get_stats();
    kprintf("physical: %llu MiB total, %llu MiB free, %llu MiB used\n",
            pmm.total_frames / 256, pmm.free_frames / 256, pmm.used_frames / 256);
    kprintf("heap:     %llu KiB total, %llu KiB used, %llu KiB free\n",
            heap.total_bytes / 1024, heap.used_bytes / 1024, heap.free_bytes / 1024);
    kprintf("ramfs:    %u nodes, %llu KiB data capacity\n", fs.nodes_used, fs.bytes_allocated / 1024);
    kprintf("allocations: %llu\n", heap.allocation_count);
}

static void command_df(void) {
    struct vfs_stats stats = vfs_get_stats();
    struct heap_stats heap = heap_get_stats();
    u64 used_capacity = stats.bytes_allocated;
    kprintf("ramfs nodes: %u of %u\n", stats.nodes_used, VFS_MAX_NODES);
    kprintf("file data:   %llu bytes allocated\n", used_capacity);
    kprintf("heap free:   %llu bytes\n", heap.free_bytes);
}

static void command_uptime(void) {
    u64 milliseconds = timer_uptime_ms();
    u64 seconds = milliseconds / 1000;
    u64 days = seconds / 86400;
    seconds %= 86400;
    u64 hours = seconds / 3600;
    seconds %= 3600;
    u64 minutes = seconds / 60;
    seconds %= 60;
    kprintf("up %llu days, %02llu:%02llu:%02llu (%llu ticks)\n",
            days, hours, minutes, seconds, timer_ticks());
}

static void command_date(void) {
    struct rtc_time time;
    rtc_read(&time);
    kprintf("%04u-%02u-%02u %02u:%02u:%02u UTC\n",
            time.year, time.month, time.day, time.hour, time.minute, time.second);
}

static const char *state_name(u32 state) {
    switch (state) {
        case THREAD_READY:
            return "ready";
        case THREAD_RUNNING:
            return "running";
        case THREAD_SLEEPING:
            return "sleeping";
        case THREAD_ZOMBIE:
            return "zombie";
        default:
            return "unused";
    }
}

static void command_ps(void) {
    struct thread_status statuses[MAX_THREADS];
    u32 count = 0;
    sched_get_status(statuses, MAX_THREADS, &count);
    kprintf("pid  state     ticks  stack  name\n");
    for (u32 index = 0; index < count; index++) {
        kprintf("%3llu  %-8s  %5llu  %3lluK  %s\n",
                statuses[index].pid, state_name(statuses[index].state),
                statuses[index].cpu_ticks, statuses[index].stack_size / 1024,
                statuses[index].name);
    }
    kprintf("processes: %llu total, %llu ready, %llu blocked\n",
            process_count(), process_ready_count(), process_blocked_count());
    static struct process snapshot[MAX_PROCESSES];
    u32 process_total = process_snapshot(snapshot, MAX_PROCESSES);
    if (process_total > 0) {
        kprintf("ppid  pid  state     ticks  switches  name\n");
        for (u32 index = 0; index < process_total; index++) {
            kprintf("%4llu  %3llu  %-8s  %5llu  %8llu  %s\n",
                    snapshot[index].parent_pid, snapshot[index].pid,
                    process_state_name(snapshot[index].state),
                    snapshot[index].cpu_ticks, snapshot[index].context_switches,
                    snapshot[index].name);
        }
    }
}

static void command_sleep(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: sleep <milliseconds>\n");
        return;
    }
    int milliseconds = atoi(arguments[1]);
    if (milliseconds < 0) {
        kprintf("error: invalid duration\n");
        return;
    }
    sched_sleep_ms((u64)milliseconds);
    kprintf("slept %d ms\n", milliseconds);
}

static void command_history(void) {
    for (u32 index = 0; index < shell_history_count; index++) {
        kprintf("%3u  %s\n", index + 1, shell_history[index]);
    }
}

static void command_hexdump(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: hexdump <path>\n");
        return;
    }
    struct vfs_node *node = vfs_lookup(shell_cwd, arguments[1]);
    if (node == NULL) {
        report_vfs_error(VFS_ERROR_NOT_FOUND);
        return;
    }
    if (!vfs_is_file(node)) {
        report_vfs_error(VFS_ERROR_TYPE);
        return;
    }
    u8 buffer[16];
    u64 offset = 0;
    for (;;) {
        int count = vfs_read(node, offset, buffer, sizeof(buffer));
        if (count <= 0) {
            break;
        }
        kprintf("%08llx  ", offset);
        for (int index = 0; index < 16; index++) {
            if (index < count) {
                kprintf("%02x ", buffer[index]);
            } else {
                kprintf("   ");
            }
            if (index == 7) {
                kprintf(" ");
            }
        }
        kprintf(" |");
        for (int index = 0; index < count; index++) {
            char character = (char)buffer[index];
            kprintf("%c", character >= 32 && character <= 126 ? character : '.');
        }
        kprintf("|\n");
        offset += (u64)count;
        if (count < (int)sizeof(buffer)) {
            break;
        }
    }
}

static void command_rand(int argc, char *arguments[]) {
    int count = argc > 1 ? atoi(arguments[1]) : 16;
    if (count <= 0 || count > 256) {
        count = 16;
    }
    u8 buffer[256];
    random_bytes(buffer, (usize)count);
    for (int index = 0; index < count; index++) {
        kprintf("%02x", buffer[index]);
        if ((index & 15) == 15 || index == count - 1) {
            kprintf("\n");
        } else if ((index & 7) == 7) {
            kprintf("  ");
        } else {
            kprintf(" ");
        }
    }
}

static u64 run_single_program(const char *name, const u8 *image_start, const u8 *image_end, u64 *status)
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
    if (status != NULL) {
        *status = exit_status;
    }
    if (result != SCHED_ERROR_NONE || completed != 1) {
        return 0;
    }
    return reaped_pid;
}

static void command_usertest(void) {
    u64 status = 0xFFFFFFFFFFFFFFFFULL;
    u64 pid = run_single_program("usertest", user_program_start, user_program_end, &status);
    if (pid == 0) {
        kprintf("user program failed to run\n");
        return;
    }
    kprintf("user program exited with status %llu\n", status);
}

static void command_schedtest(void) {
    u64 image_size = (u64)(user_multi_end - user_multi_start);
    struct process *first = process_create("multi-a", 0, user_multi_start, image_size);
    struct process *second = process_create("multi-b", 0, user_multi_start, image_size);
    if (first == NULL || second == NULL) {
        kprintf("schedtest: process creation failed\n");
        process_reap_zombies();
        return;
    }
    u64 pids[2];
    pids[0] = first->pid;
    pids[1] = second->pid;
    u64 before = sched_preemption_count();
    u32 completed = 0;
    int result = sched_run_batch(pids, 2, &completed);
    u64 first_status = 0xFFFFFFFFFFFFFFFFULL;
    u64 second_status = 0xFFFFFFFFFFFFFFFFULL;
    process_reap_child(0, pids[0], NULL, &first_status);
    process_reap_child(0, pids[1], NULL, &second_status);
    kprintf("schedtest: quantum=%llu ticks completed=%u preemptions=%llu\n",
            sched_quantum_ticks(), completed, sched_preemption_count() - before);
    kprintf("schedtest: status pid %llu = %llu, pid %llu = %llu, result=%d\n",
            pids[0], first_status, pids[1], second_status, result);
}

static void gui_print_action(char *previous) {
    const char *action = desktop_last_action();
    if (action[0] != 0 && strcmp(previous, action) != 0) {
        kprintf("gui: %s\n", action);
        strncpy(previous, action, 47);
        previous[47] = 0;
    }
}

static int gui_present_frame(bool buffered) {
    if (!buffered) {
        return 0;
    }
    i32 x = 0;
    i32 y = 0;
    u32 width = 0;
    u32 height = 0;
    if (desktop_dirty_take(&x, &y, &width, &height)) {
        fb_present_rect(x, y, width, height);
        return 1;
    }
    fb_present();
    return 0;
}

static int gui_redraw_frame(struct gfx_surface *vram, bool buffered) {
    mouse_restore(vram);
    desktop_render();
    int partial = gui_present_frame(buffered);
    mouse_render(vram);
    return partial;
}

static void gui_run_session(void) {
    if (fb_set_mode(FB_DEFAULT_WIDTH, FB_DEFAULT_HEIGHT, FB_DEFAULT_BPP) != 0) {
        kprintf("gui: framebuffer mode set failed\n");
        return;
    }
    struct gfx_surface *vram = fb_surface();
    if (vram == NULL) {
        kprintf("gui: no graphics surface\n");
        fb_restore_text();
        console_clear();
        return;
    }
    struct gfx_surface *scene = fb_double_buffer();
    bool buffered = scene != NULL && scene != vram;
    if (scene == NULL) {
        scene = vram;
    }
    mouse_set_bounds(FB_DEFAULT_WIDTH, FB_DEFAULT_HEIGHT);
    mouse_set_gui_mode(true);
    desktop_init(scene);
    desktop_dirty_mark_full();
    desktop_render();
    if (buffered) {
        fb_present();
    }
    mouse_render(vram);
    kprintf("gui: desktop active\n");
    char previous[48];
    previous[0] = 0;
    u32 full_frames = 0;
    u32 dirty_frames = 0;
    u32 move_events = 0;
    u32 move_full_frames = 0;
    while (!desktop_should_exit()) {
        struct mouse_event event;
        while (mouse_pop_event(&event)) {
            bool changed = desktop_handle_mouse(&event);
            bool move_event = event.type == MOUSE_EVENT_MOVE;
            if (move_event) {
                move_events++;
            }
            if (changed) {
                int partial = gui_redraw_frame(vram, buffered);
                if (partial != 0) {
                    dirty_frames++;
                } else if (buffered) {
                    full_frames++;
                    if (move_event) {
                        move_full_frames++;
                    }
                }
                gui_print_action(previous);
            } else if (move_event) {
                mouse_restore(vram);
                mouse_render(vram);
                kprintf("gui: mouse move x=%d y=%d dx=%d dy=%d\n",
                        event.x, event.y, event.delta_x, event.delta_y);
            }
        }
        int key = input_trygetchar();
        while (key >= 0) {
            bool changed = desktop_handle_key((char)key);
            if (changed) {
                int partial = gui_redraw_frame(vram, buffered);
                if (partial != 0) {
                    dirty_frames++;
                } else if (buffered) {
                    full_frames++;
                }
                gui_print_action(previous);
            }
            key = input_trygetchar();
        }
        sched_sleep_ms(15);
    }
    kprintf("gui: frames full=%u dirty=%u moves=%u move_full=%u\n",
            full_frames, dirty_frames, move_events, move_full_frames);
    mouse_restore(vram);
    mouse_set_gui_mode(false);
    fb_restore_text();
    console_clear();
    kprintf("gui: exited desktop\n");
}

static u32 parse_decimal(const char *text, u32 fallback) {
    if (text == NULL || text[0] == 0) {
        return fallback;
    }
    u32 value = 0;
    for (const char *cursor = text; *cursor != 0; cursor++) {
        if (*cursor < '0' || *cursor > '9') {
            return fallback;
        }
        value = value * 10 + (u32)(*cursor - '0');
        if (value > 1000000) {
            return fallback;
        }
    }
    return value;
}

static void command_netinfo(void) {
    const struct net_config *config = net_config_get();
    if (!net_ready()) {
        kprintf("net: no e1000 device\n");
        return;
    }
    char mac_text[24];
    char ip_text[20];
    char gateway_text[20];
    char mask_text[20];
    net_format_mac(config->mac, mac_text, sizeof(mac_text));
    net_format_ip(config->ip, ip_text, sizeof(ip_text));
    net_format_ip(config->gateway, gateway_text, sizeof(gateway_text));
    net_format_ip(config->netmask, mask_text, sizeof(mask_text));
    kprintf("net: link %s\n", e1000_link_state());
    kprintf("net: mac %s\n", mac_text);
    kprintf("net: ip %s\n", ip_text);
    kprintf("net: gateway %s mask %s\n", gateway_text, mask_text);
    kprintf("net: rx %u frames, tx %u frames\n",
            net_frames_received(), net_frames_sent());
    kprintf("net: ipv4 rx %u, tx %u, checksum errors %u\n",
            net_ipv4_received(), net_ipv4_sent(), ipv4_checksum_errors());
    kprintf("net: arp entries %u, requests %u, replies %u\n",
            arp_cache_count(), arp_requests_sent(), arp_replies_received());
    kprintf("net: udp rx %u, tx %u\n", udp_datagrams_received(), udp_datagrams_sent());
    const struct e1000_device *device = e1000_device_state();
    kprintf("net: e1000 rx packets %u, tx packets %u, irq %u\n",
            device->packets_received, device->packets_sent, device->irq);
}

static void command_arp(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: arp <ip>\n");
        return;
    }
    if (!net_ready()) {
        kprintf("arp: network is not ready\n");
        return;
    }
    u32 address = net_parse_ip(arguments[1]);
    if (address == 0) {
        kprintf("arp: invalid address\n");
        return;
    }
    u8 mac[6];
    if (arp_resolve(address, mac, 2000) != 0) {
        kprintf("arp: no reply from %s\n", arguments[1]);
        return;
    }
    char mac_text[24];
    net_format_mac(mac, mac_text, sizeof(mac_text));
    kprintf("arp: %s is at %s\n", arguments[1], mac_text);
}

static void command_ping(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: ping <ip>\n");
        return;
    }
    if (!net_ready()) {
        kprintf("ping: network is not ready\n");
        return;
    }
    u32 address = net_parse_ip(arguments[1]);
    if (address == 0) {
        kprintf("ping: invalid address\n");
        return;
    }
    u32 count = argc >= 3 ? parse_decimal(arguments[2], 4) : 4;
    if (count == 0 || count > 10) {
        count = 4;
    }
    kprintf("ping: %s with 16 bytes of payload\n", arguments[1]);
    u32 received = 0;
    for (u32 index = 0; index < count; index++) {
        u32 round_trip = 0;
        if (icmp_ping(address, 800, &round_trip) == 0) {
            received++;
            kprintf("ping: reply from %s time=%u ms sequence=%u\n",
                    arguments[1], round_trip, index + 1);
        } else {
            kprintf("ping: timeout sequence=%u\n", index + 1);
        }
        sched_sleep_ms(100);
    }
    kprintf("ping: %u/%u replies from %s\n", received, count, arguments[1]);
}

static void command_udp_echo(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: udp-echo <port> [timeout-ms]\n");
        return;
    }
    if (!net_ready()) {
        kprintf("udp-echo: network is not ready\n");
        return;
    }
    u16 port = (u16)parse_decimal(arguments[1], 0);
    if (port == 0) {
        kprintf("udp-echo: invalid port\n");
        return;
    }
    u32 timeout_ms = argc >= 3 ? parse_decimal(arguments[2], 4000) : 4000;
    int descriptor = socket_create(SOCKET_AF_INET, SOCKET_SOCK_DGRAM, 0);
    if (descriptor < 0 || socket_bind(descriptor, 0, port) != 0) {
        kprintf("udp-echo: bind failed\n");
        if (descriptor > 0) {
            socket_close(descriptor);
        }
        return;
    }
    kprintf("udp-echo: listening on port %u\n", port);
    u8 buffer[512];
    u32 source_ip = 0;
    u16 source_port = 0;
    int received = socket_recvfrom(descriptor, buffer, sizeof(buffer), timeout_ms,
                                   &source_ip, &source_port);
    if (received < 0) {
        kprintf("udp-echo: timeout\n");
        socket_close(descriptor);
        return;
    }
    char ip_text[20];
    net_format_ip(source_ip, ip_text, sizeof(ip_text));
    kprintf("udp-echo: received %d bytes from %s:%u\n", received, ip_text, source_port);
    int sent = socket_sendto(descriptor, source_ip, source_port, buffer, (u32)received);
    if (sent < 0) {
        kprintf("udp-echo: reply failed\n");
    } else {
        kprintf("udp-echo: echoed %d bytes\n", sent);
    }
    socket_close(descriptor);
}

static void command_mouse(void) {
    struct mouse_state state;
    mouse_get_state(&state);
    kprintf("mouse present: %s, initialized: %s\n",
            state.present ? "yes" : "no", state.initialized ? "yes" : "no");
    kprintf("mouse position: %d, %d buttons: %u\n", (int)state.x, (int)state.y, state.buttons);
    kprintf("mouse packets: %u errors: %u queued: %u\n",
            state.packets, state.errors, mouse_event_count());
}

static void command_gui(void) {
    gui_run_session();
}

static void command_exit(void) {
    kprintf("exit: shell session is the base console\n");
    kprintf("exit: type exit inside the desktop to leave it\n");
}

static void command_diskinfo(void) {
    kprintf("ata present: %s\n", ata_present() ? "yes" : "no");
    if (ata_present()) {
        kprintf("ata model: %s\n", ata_model());
        kprintf("ata sectors: %u (%u KiB)\n", ata_sector_count(), ata_sector_count() / 2);
    }
    kprintf("zfs mounted: %s, files: %u\n", zfs_mounted() ? "yes" : "no", zfs_file_count());
    struct vfs_node *disk = vfs_lookup(vfs_root(), "/disk");
    kprintf("vfs /disk: %s\n", disk != NULL ? "present" : "absent");
}

static void command_format(void) {
    int result = zfs_format();
    if (result != ZFS_ERROR_NONE) {
        kprintf("format: failed (%d)\n", result);
        return;
    }
    kprintf("format: zfs formatted %u sectors\n", ata_sector_count());
}

static void command_zmount(void) {
    int result = zfs_mount();
    if (result != ZFS_ERROR_NONE) {
        kprintf("zmount: mount failed (%d), run format first\n", result);
        return;
    }
    result = zfs_mount_vfs();
    if (result != ZFS_ERROR_NONE) {
        kprintf("zmount: vfs attach failed (%d)\n", result);
        return;
    }
    kprintf("zmount: %u files attached under /disk\n", zfs_file_count());
}

static void command_zls(void) {
    struct zfs_dirent entries[ZFS_MAX_FILES];
    u32 count = zfs_list(entries, ZFS_MAX_FILES);
    kprintf("zfs files: %u\n", count);
    for (u32 index = 0; index < count; index++) {
        kprintf("- %8u  %s\n", entries[index].size, entries[index].name);
    }
}

static void command_zsave(int argc, char *arguments[]) {
    if (argc < 3) {
        kprintf("usage: zsave <name> <text>\n");
        return;
    }
    char text[512];
    join_arguments(argc, arguments, 2, text, sizeof(text), true);
    int result = zfs_write_file(arguments[1], text, (u32)strlen(text));
    if (result != ZFS_ERROR_NONE) {
        kprintf("zsave: failed (%d)\n", result);
        return;
    }
    kprintf("zsave: wrote %u bytes to %s\n", (u32)strlen(text), arguments[1]);
}

static void command_zcat(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: zcat <name>\n");
        return;
    }
    u8 *buffer = kmalloc(ZFS_MAX_FILE_SIZE);
    if (buffer == NULL) {
        kprintf("zcat: out of memory\n");
        return;
    }
    int read = zfs_read_file(arguments[1], buffer, ZFS_MAX_FILE_SIZE);
    if (read < 0) {
        kprintf("zcat: read failed (%d)\n", read);
        kfree(buffer);
        return;
    }
    console_write_n((const char *)buffer, (usize)read);
    kfree(buffer);
}

static void command_zrm(int argc, char *arguments[]) {
    if (argc < 2) {
        kprintf("usage: zrm <name>\n");
        return;
    }
    int result = zfs_remove_file(arguments[1]);
    if (result != ZFS_ERROR_NONE) {
        kprintf("zrm: failed (%d)\n", result);
        return;
    }
    kprintf("zrm: removed %s\n", arguments[1]);
}

static void command_zsync(void) {
    int result = zfs_sync_vfs();
    if (result != ZFS_ERROR_NONE) {
        kprintf("zsync: failed (%d)\n", result);
        return;
    }
    kprintf("zsync: /disk written to zfs, %u files\n", zfs_file_count());
}

static void command_postest(void) {
    u64 status = 0xFFFFFFFFFFFFFFFFULL;
    u64 pid = run_single_program("postest", user_postest_start, user_postest_end, &status);
    if (pid == 0) {
        kprintf("postest: program failed to run\n");
        return;
    }
    kprintf("postest: exit status %llu\n", status);
}

static void command_guitest2(void) {
    if (fb_set_mode(FB_DEFAULT_WIDTH, FB_DEFAULT_HEIGHT, FB_DEFAULT_BPP) != 0) {
        kprintf("guitest2: framebuffer mode set failed\n");
        return;
    }
    u64 status = 0xFFFFFFFFFFFFFFFFULL;
    run_single_program("guitest2", user_guitest2_start, user_guitest2_end, &status);
    u32 lit = 0;
    for (u32 y = 84; y < 100; y++) {
        for (u32 x = 60; x < 240; x++) {
            if (fb_pixel(x, y) == 0x00E8EEF7U) {
                lit++;
            }
        }
    }
    bool verified = lit > 40 && fb_pixel(45, 45) == 0x00F2C14EU;
    kprintf("guitest2: program status %llu, font pixels %u, framebuffer %s\n",
            status, lit, verified ? "verified" : "failed");
    fb_restore_text();
    console_clear();
}

static void command_forktest(void) {
    u64 status = 0xFFFFFFFFFFFFFFFFULL;
    u64 pid = run_single_program("forktest", user_forktest_start, user_forktest_end, &status);
    if (pid == 0) {
        kprintf("forktest: program failed to run\n");
        return;
    }
    kprintf("forktest: parent exit status %llu\n", status);
}

static void command_gfx(int argc, char *arguments[]) {
    const char *mode = argc > 1 ? arguments[1] : "demo";
    if (strcmp(mode, "off") == 0) {
        fb_restore_text();
        console_clear();
        kprintf("gfx: framebuffer restored to text mode\n");
        return;
    }
    bool test_only = strcmp(mode, "test") == 0;
    if (fb_set_mode(FB_DEFAULT_WIDTH, FB_DEFAULT_HEIGHT, FB_DEFAULT_BPP) != 0) {
        kprintf("gfx: framebuffer mode set failed\n");
        return;
    }
    struct gfx_surface *surface = fb_surface();
    if (surface == NULL) {
        kprintf("gfx: no graphics surface available\n");
        fb_restore_text();
        return;
    }
    gfx_clear(surface, 0x00101A2AU);
    gfx_fill_rect(surface, 0, 0, FB_DEFAULT_WIDTH, 28, 0x00E8EEF7U);
    gfx_fill_rect(surface, 0, FB_DEFAULT_HEIGHT - 28, FB_DEFAULT_WIDTH, 28, 0x00202A38U);
    fb_draw_text(12, 6, "Z-OS 0.5 desktop foundation", 0x00203040U);
    fb_draw_text(12, FB_DEFAULT_HEIGHT - 22, "posix primitives online", 0x00C8D4E4U);
    wm_init();
    struct wm_window *left = wm_create(48, 64, 300, 220, 0x00F0F4FAU);
    struct wm_window *right = wm_create(392, 96, 340, 260, 0x00E6ECF6U);
    if (left != NULL) {
        wm_fill(left->id, 0, 0, 300, 24, 0x003C6E9EU);
        wm_fill(left->id, 16, 40, 120, 72, 0x00D9453DU);
        wm_fill(left->id, 148, 40, 120, 72, 0x002E9E5BU);
        wm_fill(left->id, 16, 124, 252, 72, 0x002F6FB5U);
    }
    if (right != NULL) {
        wm_fill(right->id, 0, 0, 340, 24, 0x0080401AU);
        wm_fill(right->id, 16, 40, 308, 96, 0x00F2C14EU);
        wm_fill(right->id, 16, 148, 146, 88, 0x007B4FA0U);
        wm_fill(right->id, 178, 148, 146, 88, 0x002AA6A6U);
    }
    wm_draw_to(surface);
    fb_draw_text(64, 68, "files", 0x003C6E9EU);
    fb_draw_text(408, 100, "network", 0x0080401AU);
    fb_draw_text(64, 300, "graphics stack with font rendering and mouse cursor", 0x00D8E2F0U);
    mouse_set_bounds(FB_DEFAULT_WIDTH, FB_DEFAULT_HEIGHT);
    mouse_move_to(560, 320);
    mouse_render(surface);
    u32 text_pixels = 0;
    for (u32 y = 6; y < 22; y++) {
        for (u32 x = 12; x < 280; x++) {
            if (fb_pixel(x, y) == 0x00203040U) {
                text_pixels++;
            }
        }
    }
    u32 cursor_black = 0;
    u32 cursor_white = 0;
    for (u32 y = 320; y < 336; y++) {
        for (u32 x = 560; x < 576; x++) {
            u32 pixel = fb_pixel(x, y);
            if (pixel == 0x00000000U) {
                cursor_black++;
            } else if (pixel == 0x00FFFFFFU) {
                cursor_white++;
            }
        }
    }
    bool verified = fb_pixel(120, 140) == 0x00D9453DU &&
                    fb_pixel(460, 200) == 0x00F2C14EU &&
                    fb_pixel(620, 280) == 0x002AA6A6U &&
                    fb_pixel(100, 70) == 0x003C6E9EU &&
                    text_pixels > 20 &&
                    cursor_black > 20 && cursor_white > 20;
    kprintf("gfx: framebuffer %ux%ux%u at %p, surface %s\n",
            fb_current_mode().width, fb_current_mode().height,
            fb_current_mode().bits_per_pixel, (void *)fb_current_mode().physical,
            fb_current_mode().shadow ? "shadow" : "vram");
    kprintf("gfx: window manager composited %u windows\n", wm_count());
    kprintf("gfx: font pixels %u, cursor outline pixels %u\n", text_pixels, cursor_black);
    kprintf("gfx: composition %s\n", verified ? "verified" : "failed");
    while (wm_count() > 0) {
        u32 id = wm_zorder_at(0);
        wm_destroy(id);
    }
    if (test_only) {
        fb_restore_text();
        console_clear();
        kprintf("gfx: test complete, framebuffer restored to text mode\n");
    } else {
        kprintf("gfx: graphics mode active, run 'gfx off' to return to text mode\n");
    }
}

static void execute_command(int argc, char *arguments[]) {
    const char *command = arguments[0];
    if (strcmp(command, "help") == 0) {
        command_help();
    } else if (strcmp(command, "about") == 0) {
        command_about();
    } else if (strcmp(command, "version") == 0) {
        command_about();
    } else if (strcmp(command, "uname") == 0) {
        command_uname();
    } else if (strcmp(command, "clear") == 0) {
        console_clear();
    } else if (strcmp(command, "echo") == 0) {
        char text[512];
        join_arguments(argc, arguments, 1, text, sizeof(text), false);
        kprintf("%s\n", text);
    } else if (strcmp(command, "pwd") == 0) {
        char path[VFS_PATH_MAX];
        if (vfs_build_path(shell_cwd, path, sizeof(path)) == VFS_OK) {
            kprintf("%s\n", path);
        }
    } else if (strcmp(command, "cd") == 0) {
        command_cd(argc, arguments);
    } else if (strcmp(command, "ls") == 0) {
        command_ls(argc, arguments);
    } else if (strcmp(command, "cat") == 0) {
        command_cat(argc, arguments);
    } else if (strcmp(command, "mkdir") == 0) {
        command_mkdir(argc, arguments);
    } else if (strcmp(command, "rmdir") == 0) {
        command_rm(argc, arguments, true);
    } else if (strcmp(command, "touch") == 0) {
        command_touch(argc, arguments);
    } else if (strcmp(command, "write") == 0) {
        command_write(argc, arguments, false);
    } else if (strcmp(command, "append") == 0) {
        command_write(argc, arguments, true);
    } else if (strcmp(command, "rm") == 0) {
        command_rm(argc, arguments, false);
    } else if (strcmp(command, "mv") == 0) {
        command_mv(argc, arguments);
    } else if (strcmp(command, "cp") == 0) {
        command_cp(argc, arguments);
    } else if (strcmp(command, "stat") == 0) {
        command_stat(argc, arguments);
    } else if (strcmp(command, "hexdump") == 0) {
        command_hexdump(argc, arguments);
    } else if (strcmp(command, "df") == 0) {
        command_df();
    } else if (strcmp(command, "mem") == 0 || strcmp(command, "free") == 0) {
        command_mem();
    } else if (strcmp(command, "uptime") == 0) {
        command_uptime();
    } else if (strcmp(command, "date") == 0) {
        command_date();
    } else if (strcmp(command, "ps") == 0 || strcmp(command, "threads") == 0) {
        command_ps();
    } else if (strcmp(command, "sleep") == 0) {
        command_sleep(argc, arguments);
    } else if (strcmp(command, "history") == 0) {
        command_history();
    } else if (strcmp(command, "rand") == 0) {
        command_rand(argc, arguments);
    } else if (strcmp(command, "usertest") == 0) {
        command_usertest();
    } else if (strcmp(command, "schedtest") == 0) {
        command_schedtest();
    } else if (strcmp(command, "forktest") == 0) {
        command_forktest();
    } else if (strcmp(command, "postest") == 0) {
        command_postest();
    } else if (strcmp(command, "guitest2") == 0) {
        command_guitest2();
    } else if (strcmp(command, "gfx") == 0) {
        command_gfx(argc, arguments);
    } else if (strcmp(command, "diskinfo") == 0) {
        command_diskinfo();
    } else if (strcmp(command, "format") == 0) {
        command_format();
    } else if (strcmp(command, "zmount") == 0) {
        command_zmount();
    } else if (strcmp(command, "zls") == 0) {
        command_zls();
    } else if (strcmp(command, "zsave") == 0) {
        command_zsave(argc, arguments);
    } else if (strcmp(command, "zcat") == 0) {
        command_zcat(argc, arguments);
    } else if (strcmp(command, "zrm") == 0) {
        command_zrm(argc, arguments);
    } else if (strcmp(command, "zsync") == 0) {
        command_zsync();
    } else if (strcmp(command, "mouse") == 0) {
        command_mouse();
    } else if (strcmp(command, "netinfo") == 0) {
        command_netinfo();
    } else if (strcmp(command, "arp") == 0) {
        command_arp(argc, arguments);
    } else if (strcmp(command, "ping") == 0) {
        command_ping(argc, arguments);
    } else if (strcmp(command, "udp-echo") == 0) {
        command_udp_echo(argc, arguments);
    } else if (strcmp(command, "gui") == 0) {
        command_gui();
    } else if (strcmp(command, "exit") == 0) {
        command_exit();
    } else if (strcmp(command, "reboot") == 0) {
        system_reboot();
    } else if (strcmp(command, "shutdown") == 0 || strcmp(command, "poweroff") == 0) {
        system_shutdown();
    } else {
        kprintf("unknown command: %s\n", command);
        kprintf("type help for a command list\n");
    }
}

void shell_thread(void *argument) {
    (void)argument;
    shell_cwd = vfs_root();
    for (;;) {
        char line[SHELL_LINE_MAX];
        int length = shell_read_line(line, sizeof(line));
        if (length <= 0) {
            continue;
        }
        add_history(line);
        char *arguments[SHELL_ARG_MAX + 1];
        int count = tokenize(line, arguments);
        if (count == 0) {
            continue;
        }
        execute_command(count, arguments);
        if (gui_enter_requested()) {
            gui_clear_requests();
            gui_run_session();
        }
        if (gui_exit_requested()) {
            gui_clear_requests();
            kprintf("exit: no desktop session active\n");
        }
    }
}
