#ifndef Z_OS_TIMER_H
#define Z_OS_TIMER_H

#include "types.h"

#define TIMER_FREQUENCY 100

void timer_init(u32 frequency);
u64 timer_ticks(void);
u32 timer_frequency(void);
u64 timer_uptime_ms(void);
void timer_irq_handler(void);

#endif
