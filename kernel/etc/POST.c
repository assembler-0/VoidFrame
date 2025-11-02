#include <POST.h>
#include <Console.h>
#include <KernelHeap.h>
#include <PMem.h>
#include <Panic.h>
#include <Serial.h>
#include <VMem.h>
#include <stdbool.h>
#include <x64.h>

#define N 512
void * ptrs[N] = {0};

uint64_t seed = 0x12345;
uint64_t rnd() { seed = seed * 6364136223846793005ULL + 1; return seed; }

bool SerialTest() {
    for (int i = 0; i < 128; i++) {
        if (SerialWrite(".") < 0) return false;
    }
    return true;
}

bool MemoryTest() {
    uint64_t start = rdtsc();
    for (int i = 1; i < 1000; i++) {
        size_t sz = (i % 7 == 0) ? 4096 : (i % 100 + 1);
        void *ptr = KernelMemoryAlloc(sz);
        if (!ptr) return false;
        KernelFree(ptr);
    }
    PrintKernelF("Loop 1 took: %llu sycles\n", rdtsc() - start);

    start = rdtsc();
    for (int i = 0; i < N; i++) ptrs[i] = KernelMemoryAlloc(128);
    PrintKernelF("Loop 2 took: %llu sycles\n", rdtsc() - start);

    start = rdtsc();
    // free every other block
    for (int i = 0; i < N; i += 2) KernelFree(ptrs[i]);
    PrintKernelF("Loop 3 took: %llu sycles\n", rdtsc() - start);

    start = rdtsc();
    // re-allocate in different sizes
    for (int i = 0; i < N/2; i++) {
        ptrs[i] = KernelMemoryAlloc((i % 2) ? 64 : 256);
    }
    PrintKernelF("Loop 4 took: %llu sycles\n", rdtsc() - start);

    start = rdtsc();
    for (int i = 0; i < 1000; i++) {
        size_t sz = (i % 500) + 1;
        uint8_t *p = (uint8_t*)KernelMemoryAlloc(sz);
        for (size_t j = 0; j < sz; j++) p[j] = (uint8_t)(i ^ j);
        for (size_t j = 0; j < sz; j++)
            if (p[j] != (uint8_t)(i ^ j)) PANIC("Memory corruption!");
        KernelFree(p);
    }
    PrintKernelF("Loop 5 took: %llu sycles\n", rdtsc() - start);

    return true;
}

void POSTHandler(const char * args) {
    (void)args;
    if (!SerialTest()) PrintKernelWarning("Serial test failed\n");
    if (!MemoryTest()) PrintKernelWarning("Memory test failed\n");
    PrintKernelSuccess("POST test passed\n");
}