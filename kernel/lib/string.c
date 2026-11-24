#include "string.h"

void *memcpy(void *destination, const void *source, usize count) {
    u8 *to = destination;
    const u8 *from = source;
    for (usize index = 0; index < count; index++) {
        to[index] = from[index];
    }
    return destination;
}

void *memmove(void *destination, const void *source, usize count) {
    u8 *to = destination;
    const u8 *from = source;
    if (to < from) {
        for (usize index = 0; index < count; index++) {
            to[index] = from[index];
        }
    } else if (to > from) {
        for (usize index = count; index > 0; index--) {
            to[index - 1] = from[index - 1];
        }
    }
    return destination;
}

void *memset(void *destination, int value, usize count) {
    u8 *to = destination;
    for (usize index = 0; index < count; index++) {
        to[index] = (u8)value;
    }
    return destination;
}

int memcmp(const void *left, const void *right, usize count) {
    const u8 *a = left;
    const u8 *b = right;
    for (usize index = 0; index < count; index++) {
        if (a[index] != b[index]) {
            return (int)a[index] - (int)b[index];
        }
    }
    return 0;
}

usize strlen(const char *text) {
    usize length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left != 0 && *left == *right) {
        left++;
        right++;
    }
    return (int)(u8)*left - (int)(u8)*right;
}

int strncmp(const char *left, const char *right, usize count) {
    for (usize index = 0; index < count; index++) {
        if (left[index] != right[index]) {
            return (int)(u8)left[index] - (int)(u8)right[index];
        }
        if (left[index] == 0) {
            return 0;
        }
    }
    return 0;
}

char *strcpy(char *destination, const char *source) {
    char *result = destination;
    while ((*destination++ = *source++) != 0) {
    }
    return result;
}

char *strncpy(char *destination, const char *source, usize count) {
    usize index = 0;
    while (index < count && source[index] != 0) {
        destination[index] = source[index];
        index++;
    }
    while (index < count) {
        destination[index++] = 0;
    }
    return destination;
}

char *strchr(const char *text, int character) {
    char target = (char)character;
    while (*text != 0) {
        if (*text == target) {
            return (char *)text;
        }
        text++;
    }
    if (target == 0) {
        return (char *)text;
    }
    return NULL;
}

char *strrchr(const char *text, int character) {
    const char *last = NULL;
    char target = (char)character;
    while (*text != 0) {
        if (*text == target) {
            last = text;
        }
        text++;
    }
    if (target == 0) {
        return (char *)text;
    }
    return (char *)last;
}

int is_space(char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

int is_digit(char character) {
    return character >= '0' && character <= '9';
}

char to_upper(char character) {
    if (character >= 'a' && character <= 'z') {
        return (char)(character - 32);
    }
    return character;
}

char to_lower(char character) {
    if (character >= 'A' && character <= 'Z') {
        return (char)(character + 32);
    }
    return character;
}

char *skip_spaces(char *text) {
    while (is_space(*text)) {
        text++;
    }
    return text;
}

char *trim(char *text) {
    while (is_space(*text)) {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && is_space(end[-1])) {
        end--;
    }
    *end = 0;
    return text;
}

int atoi(const char *text) {
    int sign = 1;
    int value = 0;
    while (is_space(*text)) {
        text++;
    }
    if (*text == '-') {
        sign = -1;
        text++;
    } else if (*text == '+') {
        text++;
    }
    while (is_digit(*text)) {
        value = value * 10 + (*text - '0');
        text++;
    }
    return value * sign;
}

static u64 random_state = 0x9E3779B97F4A7C15ULL;

static u64 random_next(void) {
    u64 state = random_state;
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    random_state = state;
    return state;
}

void random_bytes(void *buffer, usize count) {
    u8 *bytes = buffer;
    u64 value = 0;
    for (usize index = 0; index < count; index++) {
        if ((index & 7) == 0) {
            value = random_next();
        }
        bytes[index] = (u8)(value >> ((index & 7) * 8));
    }
}
