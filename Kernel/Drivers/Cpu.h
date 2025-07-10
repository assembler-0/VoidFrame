#ifndef CPU_H
#define CPU_H

#include "stdint.h"
typedef struct {
    uint8_t sse:1;
    uint8_t sse2:1;
    uint8_t avx:1;
    uint8_t avx2:1;
} CpuFeatures;

void CpuInit(void);
CpuFeatures* GetCpuFeatures(void);
void EnableSse(void);

#endif