#include "timer.h"
#include "io.h"
#include "pic.h"
#include "thread.h"
#include "sched.h"

static volatile u64 ticks = 0;
static u32 configured_frequency = TIMER_FREQUENCY;

void timer_init(u32 frequency) {
    configured_frequency = frequency;
    u32 divisor = 1193182 / frequency;
    outb(0x43, 0x36);
    outb(0x40, (u8)(divisor & 0xFF));
    outb(0x40, (u8)((divisor >> 8) & 0xFF));
    ticks = 0;
    pic_set_mask(PIC_IRQ_TIMER, false);
}

u64 timer_ticks(void) {
    return ticks;
}

u32 timer_frequency(void) {
    return configured_frequency;
}

u64 timer_uptime_ms(void) {
    return ticks * 1000 / configured_frequency;
}

void timer_irq_handler(void) {
    ticks++;
    sched_user_tick();
    sched_tick();
}
