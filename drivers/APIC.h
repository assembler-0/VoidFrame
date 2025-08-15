#ifndef APIC_H
#define APIC_H

#include "stdint.h"

// APIC Register offsets (from APIC base)
#define APIC_REG_ID             0x020   // APIC ID
#define APIC_REG_VERSION        0x030   // Version
#define APIC_REG_TPR            0x080   // Task Priority Register
#define APIC_REG_APR            0x090   // Arbitration Priority Register  
#define APIC_REG_PPR            0x0A0   // Processor Priority Register
#define APIC_REG_EOI            0x0B0   // End of Interrupt
#define APIC_REG_RRD            0x0C0   // Remote Read Register
#define APIC_REG_LDR            0x0D0   // Logical Destination Register
#define APIC_REG_DFR            0x0E0   // Destination Format Register
#define APIC_REG_SIVR           0x0F0   // Spurious Interrupt Vector Register
#define APIC_REG_ISR            0x100   // In-Service Register (256 bits)
#define APIC_REG_TMR            0x180   // Trigger Mode Register (256 bits)
#define APIC_REG_IRR            0x200   // Interrupt Request Register (256 bits)
#define APIC_REG_ESR            0x280   // Error Status Register
#define APIC_REG_ICR_LOW        0x300   // Interrupt Command Register (low 32 bits)
#define APIC_REG_ICR_HIGH       0x310   // Interrupt Command Register (high 32 bits)
#define APIC_REG_LVT_TIMER      0x320   // LVT Timer Register
#define APIC_REG_LVT_THERMAL    0x330   // LVT Thermal Sensor Register
#define APIC_REG_LVT_PERF       0x340   // LVT Performance Counter Register
#define APIC_REG_LVT_LINT0      0x350   // LVT LINT0 Register
#define APIC_REG_LVT_LINT1      0x360   // LVT LINT1 Register
#define APIC_REG_LVT_ERROR      0x370   // LVT Error Register
#define APIC_REG_TIMER_ICR      0x380   // Timer Initial Count Register
#define APIC_REG_TIMER_CCR      0x390   // Timer Current Count Register
#define APIC_REG_TIMER_DCR      0x3E0   // Timer Divide Configuration Register

// I/O APIC register offsets
#define IOAPIC_REG_SELECT       0x00    // Register Select
#define IOAPIC_REG_DATA         0x10    // Register Data
#define IOAPIC_REG_ID           0x00    // I/O APIC ID
#define IOAPIC_REG_VERSION      0x01    // I/O APIC Version
#define IOAPIC_REG_ARB          0x02    // I/O APIC Arbitration
#define IOAPIC_REG_REDTBL_BASE  0x10    // Redirection Table Base

// APIC delivery modes
#define APIC_DELMODE_FIXED      0x00000000
#define APIC_DELMODE_LOWEST     0x00000100
#define APIC_DELMODE_SMI        0x00000200
#define APIC_DELMODE_NMI        0x00000400
#define APIC_DELMODE_INIT       0x00000500
#define APIC_DELMODE_SIPI       0x00000600
#define APIC_DELMODE_EXTINT     0x00000700

// APIC flags
#define APIC_DEST_PHYSICAL      0x00000000
#define APIC_DEST_LOGICAL       0x00000800
#define APIC_INTPOL_HIGH        0x00000000
#define APIC_INTPOL_LOW         0x00002000
#define APIC_TRIGMOD_EDGE       0x00000000
#define APIC_TRIGMOD_LEVEL      0x00008000
#define APIC_INT_MASKED         0x00010000
#define APIC_INT_UNMASKED       0x00000000

// Functions - maintain PIC compatibility
void ApicInstall(void);
void ApicEnable(void);
void ApicDisable(void);
void APIC_enable_irq(uint8_t irq_line);
void APIC_disable_irq(uint8_t irq_line);
void ApicSendEOI(void);
void ApicSetupTimer(uint32_t frequency_hz);

// Internal APIC functions
uint32_t ApicRead(uint32_t reg);
void ApicWrite(uint32_t reg, uint32_t value);
uint32_t IoApicRead(uint32_t reg);
void IoApicWrite(uint32_t reg, uint32_t value);
void ApicSetupLVT(void);

// APIC detection and initialization
int ApicDetect(void);
int IoApicDetect(void);

#endif