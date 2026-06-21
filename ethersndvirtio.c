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
	vendor_id 				  = 0x1AF4,
	device_id 				  = 0x1041,
	cap_cfg_vendor   		  = 0x9,
	cap_common_cfg_type 	  = 0x1,
	cap_notification_cfg_type = 0x2,
	cap_isr_cfg_type		  = 0x3,
	cap_device_cfg_type 	  = 0x4,
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
typedef struct virtio_notif_cap virtio_notif_cap;
typedef struct Ctlr Ctlr;


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

struct virtio_notif_cap{
	struct{
		u8int cap_vendor;
		u8int cap_next; 
		u8int cap_len;
		u8int cap_cfg_type;
		u8int cap_bar;
		u8int cap_id;	  
		u32int cap_bar_offset;
		u32int cap_bar_len;
	} cap;
	u32int notify_off_multiplier;
};

struct Ctlr{
	/* for cleanup */
	u32int net_len, common_len, notif_len, isr_len;

	Pcidev				  *p;
	virtio_net_cfg 		  *net_cfg;
	virtio_pci_common_cfg *common_cfg;
	virtio_notif_cap 	  *notif_cap;
	u8int 				  *isr_reg;
	Ctlr *next;
};

static Ctlr *ctlrhead = nil;

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

	cfg->device_status |= FEATURES_OK;
	if ((cfg->device_status & FEATURES_OK) != FEATURES_OK){
		print("feature negotiation error\n");
		return 1;
	}
	return 0;
}

int
get_cfg(void **cfg, Pcidev *p, u8int cap)
{
	u8int bar;
	u32int off, len;

	bar = pcicfgr8(p, cap + cap_bar);
	off = pcicfgr32(p, cap + cap_bar_offset);
	len = pcicfgr32(p, cap + cap_bar_len);
	if (len < 1){
		print("\nbad bar len\n");
		return 0;
	}
	//print("bar: %d; off: 0x%08X; len: 0x%08X\n", bar, off, len);
	*cfg = vmap((p->mem[bar].bar & ~0xF) + off, len); /* ~0xF because bits 0-3 used for bar type definition */
	if (*cfg == nil){
		//print("\ncommon_cfg is nil!\n");
		return 0;
	}
	return len;
}

void
snd_virtio_attach(Ether *edev)
{
	print("\nattach");
	while(1) {}
}

void
snd_virtio_transmit(Ether *edev)
{
	print("\ntransmit");
	while(1) {}
}

void
snd_virtio_shutdown(Ether *edev)
{
	print("\nshutdown");
	while(1) {}
}

int
reset(Ether *edev)
{
	Pcidev *p;
	u8int cap;
	u8int vendor, cfg_type;
	u32int len;

	Ctlr *ctlr;

	p = nil;
	p = pcimatch(p, vendor_id, device_id);
	if(p == nil)
		goto err;
	
	if(ctlrhead != nil){
		for(ctlr = ctlrhead; ctlr != nil; ctlr = ctlr->next){
			if(ctlr->p->tbdf == p->tbdf)
				return -1;
		}
	}
	
	if (p->rid < 1) {
		print("\nRev < 1!\n");
		goto err;
	}

	ctlr = mallocz(sizeof(Ctlr), 1);
	if(ctlr == nil){
		print("Can't allocate ctlr\n");
		goto err;
	}
	ctlr->p = p;

	cap = pcicfgr8(p, PciCAP);
	while (cap) {
		vendor = pcicfgr8(p, cap + cap_vendor);
		cfg_type = pcicfgr8(p, cap + cap_cfg_type);
		if (vendor == cap_cfg_vendor && cfg_type == cap_common_cfg_type){
			//print("common\n");
			if((len = get_cfg(&ctlr->common_cfg, p, cap)) == 0){
				print("\ncommon_cfg is nil!\n");
				goto err;
			}
			ctlr->common_len = len;
		} 
		else if(vendor == cap_cfg_vendor && cfg_type == cap_device_cfg_type){
			//print("device\n");
			if((len = get_cfg(&ctlr->net_cfg, p, cap)) == 0){
				print("\nnet_cfg is nil!\n");
				goto err;
			}
			ctlr->net_len = len;
			
		}
		else if(vendor == cap_cfg_vendor && cfg_type == cap_isr_cfg_type){
			//print("isr\n");
			if((len = get_cfg(&ctlr->isr_reg, p, cap)) == 0){
				print("\nisr_cfg is nil!\n");
				goto err;
			}
			ctlr->isr_len = len;
		}
		else if(vendor == cap_cfg_vendor && cfg_type == cap_notification_cfg_type){
			//print("notification\n");
			if((len = get_cfg(&ctlr->notif_cap, p, cap)) == 0){
				print("\nnotif_cap is nil!\n");
				goto err;
			}
			ctlr->notif_len = len;
		}
		
		cap = pcicfgr8(p, cap + cap_next);
	}

	if (ctlr->common_cfg == nil || ctlr->net_cfg == nil || ctlr->notif_cap == nil || ctlr->isr_reg == nil){
		print("\nDevice discovery error\n");
		goto err;
	}
	//
	//print("cfg_type: 0x%02X\n", cfg_type);
	
	//print("\nbar: %d\noff: 0x%08X\nlen: 0x%08X\n", bar, off, len);


	/* TODO maybe delete retry loop */
	int retry = 0;
	//print("reset ether\n");
	ctlr->common_cfg->device_status = 0;
	while(retry < 1024){
		if(ctlr->common_cfg->device_status == 0)
			break;
		retry++;
	}
	if (ctlr->common_cfg->device_status == 0){}
		//print("ether was reset\n");
	else{
		print("reset error\n");
		goto err;
	}

	ctlr->common_cfg->device_status |= ACK;
	ctlr->common_cfg->device_status |= DRIVER;
	
	if(feature_negotiation(ctlr->common_cfg) != 0){
		print("negotiation error\n");
		goto err;
	}

	ctlrhead = ctlr;
	ctlr->next = nil;
	edev->ctlr = ctlr;
	edev->attach = snd_virtio_attach;
	edev->transmit = snd_virtio_transmit;
	edev->shutdown = snd_virtio_shutdown;
	edev->mbps = 100;
	edev->irq = ctlr->p->intl;
	edev->tbdf = ctlr->p->tbdf;
	edev->maxmtu = 1500;
	edev->port = p->mem[4].bar & ~0xF; /* hardcoded */
	
	for(int i = 0; i < Eaddrlen; i++){
		edev->ea[i] = ctlr->net_cfg->mac[i];
	}
	print("\nWe are here\n");
	return 0;
	
err:
	if(ctlr != nil){
		if(ctlr->common_len)
			vunmap((void*)ctlr->common_cfg, ctlr->common_len);
		if(ctlr->net_len)
			vunmap((void*)ctlr->net_cfg, ctlr->net_len);
		if(ctlr->notif_len)
			vunmap((void*)ctlr->notif_cap, ctlr->notif_len);
		if(ctlr->isr_len)
			vunmap((void*)ctlr->isr_reg, ctlr->isr_len);
		free(ctlr);
	}
	if(p)
		pcidisable(p);
	print("\nmemory freed\n");
	return -1;
	while(1) {}
	//return 0;
}

void
ethersndvirtiolink(void)
{
	addethercard("ethersndvirtio", reset);
}