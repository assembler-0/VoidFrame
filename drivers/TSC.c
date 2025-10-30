#include "TSC.h"
#include "x64.h"
#include "Console.h"
#include "APIC/APIC.h"

uint64_t tsc_freq_hz = 0;
static bool tsc_calibrated = false;

void TSCInit(void) {
    // Calibrate TSC frequency using APIC timer
    extern volatile uint32_t APIC_HZ;
    PerCpuData* cpu_data = GetPerCpuData();
    
    if (APIC_HZ == 0) {
        tsc_freq_hz = 3000000000ULL; // Fallback: 3GHz
        tsc_calibrated = true; // no-op fix
        PrintKernelWarning("TSC: Using fallback frequency\n");
        return;
    }
    
    // Use 10ms calibration period
    uint64_t start_tsc = rdtsc();
    uint32_t calibration_ticks = cpu_data->apic_bus_freq / 100; // 10 ms worth of APIC ticks

    // Wait using APIC timer current count
    uint32_t target = cpu_data->lapic_base[0x390/4] - calibration_ticks;
    while (cpu_data->lapic_base[0x390/4] > target);
    
    uint64_t end_tsc = rdtsc();
    tsc_freq_hz = (end_tsc - start_tsc) * 100; // Scale to 1 second
    tsc_calibrated = true;
    
    PrintKernelF("TSC: Calibrated frequency: %llu Hz\n", tsc_freq_hz);
}

uint64_t GetTimeInMs(void) {
    if (!tsc_calibrated) return 0;
    return (rdtsc() * 1000) / tsc_freq_hz;
}

void delay_us(uint32_t microseconds) {
    if (!tsc_calibrated) return;
    
    uint64_t start = rdtsc();
    uint64_t ticks = (tsc_freq_hz * microseconds) / 1000000;
    
    while ((rdtsc() - start) < ticks);
}


void delay(uint32_t milliseconds) {
    uint64_t us = (uint64_t)milliseconds * 1000ULL;
    if (us > UINT32_MAX) us = UINT32_MAX;  // clamp to API
    delay_us((uint32_t)us);
}
void delay_s(uint32_t seconds) {
    if (seconds > UINT32_MAX / 1000U) seconds = UINT32_MAX / 1000U;
    delay(seconds * 1000U);
}

uint64_t TSCGetFrequency(void) {
    return tsc_freq_hz;
}