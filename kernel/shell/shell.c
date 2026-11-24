#include "shell.h"
#include "console.h"
#include "heap.h"
#include "input.h"
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
    kprintf("  ps, threads          list kernel threads\n");
    kprintf("  sleep <ms>           sleep for milliseconds\n");
    kprintf("  history              show command history\n");
    kprintf("  rand [bytes]         print random bytes\n");
    kprintf("  usertest             enter ring 3 and run the embedded program\n");
    kprintf("  reboot               reset the machine\n");
    kprintf("  shutdown             power off or halt\n");
}

static void command_about(void) {
    kprintf("Z-OS %s %s\n", Z_OS_VERSION, Z_OS_CODENAME);
    kprintf("A compact x86_64 operating system with a preemptive kernel,\n");
    kprintf("virtual memory, ramfs, ring-3 syscalls, and a serial console.\n");
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

static void command_usertest(void) {
    u64 exit_code = 0;
    int result = user_run_demo(&exit_code);
    if (result != 0) {
        kprintf("user mode demo failed to start (%d)\n", result);
        return;
    }
    kprintf("user mode demo exited with status %llu\n", exit_code);
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
    }
}
