#ifndef OPIC_H
#define OPIC_H

#include <stdint.h>
#include <stdbool.h>

// =================================================================================================
// WARNING: Deprecated Technology
// The OpenPIC architecture is a deprecated technology from the mid-1990s.
// This driver is provided for educational or compatibility purposes with specific legacy hardware
// (like the AMD ÉlanSC520). It is not intended for use in modern systems, which use APIC.
// =================================================================================================

/**
 * @brief Main initialization function to detect and set up the OpenPIC.
 *
 * This function attempts to map the OpenPIC's MMIO registers, performs a hardware reset,
 * and masks all interrupt sources.
 *
 * @return true on success, false on failure (e.g., failed to map memory).
 */
bool OpicInstall();

/**
 * @brief Masks all interrupt sources in the OpenPIC.
 *
 * This function iterates through all available interrupt sources and sets the mask bit
 * in their corresponding Vector/Priority Register (IVPR), effectively disabling them.
 */
void OpicMaskAll();

/**
 * @brief Sends an End-of-Interrupt (EOI) signal to the OpenPIC.
 *
 * This informs the OpenPIC that the interrupt service routine has completed, allowing it
 * to deliver the next pending interrupt.
 */
void OpicSendEoi();

#endif // OPIC_H
