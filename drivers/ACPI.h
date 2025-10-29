#pragma once
#include "stdint.h"
#include "stdbool.h"

// ACPI Table signatures
#define ACPI_RSDP_SIG "RSD PTR "
#define ACPI_RSDT_SIG "RSDT"
#define ACPI_FADT_SIG "FACP"

// ACPI structures
typedef struct {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
} __attribute__((packed)) ACPIRSDPv1;

typedef struct {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) ACPISDTHeader;

typedef struct {
    ACPISDTHeader header;
    uint32_t table_pointers[];
} __attribute__((packed)) ACPIRSDT;

typedef struct {
    ACPISDTHeader header;
    uint32_t firmware_ctrl;
    uint32_t dsdt;
    uint8_t reserved;
    uint8_t preferred_pm_profile;
    uint16_t sci_interrupt;
    uint32_t smi_command_port;
    uint8_t acpi_enable;
    uint8_t acpi_disable;
    uint8_t s4bios_req;
    uint8_t pstate_control;
    uint32_t pm1a_event_block;
    uint32_t pm1b_event_block;
    uint32_t pm1a_control_block;
    uint32_t pm1b_control_block;
    uint32_t pm2_control_block;
    uint32_t pm_timer_block;
    uint32_t gpe0_block;
    uint32_t gpe1_block;
    uint8_t pm1_event_length;
    uint8_t pm1_control_length;
    uint8_t pm2_control_length;
    uint8_t pm_timer_length;
    uint8_t gpe0_length;
    uint8_t gpe1_length;
    uint8_t gpe1_base;
    uint8_t cstate_control;
    uint16_t worst_c2_latency;
    uint16_t worst_c3_latency;
    uint16_t flush_size;
    uint16_t flush_stride;
    uint8_t duty_offset;
    uint8_t duty_width;
    uint8_t day_alarm;
    uint8_t month_alarm;
    uint8_t century;
    uint16_t boot_architecture_flags;
    uint8_t reserved2;
    uint32_t flags;
} __attribute__((packed)) ACPIFADT;

// Power management defines
#define ACPI_SLP_TYP_MASK 0x1C00
#define ACPI_SLP_EN       0x2000

// Function declarations
bool ACPIInit(void);
void ACPIShutdown(void);
void ACPIReboot(void);
void* AcpiFindTable(const char* signature);