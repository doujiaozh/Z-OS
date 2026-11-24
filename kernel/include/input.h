#ifndef Z_OS_INPUT_H
#define Z_OS_INPUT_H

#include "types.h"

#define KEY_UP 0x80
#define KEY_DOWN 0x81
#define KEY_LEFT 0x82
#define KEY_RIGHT 0x83
#define KEY_HOME 0x84
#define KEY_END 0x85
#define KEY_DELETE 0x86

void input_init(void);
void input_push(char character);
bool input_available(void);
int input_getchar(void);
void input_flush(void);

#endif
