# Timing System Upgrade Guide

## Overview

This upgrade replaces your basic PIC/PIT timing with a modern, multi-precision timing system that automatically selects the best available hardware:

- **HPET** (High Precision Event Timer) - Microsecond precision
- **TSC** (Time Stamp Counter) - Nanosecond precision (when calibrated)
- **PIT** (Programmable Interval Timer) - Millisecond precision (fallback)

## What's New

### 1. HPET Driver (`drivers/HPET.h/c`)
- Automatic detection of HPET hardware
- Sub-microsecond timing precision
- TSC calibration using HPET as reference
- Memory-mapped I/O interface

### 2. Unified Timing Interface (`drivers/Timing.h/c`)
- Automatic hardware selection
- Precision-aware delay functions
- TSC calibration and conversion
- Fallback mechanisms for compatibility

### 3. Enhanced Delay Functions
- `TimingDelay(ms)` - Millisecond delays
- `TimingDelayUs(us)` - Microsecond delays  
- `TimingDelayNs(ns)` - Nanosecond delays
- Automatic precision selection

## Integration Steps

### Step 1: Initialize Timing System

Add this to your kernel initialization (e.g., in `Kernel.c`):

```c
#include "Timing.h"

void KernelInit(void) {
    // ... existing initialization ...
    
    // Initialize timing system (should be early)
    TimingInit();
    
    // ... rest of initialization ...
}
```

### Step 2: Replace Old Delay Calls

Replace your existing `delay()` calls with the new timing functions:

```c
// Old way
delay(1000000);  // Unpredictable timing

// New way
TimingDelay(1);      // 1 millisecond
TimingDelayUs(1000); // 1000 microseconds
TimingDelayNs(1000000); // 1000000 nanoseconds
```

### Step 3: Use TSC for High-Precision Timing

```c
#include "Timing.h"

// Get TSC value
uint64_t start = TimingGetTsc();

// ... do work ...

uint64_t end = TimingGetTsc();
uint64_t cycles = end - start;

// Convert to time (if calibrated)
if (TimingTscIsCalibrated()) {
    uint64_t microseconds = TimingTscToUs(cycles);
    PrintKernel("Operation took %llu us\n", microseconds);
}
```

## API Reference

### Core Functions
- `TimingInit()` - Initialize timing system
- `TimingDelay(ms)` - Delay in milliseconds
- `TimingDelayUs(us)` - Delay in microseconds
- `TimingDelayNs(ns)` - Delay in nanoseconds

### TSC Functions
- `TimingGetTsc()` - Get current TSC value
- `TimingTscToUs(cycles)` - Convert TSC cycles to microseconds
- `TimingUsToTsc(us)` - Convert microseconds to TSC cycles
- `TimingTscIsCalibrated()` - Check if TSC is calibrated

### Status Functions
- `TimingGetPrecision()` - Get current precision level
- `TimingGetDriverName()` - Get current driver name
- `TimingPrintStats()` - Print timing statistics

## Precision Levels

1. **Low (PIT)**: 1ms resolution, always available
2. **Medium (HPET)**: ~100ns resolution, modern hardware
3. **High (TSC)**: ~1ns resolution, when calibrated

## Benefits

- **Immediate**: Better delay precision, TSC calibration
- **Future**: Foundation for APIC timer integration
- **Compatibility**: Works alongside existing PIC/PIT
- **Performance**: Sub-microsecond timing for critical operations

## Testing

Use the included test program:

```c
#include "drivers/timing_test.c"

void TestTiming(void) {
    TimingTest();  // Run comprehensive timing tests
}
```

## Next Steps

After this is working, you can:

1. **Integrate with scheduler** - Use HPET for precise time slices
2. **Add APIC timer support** - Replace PIT with APIC timer
3. **Implement sleep/wake** - Use HPET for process scheduling
4. **Add real-time features** - Microsecond-precise timing

## Troubleshooting

### HPET Not Detected
- Check if hardware supports HPET
- Verify ACPI tables (future enhancement)
- Falls back to PIT automatically

### TSC Not Calibrating
- HPET must be available for calibration
- Check CPU TSC support
- Falls back to HPET timing

### Performance Issues
- Use appropriate precision level
- Avoid high-precision delays in hot paths
- Consider TSC for measurement, HPET for delays
