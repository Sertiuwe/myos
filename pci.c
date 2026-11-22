#include "pci.h"
#include "kstdio.h"
#include "kstring.h"

#define PCI_CONF_ADDR 0xCF8
#define PCI_CONF_DATA 0xCFC

static pci_device_t devices[PCI_MAX_DEVICES];
static int ndev;

static void outb(uint16_t p, uint8_t v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static void outl(uint16_t p, uint32_t v){__asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p));}
static uint32_t inl(uint16_t p){uint32_t v;__asm__ volatile("inl %1,%0":"=a"(v):"Nd"(p));return v;}

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONF_ADDR, addr);
    return inl(PCI_CONF_DATA);
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(PCI_CONF_ADDR, addr);
    outl(PCI_CONF_DATA, val);
}

static const char *pci_class_name(uint8_t cc, uint8_t sc) {
    switch (cc) {
    case 0x00: return "Unclassified";
    case 0x01:
        switch (sc) {
        case 0x00: return "SCSI Controller";
        case 0x01: return "IDE Controller";
        case 0x05: return "RAID Controller";
        case 0x06: return "SATA Controller";
        default:   return "Mass Storage";
        }
    case 0x02:
        switch (sc) {
        case 0x00: return "Ethernet";
        case 0x80: return "Other Network";
        default:   return "Network";
        }
    case 0x03: return "Display/VGA";
    case 0x04: return "Multimedia";
    case 0x06:
        switch (sc) {
        case 0x00: return "Host Bridge";
        case 0x01: return "ISA Bridge";
        case 0x04: return "PCI-PCI Bridge";
        default:   return "Bridge";
        }
    case 0x07: return "Simple Comm";
    case 0x08: return "Base System";
    case 0x09: return "Input Device";
    case 0x0C: return "Serial Bus";
    default:   return "Unknown";
    }
}

static void scan_func(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t vid_did = pci_read32(bus, dev, func, 0x00);
    uint16_t vendor = (uint16_t)(vid_did & 0xFFFF);
    uint16_t device = (uint16_t)(vid_did >> 16);
    if (vendor == 0xFFFF) return;

    if (ndev >= PCI_MAX_DEVICES) return;
    pci_device_t *d = &devices[ndev];
    memset(d, 0, sizeof(*d));
    d->bus = bus; d->dev = dev; d->func = func;
    d->vendor_id = vendor;
    d->device_id = device;

    uint32_t class_rev = pci_read32(bus, dev, func, 0x08);
    d->class_code = (uint8_t)(class_rev >> 24);
    d->subclass   = (uint8_t)(class_rev >> 16);
    d->prog_if    = (uint8_t)(class_rev >> 8);

    uint32_t hdr = pci_read32(bus, dev, func, 0x0C);
    d->header_type = (uint8_t)((hdr >> 16) & 0x7F);

    /* Читаем BARs только для type 0 (endpoint) */
    if (d->header_type == 0) {
        for (int i = 0; i < 6; i++)
            d->bar[i] = pci_read32(bus, dev, func, (uint8_t)(0x10 + i * 4));
        uint32_t irq_reg = pci_read32(bus, dev, func, 0x3C);
        d->irq_line = (uint8_t)(irq_reg & 0xFF);
    }

    d->present = 1;
    ndev++;

    kprintf("PCI %02x:%02x.%x %04x:%04x [%s] IRQ=%u\n",
            bus, dev, func, vendor, device,
            pci_class_name(d->class_code, d->subclass),
            d->irq_line);
}

static void scan_dev(uint8_t bus, uint8_t dev) {
    uint32_t vid_did = pci_read32(bus, dev, 0, 0x00);
    if ((vid_did & 0xFFFF) == 0xFFFF) return;

    scan_func(bus, dev, 0);

    /* Multi-function? */
    uint32_t hdr = pci_read32(bus, dev, 0, 0x0C);
    if ((hdr >> 16) & 0x80) {
        for (uint8_t f = 1; f < 8; f++)
            scan_func(bus, dev, f);
    }
}

void pci_init(void) {
    ndev = 0;
    memset(devices, 0, sizeof(devices));
    kprintf("PCI: scanning bus 0...\n");
    for (uint8_t d = 0; d < 32; d++)
        scan_dev(0, d);
    kprintf("PCI: found %d device(s)\n", ndev);
}

int pci_count(void) { return ndev; }
const pci_device_t *pci_get(int idx) {
    return (idx >= 0 && idx < ndev) ? &devices[idx] : NULL;
}
