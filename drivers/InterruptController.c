#include "InterruptController.h"
#include "APIC.h"
#include "Pic.h"
#include "Console.h"

static interrupt_controller_t current_controller = INTC_PIC;
static int apic_available = 0;

void InterruptControllerInstall(void) {
    PrintKernel("IC: Initializing interrupt controller...\n");
    
    // Try APIC first
    if (ApicDetect()) {
        apic_available = 1;
        ApicInstall();
        current_controller = INTC_APIC;
        PrintKernelSuccess("IC: Using APIC interrupt controller\n");
    } else {
        // Fall back to PIC
        PicInstall();
        current_controller = INTC_PIC;
        PrintKernelSuccess("IC: Using PIC interrupt controller\n");
    }
}

void IC_enable_irq(uint8_t irq_line) {
    switch (current_controller) {
        case INTC_APIC:
            APIC_enable_irq(irq_line);
            break;
        case INTC_PIC:
        default:
            PIC_enable_irq(irq_line);
            break;
    }
}

void IC_disable_irq(uint8_t irq_line) {
    switch (current_controller) {
        case INTC_APIC:
            APIC_disable_irq(irq_line);
            break;
        case INTC_PIC:
        default:
            PIC_disable_irq(irq_line);
            break;
    }
}

void InterruptControllerSendEOI(void) {
    switch (current_controller) {
        case INTC_APIC:
            ApicSendEOI();
            break;
        case INTC_PIC:
        default:
            // PIC EOI is handled directly in interrupt handler
            // via outb(0x20, 0x20) / outb(0xA0, 0x20)
            break;
    }
}

interrupt_controller_t GetInterruptControllerType(void) {
    return current_controller;
}

const char* GetInterruptControllerName(void) {
    switch (current_controller) {
        case INTC_APIC:
            return "APIC";
        case INTC_PIC:
        default:
            return "PIC";
    }
}

void SetInterruptControllerMode(interrupt_controller_t mode) {
    if (mode == INTC_APIC && !apic_available) {
        PrintKernelWarning("IC: APIC not available, staying with PIC\n");
        return;
    }
    
    if (current_controller == mode) {
        return; // Already using requested controller
    }
    
    PrintKernel("IC: Switching interrupt controller from ");
    PrintKernel(GetInterruptControllerName());
    PrintKernel(" to ");
    
    if (mode == INTC_APIC) {
        PrintKernel("APIC\n");
        // Disable PIC
        PIC_disable_irq(0);  // Disable timer
        PIC_disable_irq(1);  // Disable keyboard
        PIC_disable_irq(12); // Disable mouse
        PIC_disable_irq(14); // Disable IDE primary
        PIC_disable_irq(15); // Disable IDE secondary
        PIC_disable_irq(2);  // Disable cascade/FAT12
        
        // Enable APIC
        ApicEnable();
        current_controller = INTC_APIC;
    } else {
        PrintKernel("PIC\n");
        // Disable APIC
        ApicDisable();
        
        // Re-enable PIC
        current_controller = INTC_PIC;
    }
    
    PrintKernelSuccess("IC: Switched to ");
    PrintKernel(GetInterruptControllerName());
    PrintKernel("\n");
}

// Legacy compatibility functions
void InterruptControllerEnable(void) {
    switch (current_controller) {
        case INTC_APIC:
            ApicEnable();
            break;
        case INTC_PIC:
        default:
            // PIC is enabled by default
            break;
    }
}

void InterruptControllerDisable(void) {
    switch (current_controller) {
        case INTC_APIC:
            ApicDisable();
            break;
        case INTC_PIC:
        default:
            // Note: Disabling PIC completely would stop all interrupts
            PrintKernelWarning("IC: Cannot completely disable PIC\n");
            break;
    }
}

void InterruptControllerSetTimer(uint32_t frequency_hz) {
    switch (current_controller) {
        case INTC_APIC:
            // For now, still use PIT even with APIC
            PitSetFrequency(frequency_hz);
            break;
        case INTC_PIC:
        default:
            PitSetFrequency(frequency_hz);
            break;
    }
}

int InterruptControllerFallback(void) {
    if (current_controller == INTC_APIC && apic_available) {
        PrintKernelWarning("IC: APIC error detected, falling back to PIC\n");
        SetInterruptControllerMode(INTC_PIC);
        return 1;
    }
    return 0;
}