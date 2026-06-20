#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/netif.h"
#include "../port/etherif.h"

enum{
	vendor_id 			= 0x1AF4,
	device_id 			= 0x1041,
	cap_common_cfg_type = 0x9,
	cap_common_vendor   = 0x1,
};

enum{ /* PCI capabilities offsets */
	cap_vendor 	   = 0x0, /* Generic PCI field: PCI_CAP_ID_VNDR */
	cap_next 	   = 0x1, /* Generic PCI field: next ptr. */
	cap_len 	   = 0x2, /* Generic PCI field: capability length */
	cap_cfg_type   = 0x3, /* Identifies the structure. */
	cap_bar 	   = 0x4, /* Where to find it. */
	cap_id 		   = 0x5, /* Multiple capabilities of the same type */
	cap_bar_offset = 0x8, /* Offset within bar. */
	cap_bar_len    = 0xC, /* Length of the structure in bar */
};

int
reset(Ether*)
{
	Pcidev *p;
	p = nil;
	p = pcimatch(p, vendor_id, device_id);
	
	if (p->rid < 1) {
		print("\nRev < 1!\n");
		goto w;
	}
	u8int cap = pcicfgr8(p, PciCAP);
	u8int vendor, cfg_type;
	while (cap) {
		vendor = pcicfgr8(p, cap + cap_vendor);
		cfg_type = pcicfgr8(p, cap + cap_cfg_type);
		if (vendor == cap_common_cfg_type && cfg_type == cap_common_vendor)
			break;
		cap = pcicfgr8(p, cap + cap_next);
	}
	print("\nvendor: 0x%02X\n", vendor);
	print("cfg_type: 0x%02X\n", cfg_type);
	u8int bar = pcicfgr8(p, cap + cap_bar);
	u32int off = pcicfgr32(p, cap + cap_bar_offset);
	u32int len = pcicfgr32(p, cap + cap_bar_len);
	print("\nbar: %d\noff: 0x%08X\nlen: 0x%08X", bar, off, len);
	
w:
	while(1) {}
	//return 0;
}

void
ethersndvirtiolink(void)
{
	addethercard("ethersndvirtio", reset);
}