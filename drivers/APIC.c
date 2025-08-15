#include "APIC.h"
#include "Console.h"

// APIC base address (will be detected from MSR)
static volatile uint32_t* apic_base = NULL;
static volatile uint32_t* ioapic_base = NULL;
static uint8_t apic_id = 0;
static uint32_t ioapic_max_redirections = 0;

// IRQ masking state (same as PIC for compatibility)
static uint32_t irq_mask = 0xFFFFFFFF; // All masked initially

// CPUID detection
static inline void cpuid(uint32_t leaf, uint32_t* eax, uint32_t* ebx, uint32_t* ecx, uint32_t* edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf));
}

// MSR access
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    __asm__ volatile("wrmsr" :: "a"(low), "d"(high), "c"(msr));
}

int ApicDetect(void) {
    uint32_t eax, ebx, ecx, edx;
    
    // Check if CPUID is available first
    cpuid(1, &eax, &ebx, &ecx, &edx);
    
    // Check for APIC support (bit 9 of EDX)
    if (!(edx & (1 << 9))) {
        PrintKernelError("APIC: Local APIC not supported by CPU\n");
        return 0;
    }
    
    PrintKernelSuccess("APIC: Local APIC detected\n");
    return 1;
}

int IoApicDetect(void) {
    // For now, assume I/O APIC is at standard location 0xFEC00000
    // In a full implementation, this would be detected via ACPI MADT
    ioapic_base = (volatile uint32_t*)0xFEC00000;
    
    // Try to read I/O APIC version register
    uint32_t version = IoApicRead(IOAPIC_REG_VERSION);
    if (version == 0xFFFFFFFF) {
        PrintKernelError("APIC: I/O APIC not found at standard location\n");
        return 0;
    }
    
    ioapic_max_redirections = ((version >> 16) & 0xFF) + 1;
    PrintKernelSuccess("APIC: I/O APIC detected with ");
    PrintKernelInt(ioapic_max_redirections);
    PrintKernel(" redirection entries\n");
    return 1;
}

uint32_t ApicRead(uint32_t reg) {
    if (!apic_base) return 0xFFFFFFFF;
    return *(volatile uint32_t*)((uint8_t*)apic_base + reg);
}

void ApicWrite(uint32_t reg, uint32_t value) {
    if (!apic_base) return;
    *(volatile uint32_t*)((uint8_t*)apic_base + reg) = value;
}

uint32_t IoApicRead(uint32_t reg) {
    if (!ioapic_base) return 0xFFFFFFFF;
    *(volatile uint32_t*)((uint8_t*)ioapic_base + IOAPIC_REG_SELECT) = reg;
    return *(volatile uint32_t*)((uint8_t*)ioapic_base + IOAPIC_REG_DATA);
}

void IoApicWrite(uint32_t reg, uint32_t value) {
    if (!ioapic_base) return;
    *(volatile uint32_t*)((uint8_t*)ioapic_base + IOAPIC_REG_SELECT) = reg;
    *(volatile uint32_t*)((uint8_t*)ioapic_base + IOAPIC_REG_DATA) = value;
}

void ApicSetupLVT(void) {
    // Configure Local Vector Table entries
    
    // Mask all LVT entries initially
    ApicWrite(APIC_REG_LVT_TIMER, APIC_INT_MASKED);
    ApicWrite(APIC_REG_LVT_THERMAL, APIC_INT_MASKED);
    ApicWrite(APIC_REG_LVT_PERF, APIC_INT_MASKED);
    ApicWrite(APIC_REG_LVT_ERROR, APIC_INT_MASKED);
    
    // Configure LINT0 and LINT1 for compatibility
    // LINT0: External interrupts (for compatibility with PIC mode)
    ApicWrite(APIC_REG_LVT_LINT0, APIC_DELMODE_EXTINT | APIC_INT_UNMASKED);
    
    // LINT1: NMI
    ApicWrite(APIC_REG_LVT_LINT1, APIC_DELMODE_NMI | APIC_INT_UNMASKED);
}

void ApicInstall(void) {
    PrintKernel("APIC: Starting Local APIC initialization...\n");
    
    // Detect APIC support
    if (!ApicDetect()) {
        PrintKernelError("APIC: Local APIC detection failed\n");
        return;
    }
    
    // Get APIC base address from MSR
    uint64_t apic_msr = rdmsr(0x1B); // IA32_APIC_BASE MSR
    uint64_t apic_base_addr = apic_msr & 0xFFFFF000;
    
    PrintKernel("APIC: Local APIC base address: ");
    PrintKernelHex(apic_base_addr);
    PrintKernel("\n");
    
    // Map APIC base address (in real implementation, use virtual memory)
    apic_base = (volatile uint32_t*)apic_base_addr;
    
    // Enable APIC in MSR
    wrmsr(0x1B, apic_msr | (1 << 11)); // Set APIC Global Enable bit
    
    // Get APIC ID
    apic_id = (ApicRead(APIC_REG_ID) >> 24) & 0xFF;
    PrintKernel("APIC: Local APIC ID: ");
    PrintKernelInt(apic_id);
    PrintKernel("\n");
    
    // Setup Spurious Interrupt Vector Register
    // Vector 0xFF (255) for spurious interrupts, enable APIC
    ApicWrite(APIC_REG_SIVR, 0xFF | (1 << 8));
    
    // Setup Local Vector Table
    ApicSetupLVT();
    
    PrintKernelSuccess("APIC: Local APIC initialized\n");
    
    // Initialize I/O APIC
    if (IoApicDetect()) {
        // Set up I/O APIC redirection table entries for standard IRQs
        // This maintains compatibility with existing IRQ assignments
        
        // IRQ 0 (Timer) -> Vector 32
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 0*2, 32 | APIC_DELMODE_FIXED | APIC_INT_MASKED);
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 0*2 + 1, apic_id << 24);
        
        // IRQ 1 (Keyboard) -> Vector 33
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 1*2, 33 | APIC_DELMODE_FIXED | APIC_INT_MASKED);
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 1*2 + 1, apic_id << 24);
        
        // IRQ 2 (Cascade/FAT12) -> Vector 34
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 2*2, 34 | APIC_DELMODE_FIXED | APIC_INT_MASKED);
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 2*2 + 1, apic_id << 24);
        
        // IRQ 12 (Mouse) -> Vector 44
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 12*2, 44 | APIC_DELMODE_FIXED | APIC_INT_MASKED);
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 12*2 + 1, apic_id << 24);
        
        // IRQ 14 (IDE Primary) -> Vector 46
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 14*2, 46 | APIC_DELMODE_FIXED | APIC_INT_MASKED);
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 14*2 + 1, apic_id << 24);
        
        // IRQ 15 (IDE Secondary) -> Vector 47
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 15*2, 47 | APIC_DELMODE_FIXED | APIC_INT_MASKED);
        IoApicWrite(IOAPIC_REG_REDTBL_BASE + 15*2 + 1, apic_id << 24);
        
        PrintKernelSuccess("APIC: I/O APIC initialized with IRQ mappings\n");
    }
}

void APIC_enable_irq(uint8_t irq_line) {
    if (irq_line > 15) return;
    
    // Update our mask state
    irq_mask &= ~(1 << irq_line);
    
    // Configure I/O APIC redirection table entry
    if (ioapic_base && irq_line < ioapic_max_redirections) {
        uint32_t vector = 32 + irq_line; // Same mapping as PIC
        uint32_t low_reg = IOAPIC_REG_REDTBL_BASE + irq_line * 2;
        uint32_t high_reg = low_reg + 1;
        
        // Configure redirection entry: vector, fixed delivery, edge triggered, active high
        IoApicWrite(low_reg, vector | APIC_DELMODE_FIXED | APIC_TRIGMOD_EDGE | APIC_INTPOL_HIGH);
        IoApicWrite(high_reg, apic_id << 24); // Target this CPU
    }
}

void APIC_disable_irq(uint8_t irq_line) {
    if (irq_line > 15) return;
    
    // Update our mask state
    irq_mask |= (1 << irq_line);
    
    // Mask the I/O APIC redirection table entry
    if (ioapic_base && irq_line < ioapic_max_redirections) {
        uint32_t vector = 32 + irq_line;
        uint32_t low_reg = IOAPIC_REG_REDTBL_BASE + irq_line * 2;
        uint32_t high_reg = low_reg + 1;
        
        // Mask the interrupt
        IoApicWrite(low_reg, vector | APIC_DELMODE_FIXED | APIC_INT_MASKED);
        IoApicWrite(high_reg, apic_id << 24);
    }
}

void ApicSendEOI(void) {
    if (apic_base) {
        ApicWrite(APIC_REG_EOI, 0);
    }
}

void ApicSetupTimer(uint32_t frequency_hz) {
    if (!apic_base) return;
    
    // For now, disable APIC timer and rely on existing PIT
    ApicWrite(APIC_REG_LVT_TIMER, APIC_INT_MASKED);
    
    // Future: Implement APIC timer setup
    // This would replace PIT functionality
}

void ApicEnable(void) {
    if (!apic_base) return;
    
    // Enable APIC by setting bit 8 in SIVR
    uint32_t sivr = ApicRead(APIC_REG_SIVR);
    ApicWrite(APIC_REG_SIVR, sivr | (1 << 8));
    
    PrintKernelSuccess("APIC: Local APIC enabled\n");
}

void ApicDisable(void) {
    if (!apic_base) return;
    
    // Disable APIC by clearing bit 8 in SIVR
    uint32_t sivr = ApicRead(APIC_REG_SIVR);
    ApicWrite(APIC_REG_SIVR, sivr & ~(1 << 8));
    
    PrintKernel("APIC: Local APIC disabled\n");
}