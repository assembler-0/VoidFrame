#include <Random.h>
#include <../crypto/RNG.h>
#include <../fs/CharDevice.h>

static int RandomDevRead(struct CharDevice* dev, void* buffer, uint32_t size) {
    uint8_t* buf = (uint8_t*)buffer;
    if (rdrand_supported()) {
        for (uint32_t i = 0; i < size; i += 2) {
            uint16_t rand = rdrand16();
            buf[i] = rand & 0xFF;
            if (i + 1 < size) {
                buf[i + 1] = (rand >> 8) & 0xFF;
            }
        }
    } else {
        for (uint32_t i = 0; i < size; i += 8) {
            uint64_t rand = xoroshiro128plus();
            for (int j = 0; j < 8 && (i + j) < size; ++j) {
                buf[i + j] = (rand >> (j * 8)) & 0xFF;
            }
        }
    }
    return size;
}

static CharDevice_t g_random_device = {
    .name = "Random",
    .Read = RandomDevRead,
    .Write = NULL, // Not supported
};

void RandomInit(void) {
    if (!rdrand_supported()) {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        uint64_t tsc = ((uint64_t)hi << 32) | lo;
        uint64_t entropy = tsc ^ (uint64_t)(uintptr_t)&g_random_device;
        rng_seed(tsc, entropy);
    }
    CharDeviceRegister(&g_random_device);
}
