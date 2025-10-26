#ifndef VOIDFRAME_SHA256_H
#define VOIDFRAME_SHA256_H

#include <stdint.h>
#include <stddef.h>

#define SHA256_BLOCK_SIZE 32

typedef struct {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} SHA256_CTX;

void sha256_init(SHA256_CTX *ctx);
void SHA256Update(SHA256_CTX *ctx, const uint8_t data[], size_t len);
void SHA256Final(SHA256_CTX *ctx, uint8_t hash[]);

#endif //VOIDFRAME_SHA256_H
