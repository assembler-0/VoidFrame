#ifndef INTERRUPT_CONTROLLER_H
#define INTERRUPT_CONTROLLER_H

#include "stdint.h"

// Interrupt controller type
typedef enum {
    INTC_PIC = 0,
    INTC_APIC = 1
} interrupt_controller_t;

// Unified interrupt controller interface
void InterruptControllerInstall(void);
void InterruptControllerEnable(void);
void InterruptControllerDisable(void);
void IC_enable_irq(uint8_t irq_line);
void IC_disable_irq(uint8_t irq_line);
void InterruptControllerSendEOI(void);
void InterruptControllerSetTimer(uint32_t frequency_hz);

// Query functions
interrupt_controller_t GetInterruptControllerType(void);
const char* GetInterruptControllerName(void);

// Migration control
void SetInterruptControllerMode(interrupt_controller_t mode);
int InterruptControllerFallback(void);

#endif