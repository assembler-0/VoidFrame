#pragma once
#include "stdint.h"

void TSCInit(void);

uint64_t GetTimeInMs(void);
void delay_us(uint32_t microseconds);
void delay(uint32_t milliseconds);
void delay_s(uint32_t seconds);

uint64_t TSCGetFrequency(void);