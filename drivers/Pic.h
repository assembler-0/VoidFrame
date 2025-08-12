#ifndef PIC_H
#define PIC_H
#include "stdint.h"

void PicInstall();
void PitInstall();
void PitSetFrequency(uint16_t hz);
void PIC_enable_irq(uint8_t irq_line);
void PIC_disable_irq(uint8_t irq_line);

#endif