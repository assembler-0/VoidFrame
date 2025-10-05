#ifndef KERNEL_HEAP_RUST_H
#define KERNEL_HEAP_RUST_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Rust heap functions with C-compatible API
void* rust_kmalloc(size_t size);
void rust_kfree(void* ptr);
void* rust_krealloc(void* ptr, size_t new_size);
void* rust_kcalloc(size_t count, size_t size);

// Compatibility macros to replace existing heap functions
#define kmalloc(size) rust_kmalloc(size)
#define kfree(ptr) rust_kfree(ptr)
#define krealloc(ptr, size) rust_krealloc(ptr, size)
#define kcalloc(count, size) rust_kcalloc(count, size)

#ifdef __cplusplus
}
#endif

#endif // KERNEL_HEAP_RUST_H