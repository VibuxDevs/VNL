#include "pci.h"
#include "cpu.h"
#include "string.h"
#include "printf.h"

#define PCI_ADDR 0xCF8
#define PCI_DATA 0xCFC
#define PCI_MAX_DEVS 128

static PCIDevice pci_devs[PCI_MAX_DEVS];
static int       pci_count = 0;

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off)
{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    outl(PCI_ADDR, addr);
    return inl(PCI_DATA);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val)
{
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (off & 0xFC);
    outl(PCI_ADDR, addr);
    outl(PCI_DATA, val);
}

static const char *pci_class_str(uint8_t cls, uint8_t sub)
{
    switch (cls) {
        case 0x00: return sub == 0x01 ? "VGA (legacy)" : "Unknown";
        case 0x01:
            switch (sub) {
                case 0x00: return "SCSI Controller";
                case 0x01: return "IDE Controller";
                case 0x06: return "SATA Controller";
                case 0x08: return "NVMe Controller";
                default:   return "Storage Controller";
            }
        case 0x02:
            switch (sub) {
                case 0x00: return "Ethernet Controller";
                case 0x80: return "Network Controller";
                default:   return "Network Controller";
            }
        case 0x03: return "Display Controller";
        case 0x04: return "Multimedia Device";
        case 0x05: return "Memory Controller";
        case 0x06:
            switch (sub) {
                case 0x00: return "Host Bridge";
                case 0x01: return "ISA Bridge";
                case 0x04: return "PCI-PCI Bridge";
                default:   return "Bridge Device";
            }
        case 0x07: return "Communication Controller";
        case 0x08: return "System Peripheral";
        case 0x09: return "Input Device";
        case 0x0A: return "Docking Station";
        case 0x0B: return "Processor";
        case 0x0C:
            switch (sub) {
                case 0x03: return "USB Controller";
                case 0x05: return "SMBus Controller";
                default:   return "Serial Bus Controller";
            }
        case 0x0D: return "Wireless Controller";
        case 0xFF: return "Unassigned";
        default:   return "Unknown Device";
    }
}

static void scan_function(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint32_t id = pci_read32(bus, dev, func, 0x00);
    if ((id & 0xFFFF) == 0xFFFF) return;   /* no device */
    if (pci_count >= PCI_MAX_DEVS) return;

    PCIDevice *d = &pci_devs[pci_count++];
    d->bus        = bus;
    d->dev        = dev;
    d->func       = func;
    d->vendor     = (uint16_t)(id & 0xFFFF);
    d->device_id  = (uint16_t)(id >> 16);

    uint32_t cls  = pci_read32(bus, dev, func, 0x08);
    d->rev        = (uint8_t)(cls & 0xFF);
    d->prog_if    = (uint8_t)((cls >> 8) & 0xFF);
    d->subclass   = (uint8_t)((cls >> 16) & 0xFF);
    d->class_code = (uint8_t)((cls >> 24) & 0xFF);

    uint32_t hdr  = pci_read32(bus, dev, func, 0x0C);
    d->header_type = (uint8_t)((hdr >> 16) & 0xFF);

    strncpy(d->desc, pci_class_str(d->class_code, d->subclass), sizeof(d->desc) - 1);
    d->desc[sizeof(d->desc) - 1] = '\0';
}

static void scan_device(uint8_t bus, uint8_t dev)
{
    uint32_t id = pci_read32(bus, dev, 0, 0x00);
    if ((id & 0xFFFF) == 0xFFFF) return;

    scan_function(bus, dev, 0);

    uint32_t hdr = pci_read32(bus, dev, 0, 0x0C);
    if ((hdr >> 16) & 0x80) {          /* multi-function device */
        for (uint8_t f = 1; f < 8; f++) {
            uint32_t fid = pci_read32(bus, dev, f, 0x00);
            if ((fid & 0xFFFF) != 0xFFFF)
                scan_function(bus, dev, f);
        }
    }
}

void pci_init(void)
{
    pci_count = 0;
    for (int bus = 0; bus < 256; bus++)
        for (int dev = 0; dev < 32; dev++)
            scan_device((uint8_t)bus, (uint8_t)dev);
    kprintf("       Found %d PCI device(s)\n", pci_count);
}

int             pci_dev_count(void)        { return pci_count; }
const PCIDevice *pci_get_dev(int idx)     { return (idx < pci_count) ? &pci_devs[idx] : NULL; }
