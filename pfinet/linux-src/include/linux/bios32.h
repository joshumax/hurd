/*
 *	This is only a stub file to make drivers not yet converted to the new
 *	PCI probing mechanism work. [mj]
 */

#ifndef BIOS32_H
#define BIOS32_H

#include <linux/pci.h>

#warning This driver uses the old PCI interface, please fix it (see Documentation/pci.txt)

extern inline int __pcibios_read_irq(unsigned char bus, unsigned char dev_fn, unsigned char *to)
{
	struct pci_dev *pdev = pci_find_slot(bus, dev_fn);
	if (!pdev) {
		*to = 0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	} else {
		*to = pdev->irq;
		return PCIBIOS_SUCCESSFUL;
	}
}

extern inline int __pcibios_read_config_byte(unsigned char bus,
	unsigned char dev_fn, unsigned char where, unsigned char *to)
{
	return pcibios_read_config_byte(bus, dev_fn, where, to);
}

#define pcibios_read_config_byte(b,d,w,p) \
	(((w) == PCI_INTERRUPT_LINE) ? __pcibios_read_irq(b,d,p) : __pcibios_read_config_byte(b,d,w,p))

#endif
