#include "acpi.h"
#include "cpu.h"
#include "string.h"
#include "printf.h"

static uint16_t acpi_pm1a_port  = 0;
static uint16_t acpi_slp_typa   = 5;   /* default S5 type */

/* ---- ACPI table structures --------------------------------------- */
typedef struct PACKED {
    char     sig[8];
    uint8_t  checksum;
    char     oem[6];
    uint8_t  revision;
    uint32_t rsdt_addr;
} RSDP;

typedef struct PACKED {
    char     sig[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem[6];
    char     oem_table[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} ACPIHeader;

typedef struct PACKED {
    ACPIHeader hdr;
    uint32_t   entries[];
} RSDT;

typedef struct PACKED {
    ACPIHeader hdr;
    uint32_t   firmware_ctrl;
    uint32_t   dsdt;
    uint8_t    reserved;
    uint8_t    preferred_pm;
    uint16_t   sci_interrupt;
    uint32_t   smi_cmd;
    uint8_t    acpi_enable;
    uint8_t    acpi_disable;
    uint8_t    s4bios_req;
    uint8_t    pstate_ctrl;
    uint32_t   pm1a_evt_blk;
    uint32_t   pm1b_evt_blk;
    uint32_t   pm1a_cnt_blk;
    uint32_t   pm1b_cnt_blk;
} FADT;

/* ---- RSDP search ------------------------------------------------- */
static RSDP *find_rsdp(void)
{
    /* Search EBDA (0x40E is the BDA EBDA segment pointer; suppress GCC bounds warning
       since this is an intentional BIOS data area access via identity map) */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
    uint16_t ebda_seg = *(volatile uint16_t *)0x40E;
#pragma GCC diagnostic pop
    uint8_t *ebda = (uint8_t *)(uintptr_t)(ebda_seg << 4);
    for (int i = 0; i < 1024 - 8; i += 16) {
        if (memcmp(ebda + i, "RSD PTR ", 8) == 0)
            return (RSDP *)(ebda + i);
    }
    /* Search BIOS ROM 0xE0000–0xFFFFF */
    for (uint8_t *p = (uint8_t *)0xE0000; p < (uint8_t *)0x100000; p += 16) {
        if (memcmp(p, "RSD PTR ", 8) == 0)
            return (RSDP *)p;
    }
    return NULL;
}

/* ---- Parse \_S5_ AML object to get SLP_TYPa -------------------- */
static void parse_s5(uint8_t *dsdt, uint32_t len)
{
    /* Search for package containing \_S5_ */
    for (uint32_t i = 0; i < len - 8; i++) {
        if (dsdt[i]   == '_' && dsdt[i+1] == 'S' &&
            dsdt[i+2] == '5' && dsdt[i+3] == '_') {
            /* Skip NameOp and PackageOp */
            uint8_t *p = dsdt + i + 4;
            if (*p == 0x08) p++;   /* NameOp */
            if (*p == 0x12) p++;   /* PackageOp */
            p++;                   /* PkgLength */
            p++;                   /* NumElements */
            /* First element = SLP_TYPa */
            if (*p == 0x0A) {
                acpi_slp_typa = *(p + 1);
            } else {
                acpi_slp_typa = *p;
            }
            return;
        }
    }
}

void acpi_init(void)
{
    RSDP *rsdp = find_rsdp();
    if (!rsdp) {
        kprintf("       ACPI: no RSDP found\n");
        return;
    }

    RSDT *rsdt = (RSDT *)(uintptr_t)rsdp->rsdt_addr;
    if (memcmp(rsdt->hdr.sig, "RSDT", 4) != 0) return;

    uint32_t entries = (rsdt->hdr.length - sizeof(ACPIHeader)) / 4;
    for (uint32_t i = 0; i < entries; i++) {
        ACPIHeader *hdr = (ACPIHeader *)(uintptr_t)rsdt->entries[i];
        if (memcmp(hdr->sig, "FACP", 4) == 0) {
            FADT *fadt = (FADT *)hdr;
            acpi_pm1a_port = (uint16_t)fadt->pm1a_cnt_blk;

            /* Parse DSDT for _S5_ */
            ACPIHeader *dsdt = (ACPIHeader *)(uintptr_t)fadt->dsdt;
            if (memcmp(dsdt->sig, "DSDT", 4) == 0) {
                uint8_t *body = (uint8_t *)dsdt + sizeof(ACPIHeader);
                uint32_t blen = dsdt->length - sizeof(ACPIHeader);
                parse_s5(body, blen);
            }
            kprintf("       ACPI: PM1a=0x%x SLP_TYPa=%d\n",
                    acpi_pm1a_port, acpi_slp_typa);
            return;
        }
    }
    kprintf("       ACPI: no FADT\n");
}

void acpi_shutdown(void)
{
    cli_asm();
    /* QEMU/Bochs/VirtualBox shortcuts first */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    /* Standard ACPI S5 */
    if (acpi_pm1a_port)
        outw(acpi_pm1a_port, (uint16_t)((acpi_slp_typa << 10) | (1 << 13)));
    halt_loop();
}
