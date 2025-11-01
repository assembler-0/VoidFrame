#include "Random.h"
#include "../crypto/RNG.h"
#include "../fs/CharDevice.h"

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
        // In a real scenario, a better source of entropy should be used.
        // For now, we use a fixed seed.
        rng_seed(0xDEADBEEF, 0xCAFEBABE);
    }
    CharDeviceRegister(&g_random_device);
}
