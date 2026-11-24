#ifndef Z_OS_SERIAL_H
#define Z_OS_SERIAL_H

#include "types.h"

void serial_init(void);
void serial_putc(char character);
void serial_write(const char *text);
bool serial_rx_ready(void);
char serial_getc(void);
void serial_enable_rx_interrupt(void);
void serial_irq_handler(void);

#endif
