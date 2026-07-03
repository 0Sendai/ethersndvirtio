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
	cap_notify_off_multiplier = 0x10,
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
typedef struct virtq_desc virtq_desc;
typedef struct Vring Vring;
typedef struct Virtq Virtq;
typedef struct virtq_used_elem virtq_used_elem;
typedef struct virtio_net_hdr virtio_net_hdr;
typedef struct Ctlr Ctlr;


#pragma pack on
struct virtio_pci_common_cfg{  /* About the who device. */
    volatile u32int device_feature_select; /* read-write */
    volatile u32int device_feature; /* read-only for driver */
    volatile u32int driver_feature_select; /* read-write */
    volatile u32int driver_feature; /* read-write */
    volatile u16int config_msix_vector; /* read-write */
    volatile u16int num_queues; /* read-only for driver */
    volatile u8int  device_status; /* read-write */
    volatile u8int  config_generation; /* read-only for driver */

    /* About a specific virtqueue. */
    volatile u16int queue_select; /* read-write */
    volatile u16int queue_size; /* read-write */
    volatile u16int queue_msix_vector; /* read-write */
    volatile u16int queue_enable; /* read-write */
    volatile u16int queue_notify_off; /* read-only for driver */
    volatile u64int queue_desc; /* read-write */
    volatile u64int queue_driver; /* read-write */
    volatile u64int queue_device; /* read-write */
    volatile u16int queue_notif_config_data; /* read-only for driver */
    volatile u16int queue_reset; /* read-write */

    /* About the administration virtqueue. */
    volatile u16int admin_queue_index; /* read-only for driver */
    volatile u16int admin_queue_num; /* read-only for driver */
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

enum{
	NET_HDR_F_NEEDS_CSUM = 1,
	NET_HDR_F_DATA_VALID = 2,
	NET_HDR_F_RSC_INFO   = 4,
	NET_HDR_F_GSO_NONE   = 0,
	NET_HDR_F_GSO_TCPV4  = 1,
	NET_HDR_F_GSO_UDP    = 3,
	NET_HDR_F_GSO_TCPV6  = 4,
	NET_HDR_F_GSO_UDP_L4 = 5,
	NET_HDR_F_GSO_ECN    = 0x80,

	ETHERNET_FRAME_MAX_SIZE = 1518,
#define ETHERNET_BUF_SIZE sizeof(virtio_net_hdr) + ETHERNET_FRAME_MAX_SIZE
};

struct virtio_net_hdr{
	u8int  flags;
	u8int  gso_type;
	u16int hdr_len;
	u16int gso_size;
	u16int csum_start;
	u16int csum_offset;
	u16int num_buffers;

};

enum{
	DESC_F_NEXT     = 1,
	DESC_F_WRITE    = 2,
	DESC_F_INDIRECT = 4,
};

struct virtq_desc{
	u64int addr;
	u32int len;
	u16int flags;
	u16int next;
};

enum{
	AVAIL_F_NO_INTERRUPT = 1,
	USED_F_NO_NOTIFY 	 = 1,
};

struct Vring{
	u16int flags;
	u16int idx;
};

struct virtq_used_elem{
	u32int id;
    u32int len;
};

enum{
	RxQueue = 0,
	TxQueue = 1,
};

#pragma pack off

struct Virtq{
	volatile u16int 		 *notif_addr;
			 u16int 	     qsz;
			 virtq_desc  	 *desc;

			 /* virtual memory addresses of used descriptors */
			 u64int 		 **desc_virtual_addresses;

			 /* stack of available descriptor indexes */
			 u16int 		 *free_desc_list;
			 u16int			 free_desc_idx;

			 Vring 			 *avail;
			 u16int			 avail_idx;
    		 u16int      	 *avail_ring;
		   	 u16int      	 *avail_event;

			 Vring 			 *used;
			 virtq_used_elem *used_ring;
			 u16int 		 last_used_idx;
			 u16int          *used_event;			
};

#define NUM_VIRTQ 2 /* TODO add control virtq */
struct Ctlr{
			/* for cleanup TODO maybe remove */
			u32int net_len, common_len, notif_len, isr_len;
	
			Lock;
			QLock 				  qlock;

			Ctlr 				  *next;
			Pcidev				  *p;

  volatile	virtio_net_cfg 		  *net_cfg;
  volatile	virtio_pci_common_cfg *common_cfg;
  volatile	u32int				  *notif_addr;
  volatile  u8int 				  *isr_reg;

			u32int notify_off_multiplier;

  			Virtq virtq[NUM_VIRTQ];
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

	cfg->device_feature_select = 0;
	feat_lo = cfg->device_feature;
	cfg->device_feature_select = 1;
	feat_up = cfg->device_feature;
	if (feat_lo == feat_up){
		print("featbuf ident\n");
		return 1;
	}
	feat = ((u64int)feat_up << 32) | feat_lo;
	accepted_feat = 0;

	for(feat_idx = 0; feat_idx < nelem(supported_features); feat_idx++){
		if(GET_FEAT(feat, supported_features[feat_idx])){
			//print("supported: %d\n", supported_features[feat_idx]);
			SET_FEAT(accepted_feat, supported_features[feat_idx]);
		}
	}

	cfg->driver_feature_select = 0;
	cfg->driver_feature = (u32int)(accepted_feat & 0xFFFFFFFF);
	cfg->driver_feature_select = 1;
	cfg->driver_feature = (u32int)((accepted_feat >> 32) & 0xFFFFFFFF);

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
virtq_notify(u16int *addr, int x)
{
	//print("\n\nnotify; x=%d\n", x);
	coherence();
	*addr = x;
}

int 
queue_init(Virtq *virtq)
{
	Vring  *used, *avail;
	virtq_desc  *desc;
	u16int *avail_ring;
	virtq_used_elem *used_ring;
	u16int *used_event, *avail_event;
	u16int qsz;

	qsz = virtq->qsz;

	//print("alloc for used\n");
	used = mallocalign(6 + sizeof(virtq_used_elem) * qsz, 4, 0, 0);
	if(used == nil){
		print("can't alloc used\n");
		return -1;
	}
	memset(used, 0, 6 + sizeof(virtq_used_elem) * qsz);
	used_ring = (virtq_used_elem*)((u16int*)used + 2);
	used_event = (u16int*)((u8int*)used_ring + sizeof(virtq_used_elem) * qsz);

	//print("alloc for avail\n");
	avail = mallocalign(6 + 2 * qsz, 2, 0, 0);
	if(avail == nil){
		print("can't alloc avail\n");
		free(used);
		return -1;
	}
	memset(avail, 0, 6 + 2 * qsz);
	avail_ring = (u16int*)avail + 2;
	avail_event = (u16int*)((u8int*)avail_ring + 2 * qsz);

	//print("alloc for desc\n\n");
	desc = mallocalign(16 * qsz, 16, 0, 0);
	if(desc == nil){
		print("can't alloc desc\n");
		free(used);
		free(avail);
		return -1;
	}
	memset(desc, 0, 16 * qsz);

	virtq->desc        = desc;
	virtq->avail	   = avail;
	virtq->used        = used;
	virtq->used_ring   = used_ring;
	virtq->avail_ring  = avail_ring;
	virtq->used_event  = used_event;
	virtq->avail_event = avail_event;

	return 0;
}

int
virtq_init(Ctlr *ctlr)
{
	virtio_pci_common_cfg *cfg;
	u32int *notif_addr;
	Virtq *virtq;
	u32int desc_size;
	u16int free_idx;

	cfg = ctlr->common_cfg;
	notif_addr = ctlr->notif_addr;
	//print("sizeof used_elem: %d\n", sizeof(virtq_used_elem));
	//print("nq = %d\n", nq);
	for(int i = 0; i < NUM_VIRTQ; i++){
		virtq = &ctlr->virtq[i];
		cfg->queue_select = i;
		virtq->qsz = cfg->queue_size;
		
		//print("alloc virtq %d\n", i);
		if(queue_init(virtq) < 0){
			print("Can't init virtq %d\n", i);
			goto virtq_error;
		}
		cfg->queue_desc   = PADDR(virtq->desc);
		cfg->queue_driver = PADDR(virtq->avail);
		cfg->queue_device = PADDR(virtq->used);
		//cfg->queue_enable = 1;
		print("queue %d enable=%d ready=%d\n",
    i,
    cfg->queue_enable,
    cfg->queue_enable);
		virtq->desc_virtual_addresses = mallocz(virtq->qsz * sizeof(u64int*), 1);
		if(virtq->desc_virtual_addresses == nil){
			print("can't alloc desc_virtual_addresses\n");
			goto virtq_error;
		}

		virtq->notif_addr = (u16int*)((u8int*)notif_addr + ctlr->notify_off_multiplier * cfg->queue_notify_off);
		//print("notif addr: %p\n", virtq->notif_addr);
		//print("mult: %d\n",  notif_cap->notify_off_multiplier);
	}

	/* tx */
	virtq = &ctlr->virtq[TxQueue];
	virtq->avail_idx = 0;
	virtq->free_desc_list = mallocz(virtq->qsz * 2, 1);
	if (virtq->free_desc_list == nil){
		print("can't alloc free_desc_list\n");
		goto virtq_error;
	}

	for(free_idx = 0; free_idx < virtq->qsz; free_idx++)
		virtq->free_desc_list[free_idx] = free_idx;
	virtq->free_desc_idx = --free_idx;
	cfg->queue_select = TxQueue;

	/* rx */
	virtq = &ctlr->virtq[RxQueue];
	virtq->avail->idx = 0;
	desc_size = ETHERNET_BUF_SIZE;
	for(int i = 0; i < virtq->qsz; i++){
		virtq->desc_virtual_addresses[i] = mallocz(desc_size, 1);
		if(virtq->desc_virtual_addresses[i] == nil){
			print("Can't allocate rx buffer\n");
			goto virtq_error;
		}
		virtq->desc[i].addr = (u64int)PADDR(virtq->desc_virtual_addresses[i]);
		virtq->desc[i].flags = DESC_F_WRITE;
		virtq->desc[i].len = desc_size;
		virtq->avail_ring[i] = i;
		virtq->avail_idx++;
	}
	coherence();
	virtq->avail->idx = virtq->avail_idx;

	
	return 0;

virtq_error: /* TODO add handler for virtq_error */
	print("\nvirtq error\n");
	return -1;
}

/* long because we can return -1 */
long
get_free_desc(Virtq *virtq)
{
	return virtq->free_desc_idx < 0 ? -1 : 
		virtq->free_desc_list[virtq->free_desc_idx--];
}

void
set_free_desc(Virtq *virtq, u16int idx)
{
	u16int free_idx = virtq->free_desc_idx + 1;
	if(free_idx < 0 || free_idx >= virtq->qsz){
		print("free_desc corruption\n");
		return;
	}
	virtq->free_desc_list[++virtq->free_desc_idx] = idx;
}

void
snd_virtio_attach(Ether *edev)
{
	Ctlr *ctlr;
	ctlr = edev->ctlr;
	ctlr->common_cfg->queue_select = RxQueue;
	ctlr->common_cfg->queue_enable = 1;
	ctlr->common_cfg->queue_select = TxQueue;
	ctlr->common_cfg->queue_enable = 1;
	//print("\nattach\n");
	ctlr->common_cfg->device_status |= DRIVER_OK;
}

void
snd_virtio_transmit(Ether *edev)
{
	Ctlr *ctlr;
	Virtq *virtq;
	Block *bp;
	virtq_desc *desc;
	static virtio_net_hdr *hdr = nil;
	u16int normalized_idx;
	long chain_head, chain_tail;
	u32int qsz;
	
	//print("\ntransmit\n");
	ctlr = edev->ctlr;
	virtq = &ctlr->virtq[TxQueue];
	qsz = virtq->qsz;
	if(hdr == nil){
		hdr = mallocz(sizeof(virtio_net_hdr), 1);
		if (hdr == nil){
			print("cannot alloc virtio_net_hdr\n");
			//set_free_desc(virtq, (u16int)chain_head);
			//set_free_desc(virtq, (u16int)chain_tail);
			return;
		}
	}
	//if(!canlock(ctlr))
	//	return;
	while((bp = qget(edev->oq)) != nil){
		chain_head = get_free_desc(virtq);
		if (chain_head < 0){
			print("free_desc underflow\n");
			return;
		}
		chain_tail = get_free_desc(virtq);
		if( chain_tail < 0){
			print("free_desc underflow\n");
			set_free_desc(virtq, (u16int)chain_head);
			return;
		}
		desc = &virtq->desc[chain_head];
		if (hdr == nil){
			print("cannot alloc virtio_net_hdr\n");
			set_free_desc(virtq, (u16int)chain_head);
			set_free_desc(virtq, (u16int)chain_tail);
			return;
		}
		//memmove((u8int*)hdr + sizeof(*hdr), bp->rp, BLEN(bp));
		//virtq->desc_virtual_addresses[chain_head] = (u64int*)hdr;
		desc->addr = (u64int)PADDR(hdr);
		desc->len = sizeof(virtio_net_hdr);
		desc->flags = DESC_F_NEXT;
		//desc->flags = 0;
		//desc->next = 0;
		desc->next = chain_tail;
		desc = &virtq->desc[chain_tail];
		virtq->desc_virtual_addresses[chain_tail] = (u64int*)bp;
		desc->addr = PADDR(bp->rp);
		desc->len = BLEN(bp);
		desc->flags = 0;
		desc->next = 0;

		virtq->avail_ring[virtq->avail_idx++ % qsz] = chain_head;
		
	//virtq->avail->idx = virtq->avail_idx;
	}
	coherence();
	virtq->avail->idx = virtq->avail_idx;
	virtq_notify(virtq->notif_addr, TxQueue);
	//print("free idx %d\n", virtq->free_desc_idx);
}

void
snd_virtio_free_tx(Virtq *virtq)
{
	u16int idx;
	virtq_desc *desc;
	virtq_used_elem elem;
	Block *bp;
	//print("free tx\n");
	while(virtq->last_used_idx != virtq->used->idx){
		idx = virtq->last_used_idx % virtq->qsz;
		elem = virtq->used_ring[idx];
		//if(elem.len == sizeof(virtio_net_hdr)){
		//	print("\nmatch virtio_net_hdr\n");
		//	free(virtq->desc_virtual_addresses[elem.id]);
		//} else{
		//	print("\nAssume that desc is block\n");
		//	//free(KADDR((u64int)virtq->desc[elem.id].addr));
		//	//freeb((Block*)virtq->desc_virtual_addresses[elem.id]);
		//}
		desc = &virtq->desc[elem.id];
		//free(KADDR(desc->addr));
		set_free_desc(virtq, elem.id);
		bp = (Block*)virtq->desc_virtual_addresses[desc->next];
		//if (desc == nil){
		//	print("bad desc next\n");
		//	return;
		//}
		freeb(bp);
		set_free_desc(virtq, desc->next);
		virtq->last_used_idx++;
	}
}

void
snd_virtio_shutdown(Ether *edev)
{
	print("\n\n\n\nshutdown\n\n\n\n");
}

char*
snd_virtio_ifstat(void *a, char *p, char *e)
{
	Ether *edev;
	Ctlr *ctlr;
	
	if(p >= e)
		return p;

	edev = a;
	ctlr = edev->ctlr;
	
	p = seprint(p, e, "inpackets: %lld\n", edev->inpackets);
	p = seprint(p, e, "outpackets: %lld\n", edev->outpackets);
	p = seprint(p, e, "mbps: %d\n", edev->mbps);
	p = seprint(p, e, "link: %d\n", edev->link);
	p = seprint(p, e, "mac: ");
	for(int i = 0; i < Eaddrlen; i++){
		if(i != Eaddrlen - 1)
			p = seprint(p, e, "%02X:", edev->ea[i]);
		else
			p = seprint(p, e, "%02X\n", edev->ea[i]);
	}
	p = seprint(p, e, "maxmtu: %d\n", edev->maxmtu);
	return p;
}
void
snd_virtio_multicast(void *arg, uchar*, int)
{
	print("\nmulticast\n");
}

void
snd_virtio_promiscuous(void *arg, int on)
{
	print("\npromiscuous\n");
}

void
snd_virtio_receive(Ether *edev)
{
	Ctlr *ctlr;
	Virtq *virtq;
	Block *block;
	virtq_used_elem *elem;
	u64int *desc_addr;
	u16int qsz;
	int block_size;
	
	//print("receive\n");

	ctlr = edev->ctlr;
	virtq = &ctlr->virtq[RxQueue];
	qsz = virtq->qsz;

	while(virtq->last_used_idx != virtq->used->idx){
		elem = &virtq->used_ring[virtq->last_used_idx % qsz];
		block_size = elem->len - sizeof(virtio_net_hdr);
		block = iallocb(block_size);
		if(block == nil){
			print("can't alloc block\n");
			return;
		}
		//print("rp=%p, wp=%p, base=%p\n", block->rp, block->wp, block->base);
		desc_addr = virtq->desc_virtual_addresses[elem->id];
		memmove(block->wp, ((u8int*)desc_addr)+sizeof(virtio_net_hdr), block_size);
	
		block->wp += block_size;
		
		
		Etherpkt *ep = (Etherpkt*)block->rp;
		//uchar *p = block->rp;
		//print("raw receive: ");
	for(int i = 0; i < 16; i++) {}
    	//print("%02x ", p[i]);
	//print("size: %d\n", sizeof(virtio_net_hdr));
	//print("\n");
/*
		print("\ndmac: ");
		for(int i = 0; i < 6; i++){
			print("%x:", ep->d[i]);
		}
		print("\n");
*/
		//print("receive rp=%p wp=%p blen=%d\n", block->rp, block->wp, BLEN(block));

/*
		print(" ; smac: ");
		for(int i = 0; i < 6; i++){
			print("%x:", ep->s[i]);
		}
		u16int type = (ep->type[0] << 8) | ep->type[1];
		print(" ; type: %x\n", type);
*/
		etheriq(edev, block);
		virtq->avail_ring[virtq->avail_idx++ % qsz] = elem->id;
		virtq->last_used_idx++;
		
	}
	coherence();
	virtq->avail->idx = virtq->avail_idx;
	virtq_notify(virtq->notif_addr, RxQueue);
	
	//print("used_idx: %d; avail_idx: %d\n",
	//	virtq->used->idx, virtq->avail->idx);
	
	//print("End recv\n");
}

void
snd_virtio_interrupt(Ureg*, void *arg)
{
	Ether *edev;
	Ctlr *ctlr;
	Virtq *virtqrx, *virtqtx;
	edev = arg;
	ctlr = edev->ctlr;
	virtqrx = &ctlr->virtq[RxQueue];
	virtqtx = &ctlr->virtq[TxQueue];
	
	if(*ctlr->isr_reg & 1){
		coherence();
		if(virtqrx->used->idx != virtqrx->last_used_idx)
			snd_virtio_receive(edev);
	    if(virtqtx->used->idx != virtqtx->last_used_idx)
			snd_virtio_free_tx(virtqtx);
		//else
		//	print("\nBad queue interrupt\n");
	}else
		print("\nNon receive interrupt\n");
}



int
reset(Ether *edev)
{
	Pcidev *p;
	u8int cap;
	u8int vendor, cfg_type;
	u32int len;

	Ctlr *ctlr;
	
	ctlr = nil;
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
			//print("\n\n\nnotif mult = %d\n", pcicfgr32(p, cap+16));
			
			if((len = get_cfg(&ctlr->notif_addr, p, cap)) == 0){
				print("\nnotif_addr is nil!\n");
				goto err;
			}
			ctlr->notify_off_multiplier = pcicfgr32(p, cap + cap_notify_off_multiplier);
			//print("\n\nnotif mult = %d\n", *((u8int*)ctlr->notif_cap + 16));
			ctlr->notif_len = len;
		}
		
		cap = pcicfgr8(p, cap + cap_next);
	}

	if (ctlr->common_cfg == nil || ctlr->net_cfg == nil || ctlr->notif_addr == nil || ctlr->isr_reg == nil){
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

	ctlr->common_cfg->device_status |= ACK | DRIVER;
	//ctlr->common_cfg->device_status |= DRIVER;
	
	if(feature_negotiation(ctlr->common_cfg) != 0){
		print("negotiation error\n");
		goto err;
	}

	if (ctlrhead == nil)
		ctlrhead = ctlr;
	else{
		Ctlr *c = ctlrhead;
		while(c->next)
			c = c->next;
		c->next = ctlr;		
	}
	ctlr->next = nil;
	edev->ctlr = ctlr;
	edev->arg = edev;
	edev->attach = snd_virtio_attach;
	edev->transmit = snd_virtio_transmit;
	edev->shutdown = snd_virtio_shutdown;
	edev->mbps = 1000;
	edev->irq = ctlr->p->intl;
	edev->tbdf = ctlr->p->tbdf;
	//edev->minmtu = 68;
	//edev->maxmtu = 1500;
	edev->link = 1;
	edev->port = p->mem[4].bar & ~0xF; /* hardcoded */
	edev->ifstat = snd_virtio_ifstat;
	edev->multicast = snd_virtio_multicast;
	edev->promiscuous = snd_virtio_promiscuous;
	
	for(int i = 0; i < Eaddrlen; i++){
		edev->ea[i] = ctlr->net_cfg->mac[i];
	}
	virtq_init(ctlr);
	pcisetbme(ctlr->p);
	intrenable(edev->irq, snd_virtio_interrupt, edev, edev->tbdf, edev->name);
	//print("tbdf=%#ux\n", ctlr->p->tbdf);
	//print("intl=%#ux\n", ctlr->p->intl);
	virtq_notify(ctlr->virtq[0].notif_addr, RxQueue);
	//virtq_notify(ctlr->virtq[1].notif_addr, TxQueue);
	print("\nWe are here\n");
	return 0;
err:
	if(ctlr != nil){
		if(ctlr->common_len)
			vunmap((void*)ctlr->common_cfg, ctlr->common_len);
		if(ctlr->net_len)
			vunmap((void*)ctlr->net_cfg, ctlr->net_len);
		if(ctlr->notif_len)
			vunmap((void*)ctlr->notif_addr, ctlr->notif_len);
		if(ctlr->isr_len)
			vunmap((void*)ctlr->isr_reg, ctlr->isr_len);
		free(ctlr);
	}
	if(p)
		pcidisable(p);
	print("\nmemory freed\n");
	return -1;
}

void
ethersndvirtiolink(void)
{
	addethercard("ethersndvirtio", reset);
}