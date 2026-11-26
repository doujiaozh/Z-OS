#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

u32 pci_read_config32(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 address = 0x80000000U | ((u32)bus << 16) | ((u32)slot << 11) |
                  ((u32)function << 8) | ((u32)offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

u16 pci_read_config16(u8 bus, u8 slot, u8 function, u8 offset) {
    u32 value = pci_read_config32(bus, slot, function, offset & 0xFC);
    return (u16)((offset & 2) != 0 ? (value >> 16) : value);
}

void pci_write_config32(u8 bus, u8 slot, u8 function, u8 offset, u32 value) {
    u32 address = 0x80000000U | ((u32)bus << 16) | ((u32)slot << 11) |
                  ((u32)function << 8) | ((u32)offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

bool pci_find_device(u16 vendor, u16 device, struct pci_device *result) {
    if (result == NULL) {
        return false;
    }
    for (u32 bus = 0; bus < 8; bus++) {
        for (u32 slot = 0; slot < 32; slot++) {
            for (u32 function = 0; function < 8; function++) {
                u32 identifier = pci_read_config32((u8)bus, (u8)slot, (u8)function, 0);
                if ((identifier & 0xFFFF) == 0xFFFF) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }
                u16 found_vendor = (u16)(identifier & 0xFFFF);
                u16 found_device = (u16)(identifier >> 16);
                if (found_vendor != vendor || found_device != device) {
                    if (function == 0) {
                        break;
                    }
                    continue;
                }
                u32 class_register = pci_read_config32((u8)bus, (u8)slot, (u8)function, 0x08);
                result->bus = (u8)bus;
                result->slot = (u8)slot;
                result->function = (u8)function;
                result->vendor = found_vendor;
                result->device = found_device;
                result->class_code = (u8)(class_register >> 24);
                result->subclass = (u8)(class_register >> 16);
                u32 interrupt_register = pci_read_config32((u8)bus, (u8)slot, (u8)function, 0x3C);
                result->interrupt_line = (u8)(interrupt_register & 0xFF);
                result->interrupt_pin = (u8)((interrupt_register >> 8) & 0xFF);
                result->bar0 = pci_read_config32((u8)bus, (u8)slot, (u8)function, 0x10);
                return true;
            }
        }
    }
    return false;
}

u32 pci_bar_address(const struct pci_device *device, u8 index) {
    if (device == NULL || index > 5) {
        return 0;
    }
    u32 value = pci_read_config32(device->bus, device->slot, device->function,
                                  (u8)(0x10 + index * 4));
    return value & 0xFFFFFFF0U;
}

u64 pci_bar_physical(const struct pci_device *device, u8 index) {
    return (u64)pci_bar_address(device, index);
}

void pci_enable_device(const struct pci_device *device) {
    if (device == NULL) {
        return;
    }
    u32 command = pci_read_config32(device->bus, device->slot, device->function, 0x04);
    command |= 0x00000006U;
    pci_write_config32(device->bus, device->slot, device->function, 0x04, command);
}
