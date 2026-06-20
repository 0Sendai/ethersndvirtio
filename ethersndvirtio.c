#include "u.h"
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"
#include "../port/pci.h"
#include "../port/netif.h"
#include "../port/etherif.h"

#define GET_FEAT(buf, f) ((buf) >> (f)) & 1
#define SET_FEAT(buf, f) (buf) |= 1ULL << (f) 

enum{
	vendor_id 			= 0x1AF4,
	device_id 			= 0x1041,
	cap_cfg_vendor   		= 0x9,
	cap_common_cfg_type = 0x1,
	cap_device_cfg_type = 0x4,
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

enum{ /* Virtio status field bits */
	ACK 			   = 1,
	DRIVER 			   = 2,
	FAILED			   = 128,
	FEATURES_OK 	   = 8,
	DRIVER_OK 		   = 4,
	DEVICE_NEEDS_RESET = 64,
};

enum{ /* Virtio features */
	VIRTIO_F_VERSION_1 = 32,
	VIRTIO_NET_F_MAC   = 5,
};

typedef struct virtio_pci_common_cfg virtio_pci_common_cfg;
typedef struct virtio_net_cfg virtio_net_cfg;

struct virtio_pci_common_cfg{  /* About the who device. */
    u32int device_feature_sect; /* read-write */
    u32int device_feature; /* read-only for driver */
    u32int driver_feature_sect; /* read-write */
    u32int driver_feature; /* read-write */
    u16int config_msix_vector; /* read-write */
    u16int num_queues; /* read-only for driver */
    u8int  device_status; /* read-write */
    u8int  config_generation; /* read-only for driver */

    /* About a specific virtqueue. */
    u16int queue_sect; /* read-write */
    u16int queue_size; /* read-write */
    u16int queue_msix_vector; /* read-write */
    u16int queue_enab; /* read-write */
    u16int queue_notify_off; /* read-only for driver */
    u64int queue_desc; /* read-write */
    u64int queue_driver; /* read-write */
    u64int queue_device; /* read-write */
    u16int queue_notif_config_data; /* read-only for driver */
    u16int queue_reset; /* read-write */

    /* About the administration virtqueue. */
    u16int admin_queue_index; /* read-only for driver */
    u16int admin_queue_num; /* read-only for driver */
};

struct virtio_net_cfg{
    u8int mac[6];
    u16int status;
    u16int max_virtqueue_pairs;
    u16int mtu;
    u32int speed;
    u8int dupx;
    u8int rss_max_key_size;
    u16int rss_max_indirection_tab_ngth;
    u32int supported_hash_types;
    u32int supported_tunnel_types;
};

int
feature_negotiation(virtio_pci_common_cfg* cfg)
{
	u64int feat, accepted_feat;
	u32int feat_up, feat_lo;
	u8int supported_features[] = {
		VIRTIO_F_VERSION_1,
		VIRTIO_NET_F_MAC,
	};
	u8int feat_idx;

	cfg->device_feature_sect = 0;
	feat_lo = cfg->device_feature;
	cfg->device_feature_sect = 1;
	feat_up = cfg->device_feature;
	if (feat_lo == feat_up){
		print("featbuf ident\n");
		return 1;
	}
	feat = ((u64int)feat_up << 32) | feat_lo;
	accepted_feat = 0;

	for(feat_idx = 0; feat_idx < nelem(supported_features); feat_idx++){
		if(GET_FEAT(feat, supported_features[feat_idx])){
			print("supported: %d\n", supported_features[feat_idx]);
			SET_FEAT(accepted_feat, supported_features[feat_idx]);
		}
	}

	cfg->driver_feature_sect = 0;
	cfg->driver_feature = (u32int)(accepted_feat & 0xFFFF);
	cfg->driver_feature_sect = 1;
	cfg->driver_feature = (u32int)((accepted_feat >> 32) & 0xFFFF);

	cfg->device_status = FEATURES_OK;
	if (cfg->device_status != FEATURES_OK){
		print("feature negotiation error\n");
		return 1;
	}
	return 0;
}

int
reset(Ether*)
{
	Pcidev *p;
	u8int cap = pcicfgr8(p, PciCAP);
	u8int vendor, cfg_type;
	u8int bar;
	u32int off, len;
	virtio_net_cfg *net_cfg;
	virtio_pci_common_cfg *common_cfg;

	p = nil;
	p = pcimatch(p, vendor_id, device_id);
	
	if (p->rid < 1) {
		print("\nRev < 1!\n");
		goto w;
	}

	common_cfg = net_cfg = nil;
	while (cap) {
		vendor = pcicfgr8(p, cap + cap_vendor);
		cfg_type = pcicfgr8(p, cap + cap_cfg_type);
		if (vendor == cap_cfg_vendor && cfg_type == cap_common_cfg_type){
			print("\ncommon\nvendor: 0x%02X\ncfg_type: 0x%02X\n", vendor, cfg_type);
			bar = pcicfgr8(p, cap + cap_bar);
			off = pcicfgr32(p, cap + cap_bar_offset);
			len = pcicfgr32(p, cap + cap_bar_len);
			if (len < 1){
				print("\nbad bar len\n");
				goto w;
			}
			print("\nbar: %d\noff: 0x%08X\nlen: 0x%08X\n", bar, off, len);
			common_cfg = vmap((p->mem[bar].bar & ~0xF) + off, len);
			if (common_cfg == nil){
				print("\ncommon_cfg is nil!\n");
				goto w;
			}
		} 
		else if(vendor == cap_cfg_vendor && cfg_type == cap_device_cfg_type){
			print("\ndevice\nvendor: 0x%02X\ncfg_type: 0x%02X\n", vendor, cfg_type);
			bar = pcicfgr8(p, cap + cap_bar);
			off = pcicfgr32(p, cap + cap_bar_offset);
			len = pcicfgr32(p, cap + cap_bar_len);
			if (len < 1){
				print("\nbad bar len\n");
				goto w;
			}
			print("\nbar: %d\noff: 0x%08X\nlen: 0x%08X\n", bar, off, len);
			net_cfg = vmap((p->mem[bar].bar & ~0xF) + off, len);
			if(net_cfg == nil){
				print("\nnet_cfg is nil!");
				goto w;
			}
			
		}
		
		cap = pcicfgr8(p, cap + cap_next);
	}

	if (common_cfg == nil || net_cfg == nil){
		print("\nDevice discovery error\n");
		goto w;
	}
	//
	//print("cfg_type: 0x%02X\n", cfg_type);
	
	//print("\nbar: %d\noff: 0x%08X\nlen: 0x%08X\n", bar, off, len);


	/* TODO maybe delete retry loop */
	int retry = 0;
	print("reset ether\n");
	common_cfg->device_status = 0;
	while(retry < 1024){
		if(common_cfg->device_status == 0)
			break;
		retry++;
	}
	if (common_cfg->device_status == 0)
		print("ether was reset\n");
	else{
		print("reset error\n");
		goto w;
	}

	common_cfg->device_status = ACK;
	common_cfg->device_status = DRIVER;
	
	if(feature_negotiation(common_cfg) != 0){
		print("negotiation error\n");
		goto w;
	}

	/* get network config */
	print("mac: ");
	for(int i = 0; i < nelem(net_cfg->mac); i++)
		print("%02X:", net_cfg->mac[i]);

	
	print("\nWe are here\n");
	
w:
	while(1) {}
	//return 0;
}

void
ethersndvirtiolink(void)
{
	addethercard("ethersndvirtio", reset);
}