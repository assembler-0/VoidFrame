#ifndef IO_H
#define IO_H

#include "stdint.h"

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

typedef uint64_t irq_flags_t;

static inline irq_flags_t save_irq_flags(void) {
    irq_flags_t flags;
    asm volatile("pushfq\n\tpopq %0" : "=r"(flags));
    return flags;
}

static inline void restore_irq_flags(irq_flags_t flags) {
    asm volatile("pushq %0\n\tpopfq" : : "r"(flags));
}

static inline void cli(void) {
    asm volatile("cli");
}

static inline void sti(void) {
    asm volatile("sti");
}

#endif

