#include "lib.h"
#include "console.h"
#include "io.h"
#include "string.h"

struct output_state {
    char *buffer;
    usize capacity;
    usize count;
    bool to_console;
};

static void output_char(struct output_state *state, char character) {
    if (state->to_console) {
        console_putc(character);
    } else if (state->buffer != NULL && state->count + 1 < state->capacity) {
        state->buffer[state->count] = character;
    }
    state->count++;
}

static void output_repeat(struct output_state *state, char character, usize count) {
    for (usize index = 0; index < count; index++) {
        output_char(state, character);
    }
}

static void output_unsigned(struct output_state *state, u64 value, u32 base, bool uppercase, usize width, bool zero_pad, bool left_align, const char *prefix) {
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char *digits = uppercase ? digits_upper : digits_lower;
    char reversed[32];
    usize length = 0;
    if (value == 0) {
        reversed[length++] = '0';
    } else {
        while (value != 0) {
            reversed[length++] = digits[value % base];
            value /= base;
        }
    }
    usize prefix_length = prefix != NULL ? strlen(prefix) : 0;
    usize total = length + prefix_length;
    usize padding = width > total ? width - total : 0;
    if (!left_align && !zero_pad) {
        output_repeat(state, ' ', padding);
    }
    if (prefix != NULL) {
        for (usize index = 0; index < prefix_length; index++) {
            output_char(state, prefix[index]);
        }
    }
    if (!left_align && zero_pad) {
        output_repeat(state, '0', padding);
    }
    for (usize index = length; index > 0; index--) {
        output_char(state, reversed[index - 1]);
    }
    if (left_align) {
        output_repeat(state, ' ', padding);
    }
}

static void format_output(struct output_state *state, const char *format, __builtin_va_list arguments) {
    while (*format != 0) {
        if (*format != '%') {
            output_char(state, *format++);
            continue;
        }
        format++;
        if (*format == 0) {
            output_char(state, '%');
            break;
        }
        bool left_align = false;
        bool zero_pad = false;
        for (;;) {
            if (*format == '-') {
                left_align = true;
                format++;
            } else if (*format == '0') {
                zero_pad = true;
                format++;
            } else if (*format == '+') {
                format++;
            } else {
                break;
            }
        }
        usize width = 0;
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (usize)(*format - '0');
            format++;
        }
        u32 length_modifier = 0;
        while (*format == 'l') {
            length_modifier++;
            format++;
        }
        if (*format == 'z') {
            length_modifier = 3;
            format++;
        }
        if (*format == 0) {
            break;
        }
        char conversion = *format++;
        if (conversion == '%') {
            output_char(state, '%');
            continue;
        }
        if (conversion == 'c') {
            int character = __builtin_va_arg(arguments, int);
            usize padding = width > 1 ? width - 1 : 0;
            if (!left_align) {
                output_repeat(state, ' ', padding);
            }
            output_char(state, (char)character);
            if (left_align) {
                output_repeat(state, ' ', padding);
            }
            continue;
        }
        if (conversion == 's') {
            const char *text = __builtin_va_arg(arguments, const char *);
            if (text == NULL) {
                text = "(null)";
            }
            usize length = strlen(text);
            usize padding = width > length ? width - length : 0;
            if (!left_align) {
                output_repeat(state, ' ', padding);
            }
            for (usize index = 0; index < length; index++) {
                output_char(state, text[index]);
            }
            if (left_align) {
                output_repeat(state, ' ', padding);
            }
            continue;
        }
        if (conversion == 'd' || conversion == 'i') {
            i64 value;
            if (length_modifier == 3) {
                value = __builtin_va_arg(arguments, isize);
            } else if (length_modifier >= 2) {
                value = __builtin_va_arg(arguments, i64);
            } else {
                value = __builtin_va_arg(arguments, int);
            }
            bool negative = value < 0;
            u64 magnitude = negative ? (u64)(-(value + 1)) + 1 : (u64)value;
            usize adjusted_width = negative && width > 0 ? width - 1 : width;
            output_unsigned(state, magnitude, 10, false, adjusted_width, zero_pad, left_align, negative ? "-" : "");
            continue;
        }
        if (conversion == 'u' || conversion == 'x' || conversion == 'X' || conversion == 'o') {
            u64 value;
            if (length_modifier == 3) {
                value = __builtin_va_arg(arguments, usize);
            } else if (length_modifier >= 2) {
                value = __builtin_va_arg(arguments, u64);
            } else {
                value = __builtin_va_arg(arguments, unsigned int);
            }
            u32 base = conversion == 'o' ? 8 : (conversion == 'u' ? 10 : 16);
            output_unsigned(state, value, base, conversion == 'X', width, zero_pad, left_align, "");
            continue;
        }
        if (conversion == 'p') {
            u64 value = (u64)__builtin_va_arg(arguments, void *);
            usize padding = width > 18 ? width - 18 : 0;
            if (!left_align) {
                output_repeat(state, ' ', padding);
            }
            output_char(state, '0');
            output_char(state, 'x');
            bool started = false;
            for (int shift = 60; shift >= 0; shift -= 4) {
                u8 digit = (u8)((value >> shift) & 0xF);
                if (digit != 0 || shift == 0) {
                    started = true;
                }
                if (started) {
                    output_char(state, "0123456789abcdef"[digit]);
                }
            }
            if (left_align) {
                output_repeat(state, ' ', padding);
            }
            continue;
        }
        output_char(state, '%');
        output_char(state, conversion);
    }
}

void kprintf(const char *format, ...) {
    struct output_state state;
    state.buffer = NULL;
    state.capacity = 0;
    state.count = 0;
    state.to_console = true;
    __builtin_va_list arguments;
    __builtin_va_start(arguments, format);
    format_output(&state, format, arguments);
    __builtin_va_end(arguments);
}

int ksnprintf(char *buffer, usize size, const char *format, ...) {
    struct output_state state;
    state.buffer = buffer;
    state.capacity = size;
    state.count = 0;
    state.to_console = false;
    __builtin_va_list arguments;
    __builtin_va_start(arguments, format);
    format_output(&state, format, arguments);
    __builtin_va_end(arguments);
    if (size > 0) {
        usize terminate = state.count < size ? state.count : size - 1;
        buffer[terminate] = 0;
    }
    return (int)state.count;
}

usize kstrlen(const char *text) {
    return strlen(text);
}

void kputs(const char *text) {
    console_write(text);
}

void kputc(char character) {
    console_putc(character);
}

void halt_forever(void) {
    interrupts_disable();
    for (;;) {
        cpu_halt();
    }
}

void panic(const char *message) {
    interrupts_disable();
    console_set_color(COLOR_WHITE, COLOR_RED);
    kprintf("\nPANIC: %s\n", message);
    console_reset_color();
    halt_forever();
}

u64 parse_u64(const char *text, int base, bool *ok) {
    u64 value = 0;
    bool any = false;
    if (base == 16 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text += 2;
    }
    for (;;) {
        char character = *text;
        u32 digit;
        if (character >= '0' && character <= '9') {
            digit = (u32)(character - '0');
        } else if (character >= 'a' && character <= 'f') {
            digit = (u32)(character - 'a') + 10;
        } else if (character >= 'A' && character <= 'F') {
            digit = (u32)(character - 'A') + 10;
        } else {
            break;
        }
        if (digit >= (u32)base) {
            break;
        }
        value = value * (u32)base + digit;
        any = true;
        text++;
    }
    if (ok != NULL) {
        *ok = any;
    }
    return value;
}
