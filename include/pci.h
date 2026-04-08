#pragma once
#include "types.h"

typedef struct {
    uint8_t  bus, dev, func;
    uint16_t vendor, device_id;
    uint8_t  class_code, subclass, prog_if, rev;
    uint8_t  header_type;
    char     desc[48];
} PCIDevice;

void            pci_init(void);
int             pci_dev_count(void);
const PCIDevice *pci_get_dev(int idx);
uint32_t        pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off);
void            pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val);
