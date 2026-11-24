#ifndef Z_OS_STRING_H
#define Z_OS_STRING_H

#include "types.h"

void *memcpy(void *destination, const void *source, usize count);
void *memmove(void *destination, const void *source, usize count);
void *memset(void *destination, int value, usize count);
int memcmp(const void *left, const void *right, usize count);
usize strlen(const char *text);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, usize count);
char *strcpy(char *destination, const char *source);
char *strncpy(char *destination, const char *source, usize count);
char *strchr(const char *text, int character);
char *strrchr(const char *text, int character);
char *skip_spaces(char *text);
char *trim(char *text);
int atoi(const char *text);
int is_space(char character);
int is_digit(char character);
char to_upper(char character);
char to_lower(char character);
void random_bytes(void *buffer, usize count);

#endif
