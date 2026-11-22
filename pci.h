#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 32

typedef struct {
    uint8_t  bus, dev, func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  header_type;
    uint32_t bar[6];
    uint8_t  irq_line;
    uint8_t  present;
} pci_device_t;

void pci_init(void);
int  pci_count(void);
const pci_device_t *pci_get(int idx);
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t val);

#endif
