#ifndef Z_OS_PCI_H
#define Z_OS_PCI_H

#include "types.h"

#define PCI_VENDOR_INTEL 0x8086
#define PCI_DEVICE_E1000 0x100E
#define PCI_DEVICE_E1000E 0x10D3

struct pci_device {
    u8 bus;
    u8 slot;
    u8 function;
    u16 vendor;
    u16 device;
    u8 class_code;
    u8 subclass;
    u8 interrupt_line;
    u8 interrupt_pin;
    u32 bar0;
};

u32 pci_read_config32(u8 bus, u8 slot, u8 function, u8 offset);
u16 pci_read_config16(u8 bus, u8 slot, u8 function, u8 offset);
void pci_write_config32(u8 bus, u8 slot, u8 function, u8 offset, u32 value);
bool pci_find_device(u16 vendor, u16 device, struct pci_device *result);
u32 pci_bar_address(const struct pci_device *device, u8 index);
u64 pci_bar_physical(const struct pci_device *device, u8 index);
void pci_enable_device(const struct pci_device *device);

#endif
