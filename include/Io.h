#ifndef IO_H
#define IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outsb(uint16_t port, void* buf, size_t len) {
    __asm__ volatile ("cld; rep outsb" : "+D"(buf), "+c"(len) : "d"(port));
}

static inline void insb(uint16_t port, void* buf, size_t len) {
    __asm__ volatile ("cld; rep insb" : "+D"(buf), "+c"(len) : "d"(port));
}

static inline void outsl(uint16_t port, void* buf, size_t len) {
    __asm__ volatile ("cld; rep outsl" : "+D"(buf), "+c"(len) : "d"(port));
}

static inline void insl(uint16_t port, void* buf, size_t len) {
    __asm__ volatile ("cld; rep insl" : "+D"(buf), "+c"(len) : "d"(port));
}

static inline void outsw(uint16_t port, void* buf, size_t len) {
    __asm__ volatile ("cld; rep outsw" : "+D"(buf), "+c"(len) : "d"(port));
}

static inline void insw(uint16_t port, void* buf, size_t len) {
    __asm__ volatile ("cld; rep insw" : "+D"(buf), "+c"(len) : "d"(port));
}

void cli(void);
void sti(void);

typedef uint64_t irq_flags_t;

irq_flags_t save_irq_flags(void);
void restore_irq_flags(irq_flags_t flags);

// CPUID detection
void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx);

// MSR access
uint64_t rdmsr(uint32_t msr);
void wrmsr(uint32_t msr, uint64_t value);

#ifdef __cplusplus
}
#endif

#endif

