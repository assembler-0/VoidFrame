#ifndef PIC_H
#define PIC_H
#include "stdint.h"
void PicInstall();
void PitInstall();
void PitSetFrequency(uint16_t hz);
#endif