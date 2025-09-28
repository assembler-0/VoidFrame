#include "TSC.h"
#include "Cpu.h"
#include "Console.h"

uint64_t tsc_freq_hz = 0;
static bool tsc_calibrated = false;

void TSCInit(void) {
    // Calibrate TSC frequency using APIC timer
    extern volatile uint32_t APIC_HZ;
    extern uint32_t s_apic_bus_freq;
    
    if (APIC_HZ == 0) {
        tsc_freq_hz = 3000000000ULL; // Fallback: 3GHz
        PrintKernelWarning("TSC: Using fallback frequency\n");
        return;
    }
    
    // Use 10ms calibration period
    uint64_t start_tsc = rdtsc();
    uint32_t calibration_ticks = s_apic_bus_freq / 100; // 10 ms worth of APIC ticks
    
    // Wait using APIC timer current count
    extern volatile uint32_t* s_lapic_base;
    uint32_t target = s_lapic_base[0x390/4] - calibration_ticks;
    while (s_lapic_base[0x390/4] > target);
    
    uint64_t end_tsc = rdtsc();
    tsc_freq_hz = (end_tsc - start_tsc) * 100; // Scale to 1 second
    tsc_calibrated = true;
    
    PrintKernelF("TSC: Calibrated frequency: %llu Hz\n", tsc_freq_hz);
}

void delay_us(uint32_t microseconds) {
    if (!tsc_calibrated) return;
    
    uint64_t start = rdtsc();
    uint64_t ticks = (tsc_freq_hz * microseconds) / 1000000;
    
    while ((rdtsc() - start) < ticks);
}

void delay(const uint32_t milliseconds) {
    delay_us(milliseconds * 1000);
}

void delay_s(uint32_t seconds) {
    delay(seconds * 1000);
}

uint64_t TSCGetFrequency(void) {
    return tsc_freq_hz;
}