#ifndef Z_OS_PIC_H
#define Z_OS_PIC_H

#include "types.h"

#define PIC_IRQ_TIMER 0
#define PIC_IRQ_KEYBOARD 1
#define PIC_IRQ_CASCADE 2
#define PIC_IRQ_SERIAL 4

void pic_remap(void);
void pic_mask_all(void);
void pic_set_mask(u8 irq, bool masked);
void pic_send_eoi(u8 irq);

#endif
