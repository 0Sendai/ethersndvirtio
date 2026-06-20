#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/netif.h"
#include "../port/etherif.h"

typedef struct virtio_pci_cap virtio_pci_cap;
struct virtio_pci_cap
{
	u8int cap_vndr; /* Generic PCI field: PCI_CAP_ID_VNDR */
	u8int cap_next; /* Generic PCI field: next ptr. */
	u8int cap_len; /* Generic PCI field: capability length */
	u8int cfg_type; /* Identifies the structure. */
	u8int bar; /* Where to find it. */
	u8int id; /* Multiple capabilities of the same type */
	u8int padding[2]; /* Pad to full dword. */
	u32int offset; /* Offset within bar. */
	u32int length;
};

int
reset(Ether*)
{
	//print("\n\nHello from reset func!\n\n");
	Pcidev *p;
	p = nil;
	p = pcimatch(p, 0x1AF4, 0x1041);
	
	if (p->rid < 1) {
		print("\nRev < 1!\n");
		goto w;
	}
	u8int cap = pcicfgr8(p, PciCAP);
	u8int vendor, cfg_type;
	while (cap) {
		vendor = pcicfgr8(p, cap);
		cfg_type = pcicfgr8(p, cap+3);
		if (vendor == 9 && cfg_type == 1)
			break;
		cap = pcicfgr8(p, cap+1);
	}
	print("\nvendor: 0x%02X\n", vendor);
	print("cfg_type: 0x%02X\n", cfg_type);
	
w:
	while(1) {}
	//return 0;
}

void
ethersndvirtiolink(void)
{
	addethercard("ethermyvirtio", reset);
}