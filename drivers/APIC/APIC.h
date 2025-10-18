#ifndef APIC_H
#define APIC_H

#include <stdint.h>
#include <stdbool.h>

// Main initialization function to detect and set up both Local APIC and I/O APIC.
// Returns true on success, false on failure (e.g., no APIC found).
bool ApicInstall();

// Replaces PIC_enable_irq. Unmasks an interrupt line in the I/O APIC.
void ApicEnableIrq(uint8_t irq_line);

// Replaces PIC_disable_irq. Masks an interrupt line in the I/O APIC.
void ApicDisableIrq(uint8_t irq_line);

// Replaces PICMaskAll. Masks all interrupts at the I/O APIC level.
void ApicMaskAll();

// Replaces PICSendEOI. Sends End-of-Interrupt signal to the Local APIC.
void ApicSendEoi();

// Replaces PitInstall and PitSetFrequency.
// Initializes and starts the Local APIC timer at the specified frequency.
void ApicTimerInstall(uint32_t frequency_hz);

// Changes the APIC timer's frequency on the fly.
void ApicTimerSetFrequency(uint32_t frequency_hz);

void PICMaskAll();

// Get the current CPU's LAPIC ID
uint8_t lapic_get_id();

#endif // APIC_H
