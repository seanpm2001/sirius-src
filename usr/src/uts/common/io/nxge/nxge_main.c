/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * SunOs MT STREAMS NIU/Neptune 10Gb Ethernet Device Driver.
 */
#include	<sys/nxge/nxge_impl.h>
#include	<sys/nxge/nxge_hio.h>
#include	<sys/nxge/nxge_rxdma.h>
#include	<sys/pcie.h>

uint32_t 	nxge_use_partition = 0;		/* debug partition flag */
uint32_t 	nxge_dma_obp_props_only = 1;	/* use obp published props */
uint32_t 	nxge_use_rdc_intr = 1;		/* debug to assign rdc intr */
/*
 * PSARC/2007/453 MSI-X interrupt limit override
 * (This PSARC case is limited to MSI-X vectors
 *  and SPARC platforms only).
 */
#if defined(_BIG_ENDIAN)
uint32_t	nxge_msi_enable = 2;
#else
uint32_t	nxge_msi_enable = 1;
#endif

/*
 * Software workaround for a Neptune (PCI-E)
 * hardware interrupt bug which the hardware
 * may generate spurious interrupts after the
 * device interrupt handler was removed. If this flag
 * is enabled, the driver will reset the
 * hardware when devices are being detached.
 */
uint32_t	nxge_peu_reset_enable = 0;

/*
 * Software workaround for the hardware
 * checksum bugs that affect packet transmission
 * and receive:
 *
 * Usage of nxge_cksum_offload:
 *
 *  (1) nxge_cksum_offload = 0 (default):
 *	- transmits packets:
 *	  TCP: uses the hardware checksum feature.
 *	  UDP: driver will compute the software checksum
 *	       based on the partial checksum computed
 *	       by the IP layer.
 *	- receives packets
 *	  TCP: marks packets checksum flags based on hardware result.
 *	  UDP: will not mark checksum flags.
 *
 *  (2) nxge_cksum_offload = 1:
 *	- transmit packets:
 *	  TCP/UDP: uses the hardware checksum feature.
 *	- receives packets
 *	  TCP/UDP: marks packet checksum flags based on hardware result.
 *
 *  (3) nxge_cksum_offload = 2:
 *	- The driver will not register its checksum capability.
 *	  Checksum for both TCP and UDP will be computed
 *	  by the stack.
 *	- The software LSO is not allowed in this case.
 *
 *  (4) nxge_cksum_offload > 2:
 *	- Will be treated as it is set to 2
 *	  (stack will compute the checksum).
 *
 *  (5) If the hardware bug is fixed, this workaround
 *	needs to be updated accordingly to reflect
 *	the new hardware revision.
 */
uint32_t	nxge_cksum_offload = 0;

/*
 * Globals: tunable parameters (/etc/system or adb)
 *
 */
uint32_t 	nxge_rbr_size = NXGE_RBR_RBB_DEFAULT;
uint32_t 	nxge_rbr_spare_size = 0;
uint32_t 	nxge_rcr_size = NXGE_RCR_DEFAULT;
uint32_t 	nxge_tx_ring_size = NXGE_TX_RING_DEFAULT;
boolean_t 	nxge_no_msg = B_TRUE;		/* control message display */
uint32_t 	nxge_no_link_notify = 0;	/* control DL_NOTIFY */
uint32_t 	nxge_bcopy_thresh = TX_BCOPY_MAX;
uint32_t 	nxge_dvma_thresh = TX_FASTDVMA_MIN;
uint32_t 	nxge_dma_stream_thresh = TX_STREAM_MIN;
uint32_t	nxge_jumbo_mtu	= TX_JUMBO_MTU;
boolean_t	nxge_jumbo_enable = B_FALSE;
uint16_t	nxge_rcr_timeout = NXGE_RDC_RCR_TIMEOUT;
uint16_t	nxge_rcr_threshold = NXGE_RDC_RCR_THRESHOLD;
nxge_tx_mode_t	nxge_tx_scheme = NXGE_USE_SERIAL;

/* MAX LSO size */
#define		NXGE_LSO_MAXLEN	65535
uint32_t	nxge_lso_max = NXGE_LSO_MAXLEN;

/*
 * Debugging flags:
 *		nxge_no_tx_lb : transmit load balancing
 *		nxge_tx_lb_policy: 0 - TCP port (default)
 *				   3 - DEST MAC
 */
uint32_t 	nxge_no_tx_lb = 0;
uint32_t 	nxge_tx_lb_policy = NXGE_TX_LB_TCPUDP;

/*
 * Add tunable to reduce the amount of time spent in the
 * ISR doing Rx Processing.
 */
uint32_t nxge_max_rx_pkts = 1024;

/*
 * Tunables to manage the receive buffer blocks.
 *
 * nxge_rx_threshold_hi: copy all buffers.
 * nxge_rx_bcopy_size_type: receive buffer block size type.
 * nxge_rx_threshold_lo: copy only up to tunable block size type.
 */
nxge_rxbuf_threshold_t nxge_rx_threshold_hi = NXGE_RX_COPY_6;
nxge_rxbuf_type_t nxge_rx_buf_size_type = RCR_PKTBUFSZ_0;
nxge_rxbuf_threshold_t nxge_rx_threshold_lo = NXGE_RX_COPY_3;

/* Use kmem_alloc() to allocate data buffers. */
#if defined(_BIG_ENDIAN)
uint32_t	nxge_use_kmem_alloc = 1;
#else
uint32_t	nxge_use_kmem_alloc = 0;
#endif

rtrace_t npi_rtracebuf;

/*
 * The hardware sometimes fails to allow enough time for the link partner
 * to send an acknowledgement for packets that the hardware sent to it. The
 * hardware resends the packets earlier than it should be in those instances.
 * This behavior caused some switches to acknowledge the wrong packets
 * and it triggered the fatal error.
 * This software workaround is to set the replay timer to a value
 * suggested by the hardware team.
 *
 * PCI config space replay timer register:
 *     The following replay timeout value is 0xc
 *     for bit 14:18.
 */
#define	PCI_REPLAY_TIMEOUT_CFG_OFFSET	0xb8
#define	PCI_REPLAY_TIMEOUT_SHIFT	14

uint32_t	nxge_set_replay_timer = 1;
uint32_t	nxge_replay_timeout = 0xc;

/*
 * The transmit serialization sometimes causes
 * longer sleep before calling the driver transmit
 * function as it sleeps longer than it should.
 * The performace group suggests that a time wait tunable
 * can be used to set the maximum wait time when needed
 * and the default is set to 1 tick.
 */
uint32_t	nxge_tx_serial_maxsleep = 1;

#if	defined(sun4v)
/*
 * Hypervisor N2/NIU services information.
 */
static hsvc_info_t niu_hsvc = {
	HSVC_REV_1, NULL, HSVC_GROUP_NIU, NIU_MAJOR_VER,
	NIU_MINOR_VER, "nxge"
};

static int nxge_hsvc_register(p_nxge_t);
#endif

/*
 * Function Prototypes
 */
static int nxge_attach(dev_info_t *, ddi_attach_cmd_t);
static int nxge_detach(dev_info_t *, ddi_detach_cmd_t);
static void nxge_unattach(p_nxge_t);

#if NXGE_PROPERTY
static void nxge_remove_hard_properties(p_nxge_t);
#endif

/*
 * These two functions are required by nxge_hio.c
 */
extern int nxge_m_mmac_add(void *arg, mac_multi_addr_t *maddr);
extern int nxge_m_mmac_remove(void *arg, mac_addr_slot_t slot);

static nxge_status_t nxge_setup_system_dma_pages(p_nxge_t);

static nxge_status_t nxge_setup_mutexes(p_nxge_t);
static void nxge_destroy_mutexes(p_nxge_t);

static nxge_status_t nxge_map_regs(p_nxge_t nxgep);
static void nxge_unmap_regs(p_nxge_t nxgep);
#ifdef	NXGE_DEBUG
static void nxge_test_map_regs(p_nxge_t nxgep);
#endif

static nxge_status_t nxge_add_intrs(p_nxge_t nxgep);
static nxge_status_t nxge_add_soft_intrs(p_nxge_t nxgep);
static void nxge_remove_intrs(p_nxge_t nxgep);
static void nxge_remove_soft_intrs(p_nxge_t nxgep);

static nxge_status_t nxge_add_intrs_adv(p_nxge_t nxgep);
static nxge_status_t nxge_add_intrs_adv_type(p_nxge_t, uint32_t);
static nxge_status_t nxge_add_intrs_adv_type_fix(p_nxge_t, uint32_t);
static void nxge_intrs_enable(p_nxge_t nxgep);
static void nxge_intrs_disable(p_nxge_t nxgep);

static void nxge_suspend(p_nxge_t);
static nxge_status_t nxge_resume(p_nxge_t);

static nxge_status_t nxge_setup_dev(p_nxge_t);
static void nxge_destroy_dev(p_nxge_t);

static nxge_status_t nxge_alloc_mem_pool(p_nxge_t);
static void nxge_free_mem_pool(p_nxge_t);

nxge_status_t nxge_alloc_rx_mem_pool(p_nxge_t);
static void nxge_free_rx_mem_pool(p_nxge_t);

nxge_status_t nxge_alloc_tx_mem_pool(p_nxge_t);
static void nxge_free_tx_mem_pool(p_nxge_t);

static nxge_status_t nxge_dma_mem_alloc(p_nxge_t, dma_method_t,
	struct ddi_dma_attr *,
	size_t, ddi_device_acc_attr_t *, uint_t,
	p_nxge_dma_common_t);

static void nxge_dma_mem_free(p_nxge_dma_common_t);
static void nxge_dma_free_rx_data_buf(p_nxge_dma_common_t);

static nxge_status_t nxge_alloc_rx_buf_dma(p_nxge_t, uint16_t,
	p_nxge_dma_common_t *, size_t, size_t, uint32_t *);
static void nxge_free_rx_buf_dma(p_nxge_t, p_nxge_dma_common_t, uint32_t);

static nxge_status_t nxge_alloc_rx_cntl_dma(p_nxge_t, uint16_t,
	p_nxge_dma_common_t *, size_t);
static void nxge_free_rx_cntl_dma(p_nxge_t, p_nxge_dma_common_t);

extern nxge_status_t nxge_alloc_tx_buf_dma(p_nxge_t, uint16_t,
	p_nxge_dma_common_t *, size_t, size_t, uint32_t *);
static void nxge_free_tx_buf_dma(p_nxge_t, p_nxge_dma_common_t, uint32_t);

extern nxge_status_t nxge_alloc_tx_cntl_dma(p_nxge_t, uint16_t,
	p_nxge_dma_common_t *,
	size_t);
static void nxge_free_tx_cntl_dma(p_nxge_t, p_nxge_dma_common_t);

static int nxge_init_common_dev(p_nxge_t);
static void nxge_uninit_common_dev(p_nxge_t);
extern int nxge_param_set_mac(p_nxge_t, queue_t *, mblk_t *,
    char *, caddr_t);

/*
 * The next declarations are for the GLDv3 interface.
 */
static int nxge_m_start(void *);
static void nxge_m_stop(void *);
static int nxge_m_unicst(void *, const uint8_t *);
static int nxge_m_multicst(void *, boolean_t, const uint8_t *);
static int nxge_m_promisc(void *, boolean_t);
static void nxge_m_ioctl(void *, queue_t *, mblk_t *);
static void nxge_m_resources(void *);
mblk_t *nxge_m_tx(void *arg, mblk_t *);
static nxge_status_t nxge_mac_register(p_nxge_t);
static int nxge_altmac_set(p_nxge_t nxgep, uint8_t *mac_addr,
	mac_addr_slot_t slot);
void nxge_mmac_kstat_update(p_nxge_t nxgep, mac_addr_slot_t slot,
	boolean_t factory);
static int nxge_m_mmac_reserve(void *arg, mac_multi_addr_t *maddr);
static int nxge_m_mmac_modify(void *arg, mac_multi_addr_t *maddr);
static int nxge_m_mmac_get(void *arg, mac_multi_addr_t *maddr);
static	boolean_t nxge_m_getcapab(void *, mac_capab_t, void *);
static int nxge_m_setprop(void *, const char *, mac_prop_id_t,
    uint_t, const void *);
static int nxge_m_getprop(void *, const char *, mac_prop_id_t,
    uint_t, uint_t, void *);
static int nxge_set_priv_prop(nxge_t *, const char *, uint_t,
    const void *);
static int nxge_get_priv_prop(nxge_t *, const char *, uint_t, uint_t,
    void *);
static int nxge_get_def_val(nxge_t *, mac_prop_id_t, uint_t, void *);

static void nxge_niu_peu_reset(p_nxge_t nxgep);
static void nxge_set_pci_replay_timeout(nxge_t *);

mac_priv_prop_t nxge_priv_props[] = {
	{"_adv_10gfdx_cap", MAC_PROP_PERM_RW},
	{"_adv_pause_cap", MAC_PROP_PERM_RW},
	{"_function_number", MAC_PROP_PERM_READ},
	{"_fw_version", MAC_PROP_PERM_READ},
	{"_port_mode", MAC_PROP_PERM_READ},
	{"_hot_swap_phy", MAC_PROP_PERM_READ},
	{"_accept_jumbo", MAC_PROP_PERM_RW},
	{"_rxdma_intr_time", MAC_PROP_PERM_RW},
	{"_rxdma_intr_pkts", MAC_PROP_PERM_RW},
	{"_class_opt_ipv4_tcp", MAC_PROP_PERM_RW},
	{"_class_opt_ipv4_udp", MAC_PROP_PERM_RW},
	{"_class_opt_ipv4_ah", MAC_PROP_PERM_RW},
	{"_class_opt_ipv4_sctp", MAC_PROP_PERM_RW},
	{"_class_opt_ipv6_tcp", MAC_PROP_PERM_RW},
	{"_class_opt_ipv6_udp", MAC_PROP_PERM_RW},
	{"_class_opt_ipv6_ah", MAC_PROP_PERM_RW},
	{"_class_opt_ipv6_sctp", MAC_PROP_PERM_RW},
	{"_soft_lso_enable", MAC_PROP_PERM_RW}
};

#define	NXGE_MAX_PRIV_PROPS	\
	(sizeof (nxge_priv_props)/sizeof (mac_priv_prop_t))

#define	NXGE_M_CALLBACK_FLAGS\
	(MC_RESOURCES | MC_IOCTL | MC_GETCAPAB | MC_SETPROP | MC_GETPROP)


#define	NXGE_NEPTUNE_MAGIC	0x4E584745UL
#define	MAX_DUMP_SZ 256

#define	NXGE_M_CALLBACK_FLAGS	\
	(MC_RESOURCES | MC_IOCTL | MC_GETCAPAB | MC_SETPROP | MC_GETPROP)

mac_callbacks_t nxge_m_callbacks = {
	NXGE_M_CALLBACK_FLAGS,
	nxge_m_stat,
	nxge_m_start,
	nxge_m_stop,
	nxge_m_promisc,
	nxge_m_multicst,
	nxge_m_unicst,
	nxge_m_tx,
	nxge_m_resources,
	nxge_m_ioctl,
	nxge_m_getcapab,
	NULL,
	NULL,
	nxge_m_setprop,
	nxge_m_getprop
};

void
nxge_err_inject(p_nxge_t, queue_t *, mblk_t *);

/* PSARC/2007/453 MSI-X interrupt limit override. */
#define	NXGE_MSIX_REQUEST_10G	8
#define	NXGE_MSIX_REQUEST_1G	2
static int nxge_create_msi_property(p_nxge_t);

/*
 * These global variables control the message
 * output.
 */
out_dbgmsg_t nxge_dbgmsg_out = DBG_CONSOLE | STR_LOG;
uint64_t nxge_debug_level;

/*
 * This list contains the instance structures for the Neptune
 * devices present in the system. The lock exists to guarantee
 * mutually exclusive access to the list.
 */
void 			*nxge_list = NULL;

void			*nxge_hw_list = NULL;
nxge_os_mutex_t 	nxge_common_lock;

extern uint64_t 	npi_debug_level;

extern nxge_status_t	nxge_ldgv_init(p_nxge_t, int *, int *);
extern nxge_status_t	nxge_ldgv_init_n2(p_nxge_t, int *, int *);
extern nxge_status_t	nxge_ldgv_uninit(p_nxge_t);
extern nxge_status_t	nxge_intr_ldgv_init(p_nxge_t);
extern void		nxge_fm_init(p_nxge_t,
					ddi_device_acc_attr_t *,
					ddi_device_acc_attr_t *,
					ddi_dma_attr_t *);
extern void		nxge_fm_fini(p_nxge_t);
extern npi_status_t	npi_mac_altaddr_disable(npi_handle_t, uint8_t, uint8_t);

/*
 * Count used to maintain the number of buffers being used
 * by Neptune instances and loaned up to the upper layers.
 */
uint32_t nxge_mblks_pending = 0;

/*
 * Device register access attributes for PIO.
 */
static ddi_device_acc_attr_t nxge_dev_reg_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC,
};

/*
 * Device descriptor access attributes for DMA.
 */
static ddi_device_acc_attr_t nxge_dev_desc_dma_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_LE_ACC,
	DDI_STRICTORDER_ACC
};

/*
 * Device buffer access attributes for DMA.
 */
static ddi_device_acc_attr_t nxge_dev_buf_dma_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_STRUCTURE_BE_ACC,
	DDI_STRICTORDER_ACC
};

ddi_dma_attr_t nxge_desc_dma_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
#ifndef NIU_PA_WORKAROUND
	0x100000,		/* alignment */
#else
	0x2000,
#endif
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

ddi_dma_attr_t nxge_tx_dma_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
#if defined(_BIG_ENDIAN)
	0x2000,			/* alignment */
#else
	0x1000,			/* alignment */
#endif
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	5,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	0			/* attribute flags */
};

ddi_dma_attr_t nxge_rx_dma_attr = {
	DMA_ATTR_V0,		/* version number. */
	0,			/* low address */
	0xffffffffffffffff,	/* high address */
	0xffffffffffffffff,	/* address counter max */
	0x2000,			/* alignment */
	0xfc00fc,		/* dlim_burstsizes */
	0x1,			/* minimum transfer size */
	0xffffffffffffffff,	/* maximum transfer size */
	0xffffffffffffffff,	/* maximum segment size */
	1,			/* scatter/gather list length */
	(unsigned int) 1,	/* granularity */
	DDI_DMA_RELAXED_ORDERING /* attribute flags */
};

ddi_dma_lim_t nxge_dma_limits = {
	(uint_t)0,		/* dlim_addr_lo */
	(uint_t)0xffffffff,	/* dlim_addr_hi */
	(uint_t)0xffffffff,	/* dlim_cntr_max */
	(uint_t)0xfc00fc,	/* dlim_burstsizes for 32 and 64 bit xfers */
	0x1,			/* dlim_minxfer */
	1024			/* dlim_speed */
};

dma_method_t nxge_force_dma = DVMA;

/*
 * dma chunk sizes.
 *
 * Try to allocate the largest possible size
 * so that fewer number of dma chunks would be managed
 */
#ifdef NIU_PA_WORKAROUND
size_t alloc_sizes [] = {0x2000};
#else
size_t alloc_sizes [] = {0x1000, 0x2000, 0x4000, 0x8000,
		0x10000, 0x20000, 0x40000, 0x80000,
		0x100000, 0x200000, 0x400000, 0x800000,
		0x1000000, 0x2000000, 0x4000000};
#endif

/*
 * Translate "dev_t" to a pointer to the associated "dev_info_t".
 */

extern void nxge_get_environs(nxge_t *);

static int
nxge_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
	p_nxge_t	nxgep = NULL;
	int		instance;
	int		status = DDI_SUCCESS;
	uint8_t		portn;
	nxge_mmac_t	*mmac_info;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_attach"));

	/*
	 * Get the device instance since we'll need to setup
	 * or retrieve a soft state for this instance.
	 */
	instance = ddi_get_instance(dip);

	switch (cmd) {
	case DDI_ATTACH:
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "doing DDI_ATTACH"));
		break;

	case DDI_RESUME:
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "doing DDI_RESUME"));
		nxgep = (p_nxge_t)ddi_get_soft_state(nxge_list, instance);
		if (nxgep == NULL) {
			status = DDI_FAILURE;
			break;
		}
		if (nxgep->dip != dip) {
			status = DDI_FAILURE;
			break;
		}
		if (nxgep->suspended == DDI_PM_SUSPEND) {
			status = ddi_dev_is_needed(nxgep->dip, 0, 1);
		} else {
			status = nxge_resume(nxgep);
		}
		goto nxge_attach_exit;

	case DDI_PM_RESUME:
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "doing DDI_PM_RESUME"));
		nxgep = (p_nxge_t)ddi_get_soft_state(nxge_list, instance);
		if (nxgep == NULL) {
			status = DDI_FAILURE;
			break;
		}
		if (nxgep->dip != dip) {
			status = DDI_FAILURE;
			break;
		}
		status = nxge_resume(nxgep);
		goto nxge_attach_exit;

	default:
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "doing unknown"));
		status = DDI_FAILURE;
		goto nxge_attach_exit;
	}


	if (ddi_soft_state_zalloc(nxge_list, instance) == DDI_FAILURE) {
		status = DDI_FAILURE;
		goto nxge_attach_exit;
	}

	nxgep = ddi_get_soft_state(nxge_list, instance);
	if (nxgep == NULL) {
		status = NXGE_ERROR;
		goto nxge_attach_fail2;
	}

	nxgep->nxge_magic = NXGE_MAGIC;

	nxgep->drv_state = 0;
	nxgep->dip = dip;
	nxgep->instance = instance;
	nxgep->p_dip = ddi_get_parent(dip);
	nxgep->nxge_debug_level = nxge_debug_level;
	npi_debug_level = nxge_debug_level;

	/* Are we a guest running in a Hybrid I/O environment? */
	nxge_get_environs(nxgep);

	status = nxge_map_regs(nxgep);

	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "nxge_map_regs failed"));
		goto nxge_attach_fail3;
	}

	nxge_fm_init(nxgep, &nxge_dev_reg_acc_attr,
	    &nxge_dev_desc_dma_acc_attr,
	    &nxge_rx_dma_attr);

	/* Create & initialize the per-Neptune data structure */
	/* (even if we're a guest). */
	status = nxge_init_common_dev(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "nxge_init_common_dev failed"));
		goto nxge_attach_fail4;
	}

	/*
	 * Software workaround: set the replay timer.
	 */
	if (nxgep->niu_type != N2_NIU) {
		nxge_set_pci_replay_timeout(nxgep);
	}

#if defined(sun4v)
	/* This is required by nxge_hio_init(), which follows. */
	if ((status = nxge_hsvc_register(nxgep)) != DDI_SUCCESS)
		goto nxge_attach_fail;
#endif

	if ((status = nxge_hio_init(nxgep)) != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "nxge_hio_init failed"));
		goto nxge_attach_fail4;
	}

	if (nxgep->niu_type == NEPTUNE_2_10GF) {
		if (nxgep->function_num > 1) {
			NXGE_DEBUG_MSG((nxgep, DDI_CTL, "Unsupported"
			    " function %d. Only functions 0 and 1 are "
			    "supported for this card.", nxgep->function_num));
			status = NXGE_ERROR;
			goto nxge_attach_fail4;
		}
	}

	if (isLDOMguest(nxgep)) {
		/*
		 * Use the function number here.
		 */
		nxgep->mac.portnum = nxgep->function_num;
		nxgep->mac.porttype = PORT_TYPE_LOGICAL;

		/* XXX We'll set the MAC address counts to 1 for now. */
		mmac_info = &nxgep->nxge_mmac_info;
		mmac_info->num_mmac = 1;
		mmac_info->naddrfree = 1;
	} else {
		portn = NXGE_GET_PORT_NUM(nxgep->function_num);
		nxgep->mac.portnum = portn;
		if ((portn == 0) || (portn == 1))
			nxgep->mac.porttype = PORT_TYPE_XMAC;
		else
			nxgep->mac.porttype = PORT_TYPE_BMAC;
		/*
		 * Neptune has 4 ports, the first 2 ports use XMAC (10G MAC)
		 * internally, the rest 2 ports use BMAC (1G "Big" MAC).
		 * The two types of MACs have different characterizations.
		 */
		mmac_info = &nxgep->nxge_mmac_info;
		if (nxgep->function_num < 2) {
			mmac_info->num_mmac = XMAC_MAX_ALT_ADDR_ENTRY;
			mmac_info->naddrfree = XMAC_MAX_ALT_ADDR_ENTRY;
		} else {
			mmac_info->num_mmac = BMAC_MAX_ALT_ADDR_ENTRY;
			mmac_info->naddrfree = BMAC_MAX_ALT_ADDR_ENTRY;
		}
	}
	/*
	 * Setup the Ndd parameters for the this instance.
	 */
	nxge_init_param(nxgep);

	/*
	 * Setup Register Tracing Buffer.
	 */
	npi_rtrace_buf_init((rtrace_t *)&npi_rtracebuf);

	/* init stats ptr */
	nxge_init_statsp(nxgep);

	/*
	 * Copy the vpd info from eeprom to a local data
	 * structure, and then check its validity.
	 */
	if (!isLDOMguest(nxgep)) {
		int *regp;
		uint_t reglen;
		int rv;

		nxge_vpd_info_get(nxgep);

		/* Find the NIU config handle. */
		rv = ddi_prop_lookup_int_array(DDI_DEV_T_ANY,
		    ddi_get_parent(nxgep->dip), DDI_PROP_DONTPASS,
		    "reg", &regp, &reglen);

		if (rv != DDI_PROP_SUCCESS) {
			goto nxge_attach_fail5;
		}
		/*
		 * The address_hi, that is the first int, in the reg
		 * property consists of config handle, but need to remove
		 * the bits 28-31 which are OBP specific info.
		 */
		nxgep->niu_cfg_hdl = (*regp) & 0xFFFFFFF;
		ddi_prop_free(regp);
	}

	if (isLDOMguest(nxgep)) {
		uchar_t *prop_val;
		uint_t prop_len;

		extern void nxge_get_logical_props(p_nxge_t);

		nxgep->statsp->mac_stats.xcvr_inuse = LOGICAL_XCVR;
		nxgep->mac.portmode = PORT_LOGICAL;
		(void) ddi_prop_update_string(DDI_DEV_T_NONE, nxgep->dip,
		    "phy-type", "virtual transceiver");

		nxgep->nports = 1;
		nxgep->board_ver = 0;	/* XXX What? */

		/*
		 * local-mac-address property gives us info on which
		 * specific MAC address the Hybrid resource is associated
		 * with.
		 */
		if (ddi_prop_lookup_byte_array(DDI_DEV_T_ANY, nxgep->dip, 0,
		    "local-mac-address", &prop_val,
		    &prop_len) != DDI_PROP_SUCCESS) {
			goto nxge_attach_fail5;
		}
		if (prop_len !=  ETHERADDRL) {
			ddi_prop_free(prop_val);
			goto nxge_attach_fail5;
		}
		ether_copy(prop_val, nxgep->hio_mac_addr);
		ddi_prop_free(prop_val);
		nxge_get_logical_props(nxgep);

	} else {
		status = nxge_xcvr_find(nxgep);

		if (status != NXGE_OK) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "nxge_attach: "
			    " Couldn't determine card type"
			    " .... exit "));
			goto nxge_attach_fail5;
		}

		status = nxge_get_config_properties(nxgep);

		if (status != NXGE_OK) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "get_hw create failed"));
			goto nxge_attach_fail;
		}
	}

	/*
	 * Setup the Kstats for the driver.
	 */
	nxge_setup_kstats(nxgep);

	if (!isLDOMguest(nxgep))
		nxge_setup_param(nxgep);

	status = nxge_setup_system_dma_pages(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "set dma page failed"));
		goto nxge_attach_fail;
	}

	nxge_hw_id_init(nxgep);

	if (!isLDOMguest(nxgep))
		nxge_hw_init_niu_common(nxgep);

	status = nxge_setup_mutexes(nxgep);
	if (status != NXGE_OK) {
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "set mutex failed"));
		goto nxge_attach_fail;
	}

#if defined(sun4v)
	if (isLDOMguest(nxgep)) {
		/* Find our VR & channel sets. */
		status = nxge_hio_vr_add(nxgep);
		goto nxge_attach_exit;
	}
#endif

	status = nxge_setup_dev(nxgep);
	if (status != DDI_SUCCESS) {
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "set dev failed"));
		goto nxge_attach_fail;
	}

	status = nxge_add_intrs(nxgep);
	if (status != DDI_SUCCESS) {
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "add_intr failed"));
		goto nxge_attach_fail;
	}
	status = nxge_add_soft_intrs(nxgep);
	if (status != DDI_SUCCESS) {
		NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL,
		    "add_soft_intr failed"));
		goto nxge_attach_fail;
	}

	/*
	 * Enable interrupts.
	 */
	nxge_intrs_enable(nxgep);

	/* If a guest, register with vio_net instead. */
	if ((status = nxge_mac_register(nxgep)) != NXGE_OK) {
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "unable to register to mac layer (%d)", status));
		goto nxge_attach_fail;
	}

	mac_link_update(nxgep->mach, LINK_STATE_UNKNOWN);

	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "registered to mac (instance %d)", instance));

	/* nxge_link_monitor calls xcvr.check_link recursively */
	(void) nxge_link_monitor(nxgep, LINK_MONITOR_START);

	goto nxge_attach_exit;

nxge_attach_fail:
	nxge_unattach(nxgep);
	goto nxge_attach_fail1;

nxge_attach_fail5:
	/*
	 * Tear down the ndd parameters setup.
	 */
	nxge_destroy_param(nxgep);

	/*
	 * Tear down the kstat setup.
	 */
	nxge_destroy_kstats(nxgep);

nxge_attach_fail4:
	if (nxgep->nxge_hw_p) {
		nxge_uninit_common_dev(nxgep);
		nxgep->nxge_hw_p = NULL;
	}

nxge_attach_fail3:
	/*
	 * Unmap the register setup.
	 */
	nxge_unmap_regs(nxgep);

	nxge_fm_fini(nxgep);

nxge_attach_fail2:
	ddi_soft_state_free(nxge_list, nxgep->instance);

nxge_attach_fail1:
	if (status != NXGE_OK)
		status = (NXGE_ERROR | NXGE_DDI_FAILED);
	nxgep = NULL;

nxge_attach_exit:
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_attach status = 0x%08x",
	    status));

	return (status);
}

static int
nxge_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
	int 		status = DDI_SUCCESS;
	int 		instance;
	p_nxge_t 	nxgep = NULL;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_detach"));
	instance = ddi_get_instance(dip);
	nxgep = ddi_get_soft_state(nxge_list, instance);
	if (nxgep == NULL) {
		status = DDI_FAILURE;
		goto nxge_detach_exit;
	}

	switch (cmd) {
	case DDI_DETACH:
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "doing DDI_DETACH"));
		break;

	case DDI_PM_SUSPEND:
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "doing DDI_PM_SUSPEND"));
		nxgep->suspended = DDI_PM_SUSPEND;
		nxge_suspend(nxgep);
		break;

	case DDI_SUSPEND:
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "doing DDI_SUSPEND"));
		if (nxgep->suspended != DDI_PM_SUSPEND) {
			nxgep->suspended = DDI_SUSPEND;
			nxge_suspend(nxgep);
		}
		break;

	default:
		status = DDI_FAILURE;
	}

	if (cmd != DDI_DETACH)
		goto nxge_detach_exit;

	/*
	 * Stop the xcvr polling.
	 */
	nxgep->suspended = cmd;

	(void) nxge_link_monitor(nxgep, LINK_MONITOR_STOP);

	if (isLDOMguest(nxgep)) {
		nxge_hio_unregister(nxgep);
	} else if (nxgep->mach && (status = mac_unregister(nxgep->mach)) != 0) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "<== nxge_detach status = 0x%08X", status));
		return (DDI_FAILURE);
	}

	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "<== nxge_detach (mac_unregister) status = 0x%08X", status));

	nxge_unattach(nxgep);
	nxgep = NULL;

nxge_detach_exit:
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_detach status = 0x%08X",
	    status));

	return (status);
}

static void
nxge_unattach(p_nxge_t nxgep)
{
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_unattach"));

	if (nxgep == NULL || nxgep->dev_regs == NULL) {
		return;
	}

	nxgep->nxge_magic = 0;

	if (nxgep->nxge_timerid) {
		nxge_stop_timer(nxgep, nxgep->nxge_timerid);
		nxgep->nxge_timerid = 0;
	}

	/*
	 * If this flag is set, it will affect the Neptune
	 * only.
	 */
	if ((nxgep->niu_type != N2_NIU) && nxge_peu_reset_enable) {
		nxge_niu_peu_reset(nxgep);
	}

#if	defined(sun4v)
	if (isLDOMguest(nxgep)) {
		(void) nxge_hio_vr_release(nxgep);
	}
#endif

	if (nxgep->nxge_hw_p) {
		nxge_uninit_common_dev(nxgep);
		nxgep->nxge_hw_p = NULL;
	}

#if	defined(sun4v)
	if (nxgep->niu_type == N2_NIU && nxgep->niu_hsvc_available == B_TRUE) {
		(void) hsvc_unregister(&nxgep->niu_hsvc);
		nxgep->niu_hsvc_available = B_FALSE;
	}
#endif
	/*
	 * Stop any further interrupts.
	 */
	nxge_remove_intrs(nxgep);

	/* remove soft interrups */
	nxge_remove_soft_intrs(nxgep);

	/*
	 * Stop the device and free resources.
	 */
	if (!isLDOMguest(nxgep)) {
		nxge_destroy_dev(nxgep);
	}

	/*
	 * Tear down the ndd parameters setup.
	 */
	nxge_destroy_param(nxgep);

	/*
	 * Tear down the kstat setup.
	 */
	nxge_destroy_kstats(nxgep);

	/*
	 * Destroy all mutexes.
	 */
	nxge_destroy_mutexes(nxgep);

	/*
	 * Remove the list of ndd parameters which
	 * were setup during attach.
	 */
	if (nxgep->dip) {
		NXGE_DEBUG_MSG((nxgep, OBP_CTL,
		    " nxge_unattach: remove all properties"));

		(void) ddi_prop_remove_all(nxgep->dip);
	}

#if NXGE_PROPERTY
	nxge_remove_hard_properties(nxgep);
#endif

	/*
	 * Unmap the register setup.
	 */
	nxge_unmap_regs(nxgep);

	nxge_fm_fini(nxgep);

	ddi_soft_state_free(nxge_list, nxgep->instance);

	NXGE_DEBUG_MSG((NULL, DDI_CTL, "<== nxge_unattach"));
}

#if defined(sun4v)
int
nxge_hsvc_register(
	nxge_t *nxgep)
{
	nxge_status_t status;

	if (nxgep->niu_type == N2_NIU) {
		nxgep->niu_hsvc_available = B_FALSE;
		bcopy(&niu_hsvc, &nxgep->niu_hsvc, sizeof (hsvc_info_t));
		if ((status = hsvc_register(&nxgep->niu_hsvc,
		    &nxgep->niu_min_ver)) != 0) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "nxge_attach: %s: cannot negotiate "
			    "hypervisor services revision %d group: 0x%lx "
			    "major: 0x%lx minor: 0x%lx errno: %d",
			    niu_hsvc.hsvc_modname, niu_hsvc.hsvc_rev,
			    niu_hsvc.hsvc_group, niu_hsvc.hsvc_major,
			    niu_hsvc.hsvc_minor, status));
			return (DDI_FAILURE);
		}
		nxgep->niu_hsvc_available = B_TRUE;
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "NIU Hypervisor service enabled"));
	}

	return (DDI_SUCCESS);
}
#endif

static char n2_siu_name[] = "niu";

static nxge_status_t
nxge_map_regs(p_nxge_t nxgep)
{
	int		ddi_status = DDI_SUCCESS;
	p_dev_regs_t 	dev_regs;
	char		buf[MAXPATHLEN + 1];
	char 		*devname;
#ifdef	NXGE_DEBUG
	char 		*sysname;
#endif
	off_t		regsize;
	nxge_status_t	status = NXGE_OK;
#if !defined(_BIG_ENDIAN)
	off_t pci_offset;
	uint16_t pcie_devctl;
#endif

	if (isLDOMguest(nxgep)) {
		return (nxge_guest_regs_map(nxgep));
	}

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_map_regs"));
	nxgep->dev_regs = NULL;
	dev_regs = KMEM_ZALLOC(sizeof (dev_regs_t), KM_SLEEP);
	dev_regs->nxge_regh = NULL;
	dev_regs->nxge_pciregh = NULL;
	dev_regs->nxge_msix_regh = NULL;
	dev_regs->nxge_vir_regh = NULL;
	dev_regs->nxge_vir2_regh = NULL;
	nxgep->niu_type = NIU_TYPE_NONE;

	devname = ddi_pathname(nxgep->dip, buf);
	ASSERT(strlen(devname) > 0);
	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "nxge_map_regs: pathname devname %s", devname));

	/*
	 * The driver is running on a N2-NIU system if devname is something
	 * like "/niu@80/network@0"
	 */
	if (strstr(devname, n2_siu_name)) {
		/* N2/NIU */
		nxgep->niu_type = N2_NIU;
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_regs: N2/NIU devname %s", devname));
		/* get function number */
		nxgep->function_num =
		    (devname[strlen(devname) -1] == '1' ? 1 : 0);
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_regs: N2/NIU function number %d",
		    nxgep->function_num));
	} else {
		int		*prop_val;
		uint_t 		prop_len;
		uint8_t 	func_num;

		if (ddi_prop_lookup_int_array(DDI_DEV_T_ANY, nxgep->dip,
		    0, "reg",
		    &prop_val, &prop_len) != DDI_PROP_SUCCESS) {
			NXGE_DEBUG_MSG((nxgep, VPD_CTL,
			    "Reg property not found"));
			ddi_status = DDI_FAILURE;
			goto nxge_map_regs_fail0;

		} else {
			func_num = (prop_val[0] >> 8) & 0x7;
			NXGE_DEBUG_MSG((nxgep, DDI_CTL,
			    "Reg property found: fun # %d",
			    func_num));
			nxgep->function_num = func_num;
			if (isLDOMguest(nxgep)) {
				nxgep->function_num /= 2;
				return (NXGE_OK);
			}
			ddi_prop_free(prop_val);
		}
	}

	switch (nxgep->niu_type) {
	default:
		(void) ddi_dev_regsize(nxgep->dip, 0, &regsize);
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_regs: pci config size 0x%x", regsize));

		ddi_status = ddi_regs_map_setup(nxgep->dip, 0,
		    (caddr_t *)&(dev_regs->nxge_pciregp), 0, 0,
		    &nxge_dev_reg_acc_attr, &dev_regs->nxge_pciregh);
		if (ddi_status != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "ddi_map_regs, nxge bus config regs failed"));
			goto nxge_map_regs_fail0;
		}
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_reg: PCI config addr 0x%0llx "
		    " handle 0x%0llx", dev_regs->nxge_pciregp,
		    dev_regs->nxge_pciregh));
			/*
			 * IMP IMP
			 * workaround  for bit swapping bug in HW
			 * which ends up in no-snoop = yes
			 * resulting, in DMA not synched properly
			 */
#if !defined(_BIG_ENDIAN)
		/* workarounds for x86 systems */
		pci_offset = 0x80 + PCIE_DEVCTL;
		pcie_devctl = 0x0;
		pcie_devctl &= PCIE_DEVCTL_ENABLE_NO_SNOOP;
		pcie_devctl |= PCIE_DEVCTL_RO_EN;
		pci_config_put16(dev_regs->nxge_pciregh, pci_offset,
		    pcie_devctl);
#endif

		(void) ddi_dev_regsize(nxgep->dip, 1, &regsize);
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_regs: pio size 0x%x", regsize));
		/* set up the device mapped register */
		ddi_status = ddi_regs_map_setup(nxgep->dip, 1,
		    (caddr_t *)&(dev_regs->nxge_regp), 0, 0,
		    &nxge_dev_reg_acc_attr, &dev_regs->nxge_regh);
		if (ddi_status != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "ddi_map_regs for Neptune global reg failed"));
			goto nxge_map_regs_fail1;
		}

		/* set up the msi/msi-x mapped register */
		(void) ddi_dev_regsize(nxgep->dip, 2, &regsize);
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_regs: msix size 0x%x", regsize));
		ddi_status = ddi_regs_map_setup(nxgep->dip, 2,
		    (caddr_t *)&(dev_regs->nxge_msix_regp), 0, 0,
		    &nxge_dev_reg_acc_attr, &dev_regs->nxge_msix_regh);
		if (ddi_status != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "ddi_map_regs for msi reg failed"));
			goto nxge_map_regs_fail2;
		}

		/* set up the vio region mapped register */
		(void) ddi_dev_regsize(nxgep->dip, 3, &regsize);
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_regs: vio size 0x%x", regsize));
		ddi_status = ddi_regs_map_setup(nxgep->dip, 3,
		    (caddr_t *)&(dev_regs->nxge_vir_regp), 0, 0,
		    &nxge_dev_reg_acc_attr, &dev_regs->nxge_vir_regh);

		if (ddi_status != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "ddi_map_regs for nxge vio reg failed"));
			goto nxge_map_regs_fail3;
		}
		nxgep->dev_regs = dev_regs;

		NPI_PCI_ACC_HANDLE_SET(nxgep, dev_regs->nxge_pciregh);
		NPI_PCI_ADD_HANDLE_SET(nxgep,
		    (npi_reg_ptr_t)dev_regs->nxge_pciregp);
		NPI_MSI_ACC_HANDLE_SET(nxgep, dev_regs->nxge_msix_regh);
		NPI_MSI_ADD_HANDLE_SET(nxgep,
		    (npi_reg_ptr_t)dev_regs->nxge_msix_regp);

		NPI_ACC_HANDLE_SET(nxgep, dev_regs->nxge_regh);
		NPI_ADD_HANDLE_SET(nxgep, (npi_reg_ptr_t)dev_regs->nxge_regp);

		NPI_REG_ACC_HANDLE_SET(nxgep, dev_regs->nxge_regh);
		NPI_REG_ADD_HANDLE_SET(nxgep,
		    (npi_reg_ptr_t)dev_regs->nxge_regp);

		NPI_VREG_ACC_HANDLE_SET(nxgep, dev_regs->nxge_vir_regh);
		NPI_VREG_ADD_HANDLE_SET(nxgep,
		    (npi_reg_ptr_t)dev_regs->nxge_vir_regp);

		break;

	case N2_NIU:
		NXGE_DEBUG_MSG((nxgep, DDI_CTL, "ddi_map_regs, NIU"));
		/*
		 * Set up the device mapped register (FWARC 2006/556)
		 * (changed back to 1: reg starts at 1!)
		 */
		(void) ddi_dev_regsize(nxgep->dip, 1, &regsize);
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_regs: dev size 0x%x", regsize));
		ddi_status = ddi_regs_map_setup(nxgep->dip, 1,
		    (caddr_t *)&(dev_regs->nxge_regp), 0, 0,
		    &nxge_dev_reg_acc_attr, &dev_regs->nxge_regh);

		if (ddi_status != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "ddi_map_regs for N2/NIU, global reg failed "));
			goto nxge_map_regs_fail1;
		}

		/* set up the first vio region mapped register */
		(void) ddi_dev_regsize(nxgep->dip, 2, &regsize);
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_regs: vio (1) size 0x%x", regsize));
		ddi_status = ddi_regs_map_setup(nxgep->dip, 2,
		    (caddr_t *)&(dev_regs->nxge_vir_regp), 0, 0,
		    &nxge_dev_reg_acc_attr, &dev_regs->nxge_vir_regh);

		if (ddi_status != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "ddi_map_regs for nxge vio reg failed"));
			goto nxge_map_regs_fail2;
		}
		/* set up the second vio region mapped register */
		(void) ddi_dev_regsize(nxgep->dip, 3, &regsize);
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "nxge_map_regs: vio (3) size 0x%x", regsize));
		ddi_status = ddi_regs_map_setup(nxgep->dip, 3,
		    (caddr_t *)&(dev_regs->nxge_vir2_regp), 0, 0,
		    &nxge_dev_reg_acc_attr, &dev_regs->nxge_vir2_regh);

		if (ddi_status != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "ddi_map_regs for nxge vio2 reg failed"));
			goto nxge_map_regs_fail3;
		}
		nxgep->dev_regs = dev_regs;

		NPI_ACC_HANDLE_SET(nxgep, dev_regs->nxge_regh);
		NPI_ADD_HANDLE_SET(nxgep, (npi_reg_ptr_t)dev_regs->nxge_regp);

		NPI_REG_ACC_HANDLE_SET(nxgep, dev_regs->nxge_regh);
		NPI_REG_ADD_HANDLE_SET(nxgep,
		    (npi_reg_ptr_t)dev_regs->nxge_regp);

		NPI_VREG_ACC_HANDLE_SET(nxgep, dev_regs->nxge_vir_regh);
		NPI_VREG_ADD_HANDLE_SET(nxgep,
		    (npi_reg_ptr_t)dev_regs->nxge_vir_regp);

		NPI_V2REG_ACC_HANDLE_SET(nxgep, dev_regs->nxge_vir2_regh);
		NPI_V2REG_ADD_HANDLE_SET(nxgep,
		    (npi_reg_ptr_t)dev_regs->nxge_vir2_regp);

		break;
	}

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "nxge_map_reg: hardware addr 0x%0llx "
	    " handle 0x%0llx", dev_regs->nxge_regp, dev_regs->nxge_regh));

	goto nxge_map_regs_exit;
nxge_map_regs_fail3:
	if (dev_regs->nxge_msix_regh) {
		ddi_regs_map_free(&dev_regs->nxge_msix_regh);
	}
	if (dev_regs->nxge_vir_regh) {
		ddi_regs_map_free(&dev_regs->nxge_regh);
	}
nxge_map_regs_fail2:
	if (dev_regs->nxge_regh) {
		ddi_regs_map_free(&dev_regs->nxge_regh);
	}
nxge_map_regs_fail1:
	if (dev_regs->nxge_pciregh) {
		ddi_regs_map_free(&dev_regs->nxge_pciregh);
	}
nxge_map_regs_fail0:
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "Freeing register set memory"));
	kmem_free(dev_regs, sizeof (dev_regs_t));

nxge_map_regs_exit:
	if (ddi_status != DDI_SUCCESS)
		status |= (NXGE_ERROR | NXGE_DDI_FAILED);
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_map_regs"));
	return (status);
}

static void
nxge_unmap_regs(p_nxge_t nxgep)
{
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_unmap_regs"));

	if (isLDOMguest(nxgep)) {
		nxge_guest_regs_map_free(nxgep);
		return;
	}

	if (nxgep->dev_regs) {
		if (nxgep->dev_regs->nxge_pciregh) {
			NXGE_DEBUG_MSG((nxgep, DDI_CTL,
			    "==> nxge_unmap_regs: bus"));
			ddi_regs_map_free(&nxgep->dev_regs->nxge_pciregh);
			nxgep->dev_regs->nxge_pciregh = NULL;
		}
		if (nxgep->dev_regs->nxge_regh) {
			NXGE_DEBUG_MSG((nxgep, DDI_CTL,
			    "==> nxge_unmap_regs: device registers"));
			ddi_regs_map_free(&nxgep->dev_regs->nxge_regh);
			nxgep->dev_regs->nxge_regh = NULL;
		}
		if (nxgep->dev_regs->nxge_msix_regh) {
			NXGE_DEBUG_MSG((nxgep, DDI_CTL,
			    "==> nxge_unmap_regs: device interrupts"));
			ddi_regs_map_free(&nxgep->dev_regs->nxge_msix_regh);
			nxgep->dev_regs->nxge_msix_regh = NULL;
		}
		if (nxgep->dev_regs->nxge_vir_regh) {
			NXGE_DEBUG_MSG((nxgep, DDI_CTL,
			    "==> nxge_unmap_regs: vio region"));
			ddi_regs_map_free(&nxgep->dev_regs->nxge_vir_regh);
			nxgep->dev_regs->nxge_vir_regh = NULL;
		}
		if (nxgep->dev_regs->nxge_vir2_regh) {
			NXGE_DEBUG_MSG((nxgep, DDI_CTL,
			    "==> nxge_unmap_regs: vio2 region"));
			ddi_regs_map_free(&nxgep->dev_regs->nxge_vir2_regh);
			nxgep->dev_regs->nxge_vir2_regh = NULL;
		}

		kmem_free(nxgep->dev_regs, sizeof (dev_regs_t));
		nxgep->dev_regs = NULL;
	}

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_unmap_regs"));
}

static nxge_status_t
nxge_setup_mutexes(p_nxge_t nxgep)
{
	int ddi_status = DDI_SUCCESS;
	nxge_status_t status = NXGE_OK;
	nxge_classify_t *classify_ptr;
	int partition;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_setup_mutexes"));

	/*
	 * Get the interrupt cookie so the mutexes can be
	 * Initialized.
	 */
	if (isLDOMguest(nxgep)) {
		nxgep->interrupt_cookie = 0;
	} else {
		ddi_status = ddi_get_iblock_cookie(nxgep->dip, 0,
		    &nxgep->interrupt_cookie);

		if (ddi_status != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "<== nxge_setup_mutexes: failed 0x%x",
			    ddi_status));
			goto nxge_setup_mutexes_exit;
		}
	}

	cv_init(&nxgep->poll_cv, NULL, CV_DRIVER, NULL);
	MUTEX_INIT(&nxgep->poll_lock, NULL,
	    MUTEX_DRIVER, (void *)nxgep->interrupt_cookie);

	/*
	 * Initialize mutexes for this device.
	 */
	MUTEX_INIT(nxgep->genlock, NULL,
	    MUTEX_DRIVER, (void *)nxgep->interrupt_cookie);
	MUTEX_INIT(&nxgep->ouraddr_lock, NULL,
	    MUTEX_DRIVER, (void *)nxgep->interrupt_cookie);
	MUTEX_INIT(&nxgep->mif_lock, NULL,
	    MUTEX_DRIVER, (void *)nxgep->interrupt_cookie);
	MUTEX_INIT(&nxgep->group_lock, NULL,
	    MUTEX_DRIVER, (void *)nxgep->interrupt_cookie);
	RW_INIT(&nxgep->filter_lock, NULL,
	    RW_DRIVER, (void *)nxgep->interrupt_cookie);

	classify_ptr = &nxgep->classifier;
		/*
		 * FFLP Mutexes are never used in interrupt context
		 * as fflp operation can take very long time to
		 * complete and hence not suitable to invoke from interrupt
		 * handlers.
		 */
	MUTEX_INIT(&classify_ptr->tcam_lock, NULL,
	    NXGE_MUTEX_DRIVER, (void *)nxgep->interrupt_cookie);
	if (NXGE_IS_VALID_NEPTUNE_TYPE(nxgep)) {
		MUTEX_INIT(&classify_ptr->fcram_lock, NULL,
		    NXGE_MUTEX_DRIVER, (void *)nxgep->interrupt_cookie);
		for (partition = 0; partition < MAX_PARTITION; partition++) {
			MUTEX_INIT(&classify_ptr->hash_lock[partition], NULL,
			    NXGE_MUTEX_DRIVER, (void *)nxgep->interrupt_cookie);
		}
	}

nxge_setup_mutexes_exit:
	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "<== nxge_setup_mutexes status = %x", status));

	if (ddi_status != DDI_SUCCESS)
		status |= (NXGE_ERROR | NXGE_DDI_FAILED);

	return (status);
}

static void
nxge_destroy_mutexes(p_nxge_t nxgep)
{
	int partition;
	nxge_classify_t *classify_ptr;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_destroy_mutexes"));
	RW_DESTROY(&nxgep->filter_lock);
	MUTEX_DESTROY(&nxgep->group_lock);
	MUTEX_DESTROY(&nxgep->mif_lock);
	MUTEX_DESTROY(&nxgep->ouraddr_lock);
	MUTEX_DESTROY(nxgep->genlock);

	classify_ptr = &nxgep->classifier;
	MUTEX_DESTROY(&classify_ptr->tcam_lock);

	/* Destroy all polling resources. */
	MUTEX_DESTROY(&nxgep->poll_lock);
	cv_destroy(&nxgep->poll_cv);

	/* free data structures, based on HW type */
	if (NXGE_IS_VALID_NEPTUNE_TYPE(nxgep)) {
		MUTEX_DESTROY(&classify_ptr->fcram_lock);
		for (partition = 0; partition < MAX_PARTITION; partition++) {
			MUTEX_DESTROY(&classify_ptr->hash_lock[partition]);
		}
	}

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_destroy_mutexes"));
}

nxge_status_t
nxge_init(p_nxge_t nxgep)
{
	nxge_status_t status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, STR_CTL, "==> nxge_init"));

	if (nxgep->drv_state & STATE_HW_INITIALIZED) {
		return (status);
	}

	/*
	 * Allocate system memory for the receive/transmit buffer blocks
	 * and receive/transmit descriptor rings.
	 */
	status = nxge_alloc_mem_pool(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "alloc mem failed\n"));
		goto nxge_init_fail1;
	}

	if (!isLDOMguest(nxgep)) {
		/*
		 * Initialize and enable the TXC registers.
		 * (Globally enable the Tx controller,
		 *  enable the port, configure the dma channel bitmap,
		 *  configure the max burst size).
		 */
		status = nxge_txc_init(nxgep);
		if (status != NXGE_OK) {
			NXGE_ERROR_MSG((nxgep,
			    NXGE_ERR_CTL, "init txc failed\n"));
			goto nxge_init_fail2;
		}
	}

	/*
	 * Initialize and enable TXDMA channels.
	 */
	status = nxge_init_txdma_channels(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "init txdma failed\n"));
		goto nxge_init_fail3;
	}

	/*
	 * Initialize and enable RXDMA channels.
	 */
	status = nxge_init_rxdma_channels(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "init rxdma failed\n"));
		goto nxge_init_fail4;
	}

	/*
	 * The guest domain is now done.
	 */
	if (isLDOMguest(nxgep)) {
		nxgep->drv_state |= STATE_HW_INITIALIZED;
		goto nxge_init_exit;
	}

	/*
	 * Initialize TCAM and FCRAM (Neptune).
	 */
	status = nxge_classify_init(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "init classify failed\n"));
		goto nxge_init_fail5;
	}

	/*
	 * Initialize ZCP
	 */
	status = nxge_zcp_init(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "init ZCP failed\n"));
		goto nxge_init_fail5;
	}

	/*
	 * Initialize IPP.
	 */
	status = nxge_ipp_init(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "init IPP failed\n"));
		goto nxge_init_fail5;
	}

	/*
	 * Initialize the MAC block.
	 */
	status = nxge_mac_init(nxgep);
	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "init MAC failed\n"));
		goto nxge_init_fail5;
	}

	nxge_intrs_enable(nxgep); /* XXX What changes do I need to make here? */

	/*
	 * Enable hardware interrupts.
	 */
	nxge_intr_hw_enable(nxgep);
	nxgep->drv_state |= STATE_HW_INITIALIZED;

	goto nxge_init_exit;

nxge_init_fail5:
	nxge_uninit_rxdma_channels(nxgep);
nxge_init_fail4:
	nxge_uninit_txdma_channels(nxgep);
nxge_init_fail3:
	if (!isLDOMguest(nxgep)) {
		(void) nxge_txc_uninit(nxgep);
	}
nxge_init_fail2:
	nxge_free_mem_pool(nxgep);
nxge_init_fail1:
	NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
	    "<== nxge_init status (failed) = 0x%08x", status));
	return (status);

nxge_init_exit:
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_init status = 0x%08x",
	    status));
	return (status);
}


timeout_id_t
nxge_start_timer(p_nxge_t nxgep, fptrv_t func, int msec)
{
	if ((nxgep->suspended == 0) || (nxgep->suspended == DDI_RESUME)) {
		return (timeout(func, (caddr_t)nxgep,
		    drv_usectohz(1000 * msec)));
	}
	return (NULL);
}

/*ARGSUSED*/
void
nxge_stop_timer(p_nxge_t nxgep, timeout_id_t timerid)
{
	if (timerid) {
		(void) untimeout(timerid);
	}
}

void
nxge_uninit(p_nxge_t nxgep)
{
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_uninit"));

	if (!(nxgep->drv_state & STATE_HW_INITIALIZED)) {
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "==> nxge_uninit: not initialized"));
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "<== nxge_uninit"));
		return;
	}

	/* stop timer */
	if (nxgep->nxge_timerid) {
		nxge_stop_timer(nxgep, nxgep->nxge_timerid);
		nxgep->nxge_timerid = 0;
	}

	(void) nxge_link_monitor(nxgep, LINK_MONITOR_STOP);
	(void) nxge_intr_hw_disable(nxgep);

	/*
	 * Reset the receive MAC side.
	 */
	(void) nxge_rx_mac_disable(nxgep);

	/* Disable and soft reset the IPP */
	if (!isLDOMguest(nxgep))
		(void) nxge_ipp_disable(nxgep);

	/* Free classification resources */
	(void) nxge_classify_uninit(nxgep);

	/*
	 * Reset the transmit/receive DMA side.
	 */
	(void) nxge_txdma_hw_mode(nxgep, NXGE_DMA_STOP);
	(void) nxge_rxdma_hw_mode(nxgep, NXGE_DMA_STOP);

	nxge_uninit_txdma_channels(nxgep);
	nxge_uninit_rxdma_channels(nxgep);

	/*
	 * Reset the transmit MAC side.
	 */
	(void) nxge_tx_mac_disable(nxgep);

	nxge_free_mem_pool(nxgep);

	/*
	 * Start the timer if the reset flag is not set.
	 * If this reset flag is set, the link monitor
	 * will not be started in order to stop furthur bus
	 * activities coming from this interface.
	 * The driver will start the monitor function
	 * if the interface was initialized again later.
	 */
	if (!nxge_peu_reset_enable) {
		(void) nxge_link_monitor(nxgep, LINK_MONITOR_START);
	}

	nxgep->drv_state &= ~STATE_HW_INITIALIZED;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_uninit: "
	    "nxge_mblks_pending %d", nxge_mblks_pending));
}

void
nxge_get64(p_nxge_t nxgep, p_mblk_t mp)
{
#if defined(__i386)
	size_t		reg;
#else
	uint64_t	reg;
#endif
	uint64_t	regdata;
	int		i, retry;

	bcopy((char *)mp->b_rptr, (char *)&reg, sizeof (uint64_t));
	regdata = 0;
	retry = 1;

	for (i = 0; i < retry; i++) {
		NXGE_REG_RD64(nxgep->npi_handle, reg, &regdata);
	}
	bcopy((char *)&regdata, (char *)mp->b_rptr, sizeof (uint64_t));
}

void
nxge_put64(p_nxge_t nxgep, p_mblk_t mp)
{
#if defined(__i386)
	size_t		reg;
#else
	uint64_t	reg;
#endif
	uint64_t	buf[2];

	bcopy((char *)mp->b_rptr, (char *)&buf[0], 2 * sizeof (uint64_t));
#if defined(__i386)
	reg = (size_t)buf[0];
#else
	reg = buf[0];
#endif

	NXGE_NPI_PIO_WRITE64(nxgep->npi_handle, reg, buf[1]);
}


nxge_os_mutex_t nxgedebuglock;
int nxge_debug_init = 0;

/*ARGSUSED*/
/*VARARGS*/
void
nxge_debug_msg(p_nxge_t nxgep, uint64_t level, char *fmt, ...)
{
	char msg_buffer[1048];
	char prefix_buffer[32];
	int instance;
	uint64_t debug_level;
	int cmn_level = CE_CONT;
	va_list ap;

	if (nxgep && nxgep->nxge_debug_level != nxge_debug_level) {
		/* In case a developer has changed nxge_debug_level. */
		if (nxgep->nxge_debug_level != nxge_debug_level)
			nxgep->nxge_debug_level = nxge_debug_level;
	}

	debug_level = (nxgep == NULL) ? nxge_debug_level :
	    nxgep->nxge_debug_level;

	if ((level & debug_level) ||
	    (level == NXGE_NOTE) ||
	    (level == NXGE_ERR_CTL)) {
		/* do the msg processing */
		if (nxge_debug_init == 0) {
			MUTEX_INIT(&nxgedebuglock, NULL, MUTEX_DRIVER, NULL);
			nxge_debug_init = 1;
		}

		MUTEX_ENTER(&nxgedebuglock);

		if ((level & NXGE_NOTE)) {
			cmn_level = CE_NOTE;
		}

		if (level & NXGE_ERR_CTL) {
			cmn_level = CE_WARN;
		}

		va_start(ap, fmt);
		(void) vsprintf(msg_buffer, fmt, ap);
		va_end(ap);
		if (nxgep == NULL) {
			instance = -1;
			(void) sprintf(prefix_buffer, "%s :", "nxge");
		} else {
			instance = nxgep->instance;
			(void) sprintf(prefix_buffer,
			    "%s%d :", "nxge", instance);
		}

		MUTEX_EXIT(&nxgedebuglock);
		cmn_err(cmn_level, "!%s %s\n",
		    prefix_buffer, msg_buffer);

	}
}

char *
nxge_dump_packet(char *addr, int size)
{
	uchar_t *ap = (uchar_t *)addr;
	int i;
	static char etherbuf[1024];
	char *cp = etherbuf;
	char digits[] = "0123456789abcdef";

	if (!size)
		size = 60;

	if (size > MAX_DUMP_SZ) {
		/* Dump the leading bytes */
		for (i = 0; i < MAX_DUMP_SZ/2; i++) {
			if (*ap > 0x0f)
				*cp++ = digits[*ap >> 4];
			*cp++ = digits[*ap++ & 0xf];
			*cp++ = ':';
		}
		for (i = 0; i < 20; i++)
			*cp++ = '.';
		/* Dump the last MAX_DUMP_SZ/2 bytes */
		ap = (uchar_t *)(addr + (size - MAX_DUMP_SZ/2));
		for (i = 0; i < MAX_DUMP_SZ/2; i++) {
			if (*ap > 0x0f)
				*cp++ = digits[*ap >> 4];
			*cp++ = digits[*ap++ & 0xf];
			*cp++ = ':';
		}
	} else {
		for (i = 0; i < size; i++) {
			if (*ap > 0x0f)
				*cp++ = digits[*ap >> 4];
			*cp++ = digits[*ap++ & 0xf];
			*cp++ = ':';
		}
	}
	*--cp = 0;
	return (etherbuf);
}

#ifdef	NXGE_DEBUG
static void
nxge_test_map_regs(p_nxge_t nxgep)
{
	ddi_acc_handle_t cfg_handle;
	p_pci_cfg_t	cfg_ptr;
	ddi_acc_handle_t dev_handle;
	char		*dev_ptr;
	ddi_acc_handle_t pci_config_handle;
	uint32_t	regval;
	int		i;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_test_map_regs"));

	dev_handle = nxgep->dev_regs->nxge_regh;
	dev_ptr = (char *)nxgep->dev_regs->nxge_regp;

	if (NXGE_IS_VALID_NEPTUNE_TYPE(nxgep)) {
		cfg_handle = nxgep->dev_regs->nxge_pciregh;
		cfg_ptr = (void *)nxgep->dev_regs->nxge_pciregp;

		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "Neptune PCI regp cfg_ptr 0x%llx", (char *)cfg_ptr));
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "Neptune PCI cfg_ptr vendor id ptr 0x%llx",
		    &cfg_ptr->vendorid));
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "\tvendorid 0x%x devid 0x%x",
		    NXGE_PIO_READ16(cfg_handle, &cfg_ptr->vendorid, 0),
		    NXGE_PIO_READ16(cfg_handle, &cfg_ptr->devid,    0)));
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "PCI BAR: base 0x%x base14 0x%x base 18 0x%x "
		    "bar1c 0x%x",
		    NXGE_PIO_READ32(cfg_handle, &cfg_ptr->base,   0),
		    NXGE_PIO_READ32(cfg_handle, &cfg_ptr->base14, 0),
		    NXGE_PIO_READ32(cfg_handle, &cfg_ptr->base18, 0),
		    NXGE_PIO_READ32(cfg_handle, &cfg_ptr->base1c, 0)));
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "\nNeptune PCI BAR: base20 0x%x base24 0x%x "
		    "base 28 0x%x bar2c 0x%x\n",
		    NXGE_PIO_READ32(cfg_handle, &cfg_ptr->base20, 0),
		    NXGE_PIO_READ32(cfg_handle, &cfg_ptr->base24, 0),
		    NXGE_PIO_READ32(cfg_handle, &cfg_ptr->base28, 0),
		    NXGE_PIO_READ32(cfg_handle, &cfg_ptr->base2c, 0)));
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "\nNeptune PCI BAR: base30 0x%x\n",
		    NXGE_PIO_READ32(cfg_handle, &cfg_ptr->base30, 0)));

		cfg_handle = nxgep->dev_regs->nxge_pciregh;
		cfg_ptr = (void *)nxgep->dev_regs->nxge_pciregp;
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "first  0x%llx second 0x%llx third 0x%llx "
		    "last 0x%llx ",
		    NXGE_PIO_READ64(dev_handle,
		    (uint64_t *)(dev_ptr + 0),  0),
		    NXGE_PIO_READ64(dev_handle,
		    (uint64_t *)(dev_ptr + 8),  0),
		    NXGE_PIO_READ64(dev_handle,
		    (uint64_t *)(dev_ptr + 16), 0),
		    NXGE_PIO_READ64(cfg_handle,
		    (uint64_t *)(dev_ptr + 24), 0)));
	}
}

#endif

static void
nxge_suspend(p_nxge_t nxgep)
{
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_suspend"));

	nxge_intrs_disable(nxgep);
	nxge_destroy_dev(nxgep);

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_suspend"));
}

static nxge_status_t
nxge_resume(p_nxge_t nxgep)
{
	nxge_status_t status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_resume"));

	nxgep->suspended = DDI_RESUME;
	(void) nxge_link_monitor(nxgep, LINK_MONITOR_START);
	(void) nxge_rxdma_hw_mode(nxgep, NXGE_DMA_START);
	(void) nxge_txdma_hw_mode(nxgep, NXGE_DMA_START);
	(void) nxge_rx_mac_enable(nxgep);
	(void) nxge_tx_mac_enable(nxgep);
	nxge_intrs_enable(nxgep);
	nxgep->suspended = 0;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "<== nxge_resume status = 0x%x", status));
	return (status);
}

static nxge_status_t
nxge_setup_dev(p_nxge_t nxgep)
{
	nxge_status_t	status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_setup_dev port %d",
	    nxgep->mac.portnum));

	status = nxge_link_init(nxgep);

	if (fm_check_acc_handle(nxgep->dev_regs->nxge_regh) != DDI_FM_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "port%d Bad register acc handle", nxgep->mac.portnum));
		status = NXGE_ERROR;
	}

	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    " nxge_setup_dev status "
		    "(xcvr init 0x%08x)", status));
		goto nxge_setup_dev_exit;
	}

nxge_setup_dev_exit:
	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "<== nxge_setup_dev port %d status = 0x%08x",
	    nxgep->mac.portnum, status));

	return (status);
}

static void
nxge_destroy_dev(p_nxge_t nxgep)
{
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_destroy_dev"));

	(void) nxge_link_monitor(nxgep, LINK_MONITOR_STOP);

	(void) nxge_hw_stop(nxgep);

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_destroy_dev"));
}

static nxge_status_t
nxge_setup_system_dma_pages(p_nxge_t nxgep)
{
	int 			ddi_status = DDI_SUCCESS;
	uint_t 			count;
	ddi_dma_cookie_t 	cookie;
	uint_t 			iommu_pagesize;
	nxge_status_t		status = NXGE_OK;

	NXGE_ERROR_MSG((nxgep, DDI_CTL, "==> nxge_setup_system_dma_pages"));
	nxgep->sys_page_sz = ddi_ptob(nxgep->dip, (ulong_t)1);
	if (nxgep->niu_type != N2_NIU) {
		iommu_pagesize = dvma_pagesize(nxgep->dip);
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    " nxge_setup_system_dma_pages: page %d (ddi_ptob %d) "
		    " default_block_size %d iommu_pagesize %d",
		    nxgep->sys_page_sz,
		    ddi_ptob(nxgep->dip, (ulong_t)1),
		    nxgep->rx_default_block_size,
		    iommu_pagesize));

		if (iommu_pagesize != 0) {
			if (nxgep->sys_page_sz == iommu_pagesize) {
				if (iommu_pagesize > 0x4000)
					nxgep->sys_page_sz = 0x4000;
			} else {
				if (nxgep->sys_page_sz > iommu_pagesize)
					nxgep->sys_page_sz = iommu_pagesize;
			}
		}
	}
	nxgep->sys_page_mask = ~(nxgep->sys_page_sz - 1);
	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "==> nxge_setup_system_dma_pages: page %d (ddi_ptob %d) "
	    "default_block_size %d page mask %d",
	    nxgep->sys_page_sz,
	    ddi_ptob(nxgep->dip, (ulong_t)1),
	    nxgep->rx_default_block_size,
	    nxgep->sys_page_mask));


	switch (nxgep->sys_page_sz) {
	default:
		nxgep->sys_page_sz = 0x1000;
		nxgep->sys_page_mask = ~(nxgep->sys_page_sz - 1);
		nxgep->rx_default_block_size = 0x1000;
		nxgep->rx_bksize_code = RBR_BKSIZE_4K;
		break;
	case 0x1000:
		nxgep->rx_default_block_size = 0x1000;
		nxgep->rx_bksize_code = RBR_BKSIZE_4K;
		break;
	case 0x2000:
		nxgep->rx_default_block_size = 0x2000;
		nxgep->rx_bksize_code = RBR_BKSIZE_8K;
		break;
	case 0x4000:
		nxgep->rx_default_block_size = 0x4000;
		nxgep->rx_bksize_code = RBR_BKSIZE_16K;
		break;
	case 0x8000:
		nxgep->rx_default_block_size = 0x8000;
		nxgep->rx_bksize_code = RBR_BKSIZE_32K;
		break;
	}

#ifndef USE_RX_BIG_BUF
	nxge_rx_dma_attr.dma_attr_align = nxgep->sys_page_sz;
#else
		nxgep->rx_default_block_size = 0x2000;
		nxgep->rx_bksize_code = RBR_BKSIZE_8K;
#endif
	/*
	 * Get the system DMA burst size.
	 */
	ddi_status = ddi_dma_alloc_handle(nxgep->dip, &nxge_tx_dma_attr,
	    DDI_DMA_DONTWAIT, 0,
	    &nxgep->dmasparehandle);
	if (ddi_status != DDI_SUCCESS) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "ddi_dma_alloc_handle: failed "
		    " status 0x%x", ddi_status));
		goto nxge_get_soft_properties_exit;
	}

	ddi_status = ddi_dma_addr_bind_handle(nxgep->dmasparehandle, NULL,
	    (caddr_t)nxgep->dmasparehandle,
	    sizeof (nxgep->dmasparehandle),
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    DDI_DMA_DONTWAIT, 0,
	    &cookie, &count);
	if (ddi_status != DDI_DMA_MAPPED) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "Binding spare handle to find system"
		    " burstsize failed."));
		ddi_status = DDI_FAILURE;
		goto nxge_get_soft_properties_fail1;
	}

	nxgep->sys_burst_sz = ddi_dma_burstsizes(nxgep->dmasparehandle);
	(void) ddi_dma_unbind_handle(nxgep->dmasparehandle);

nxge_get_soft_properties_fail1:
	ddi_dma_free_handle(&nxgep->dmasparehandle);

nxge_get_soft_properties_exit:

	if (ddi_status != DDI_SUCCESS)
		status |= (NXGE_ERROR | NXGE_DDI_FAILED);

	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "<== nxge_setup_system_dma_pages status = 0x%08x", status));
	return (status);
}

static nxge_status_t
nxge_alloc_mem_pool(p_nxge_t nxgep)
{
	nxge_status_t	status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_alloc_mem_pool"));

	status = nxge_alloc_rx_mem_pool(nxgep);
	if (status != NXGE_OK) {
		return (NXGE_ERROR);
	}

	status = nxge_alloc_tx_mem_pool(nxgep);
	if (status != NXGE_OK) {
		nxge_free_rx_mem_pool(nxgep);
		return (NXGE_ERROR);
	}

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_alloc_mem_pool"));
	return (NXGE_OK);
}

static void
nxge_free_mem_pool(p_nxge_t nxgep)
{
	NXGE_DEBUG_MSG((nxgep, MEM_CTL, "==> nxge_free_mem_pool"));

	nxge_free_rx_mem_pool(nxgep);
	nxge_free_tx_mem_pool(nxgep);

	NXGE_DEBUG_MSG((nxgep, MEM_CTL, "<== nxge_free_mem_pool"));
}

nxge_status_t
nxge_alloc_rx_mem_pool(p_nxge_t nxgep)
{
	uint32_t		rdc_max;
	p_nxge_dma_pt_cfg_t	p_all_cfgp;
	p_nxge_hw_pt_cfg_t	p_cfgp;
	p_nxge_dma_pool_t	dma_poolp;
	p_nxge_dma_common_t	*dma_buf_p;
	p_nxge_dma_pool_t	dma_cntl_poolp;
	p_nxge_dma_common_t	*dma_cntl_p;
	uint32_t 		*num_chunks; /* per dma */
	nxge_status_t		status = NXGE_OK;

	uint32_t		nxge_port_rbr_size;
	uint32_t		nxge_port_rbr_spare_size;
	uint32_t		nxge_port_rcr_size;
	uint32_t		rx_cntl_alloc_size;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_alloc_rx_mem_pool"));

	p_all_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
	p_cfgp = (p_nxge_hw_pt_cfg_t)&p_all_cfgp->hw_config;
	rdc_max = NXGE_MAX_RDCS;

	/*
	 * Allocate memory for the common DMA data structures.
	 */
	dma_poolp = (p_nxge_dma_pool_t)KMEM_ZALLOC(sizeof (nxge_dma_pool_t),
	    KM_SLEEP);
	dma_buf_p = (p_nxge_dma_common_t *)KMEM_ZALLOC(
	    sizeof (p_nxge_dma_common_t) * rdc_max, KM_SLEEP);

	dma_cntl_poolp = (p_nxge_dma_pool_t)
	    KMEM_ZALLOC(sizeof (nxge_dma_pool_t), KM_SLEEP);
	dma_cntl_p = (p_nxge_dma_common_t *)KMEM_ZALLOC(
	    sizeof (p_nxge_dma_common_t) * rdc_max, KM_SLEEP);

	num_chunks = (uint32_t *)KMEM_ZALLOC(
	    sizeof (uint32_t) * rdc_max, KM_SLEEP);

	/*
	 * Assume that each DMA channel will be configured with
	 * the default block size.
	 * rbr block counts are modulo the batch count (16).
	 */
	nxge_port_rbr_size = p_all_cfgp->rbr_size;
	nxge_port_rcr_size = p_all_cfgp->rcr_size;

	if (!nxge_port_rbr_size) {
		nxge_port_rbr_size = NXGE_RBR_RBB_DEFAULT;
	}
	if (nxge_port_rbr_size % NXGE_RXDMA_POST_BATCH) {
		nxge_port_rbr_size = (NXGE_RXDMA_POST_BATCH *
		    (nxge_port_rbr_size / NXGE_RXDMA_POST_BATCH + 1));
	}

	p_all_cfgp->rbr_size = nxge_port_rbr_size;
	nxge_port_rbr_spare_size = nxge_rbr_spare_size;

	if (nxge_port_rbr_spare_size % NXGE_RXDMA_POST_BATCH) {
		nxge_port_rbr_spare_size = (NXGE_RXDMA_POST_BATCH *
		    (nxge_port_rbr_spare_size / NXGE_RXDMA_POST_BATCH + 1));
	}
	if (nxge_port_rbr_size > RBR_DEFAULT_MAX_BLKS) {
		NXGE_DEBUG_MSG((nxgep, MEM_CTL,
		    "nxge_alloc_rx_mem_pool: RBR size too high %d, "
		    "set to default %d",
		    nxge_port_rbr_size, RBR_DEFAULT_MAX_BLKS));
		nxge_port_rbr_size = RBR_DEFAULT_MAX_BLKS;
	}
	if (nxge_port_rcr_size > RCR_DEFAULT_MAX) {
		NXGE_DEBUG_MSG((nxgep, MEM_CTL,
		    "nxge_alloc_rx_mem_pool: RCR too high %d, "
		    "set to default %d",
		    nxge_port_rcr_size, RCR_DEFAULT_MAX));
		nxge_port_rcr_size = RCR_DEFAULT_MAX;
	}

	/*
	 * N2/NIU has limitation on the descriptor sizes (contiguous
	 * memory allocation on data buffers to 4M (contig_mem_alloc)
	 * and little endian for control buffers (must use the ddi/dki mem alloc
	 * function).
	 */
#if	defined(sun4v) && defined(NIU_LP_WORKAROUND)
	if (nxgep->niu_type == N2_NIU) {
		nxge_port_rbr_spare_size = 0;
		if ((nxge_port_rbr_size > NXGE_NIU_CONTIG_RBR_MAX) ||
		    (!ISP2(nxge_port_rbr_size))) {
			nxge_port_rbr_size = NXGE_NIU_CONTIG_RBR_MAX;
		}
		if ((nxge_port_rcr_size > NXGE_NIU_CONTIG_RCR_MAX) ||
		    (!ISP2(nxge_port_rcr_size))) {
			nxge_port_rcr_size = NXGE_NIU_CONTIG_RCR_MAX;
		}
	}
#endif

	/*
	 * Addresses of receive block ring, receive completion ring and the
	 * mailbox must be all cache-aligned (64 bytes).
	 */
	rx_cntl_alloc_size = nxge_port_rbr_size + nxge_port_rbr_spare_size;
	rx_cntl_alloc_size *= (sizeof (rx_desc_t));
	rx_cntl_alloc_size += (sizeof (rcr_entry_t) * nxge_port_rcr_size);
	rx_cntl_alloc_size += sizeof (rxdma_mailbox_t);

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL, "==> nxge_alloc_rx_mem_pool: "
	    "nxge_port_rbr_size = %d nxge_port_rbr_spare_size = %d "
	    "nxge_port_rcr_size = %d "
	    "rx_cntl_alloc_size = %d",
	    nxge_port_rbr_size, nxge_port_rbr_spare_size,
	    nxge_port_rcr_size,
	    rx_cntl_alloc_size));

#if	defined(sun4v) && defined(NIU_LP_WORKAROUND)
	if (nxgep->niu_type == N2_NIU) {
		uint32_t rx_buf_alloc_size = (nxgep->rx_default_block_size *
		    (nxge_port_rbr_size + nxge_port_rbr_spare_size));

		if (!ISP2(rx_buf_alloc_size)) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "==> nxge_alloc_rx_mem_pool: "
			    " must be power of 2"));
			status |= (NXGE_ERROR | NXGE_DDI_FAILED);
			goto nxge_alloc_rx_mem_pool_exit;
		}

		if (rx_buf_alloc_size > (1 << 22)) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "==> nxge_alloc_rx_mem_pool: "
			    " limit size to 4M"));
			status |= (NXGE_ERROR | NXGE_DDI_FAILED);
			goto nxge_alloc_rx_mem_pool_exit;
		}

		if (rx_cntl_alloc_size < 0x2000) {
			rx_cntl_alloc_size = 0x2000;
		}
	}
#endif
	nxgep->nxge_port_rbr_size = nxge_port_rbr_size;
	nxgep->nxge_port_rcr_size = nxge_port_rcr_size;
	nxgep->nxge_port_rbr_spare_size = nxge_port_rbr_spare_size;
	nxgep->nxge_port_rx_cntl_alloc_size = rx_cntl_alloc_size;

	dma_poolp->ndmas = p_cfgp->max_rdcs;
	dma_poolp->num_chunks = num_chunks;
	dma_poolp->buf_allocated = B_TRUE;
	nxgep->rx_buf_pool_p = dma_poolp;
	dma_poolp->dma_buf_pool_p = dma_buf_p;

	dma_cntl_poolp->ndmas = p_cfgp->max_rdcs;
	dma_cntl_poolp->buf_allocated = B_TRUE;
	nxgep->rx_cntl_pool_p = dma_cntl_poolp;
	dma_cntl_poolp->dma_buf_pool_p = dma_cntl_p;

	/* Allocate the receive rings, too. */
	nxgep->rx_rbr_rings =
	    KMEM_ZALLOC(sizeof (rx_rbr_rings_t), KM_SLEEP);
	nxgep->rx_rbr_rings->rbr_rings =
	    KMEM_ZALLOC(sizeof (p_rx_rbr_ring_t) * rdc_max, KM_SLEEP);
	nxgep->rx_rcr_rings =
	    KMEM_ZALLOC(sizeof (rx_rcr_rings_t), KM_SLEEP);
	nxgep->rx_rcr_rings->rcr_rings =
	    KMEM_ZALLOC(sizeof (p_rx_rcr_ring_t) * rdc_max, KM_SLEEP);
	nxgep->rx_mbox_areas_p =
	    KMEM_ZALLOC(sizeof (rx_mbox_areas_t), KM_SLEEP);
	nxgep->rx_mbox_areas_p->rxmbox_areas =
	    KMEM_ZALLOC(sizeof (p_rx_mbox_t) * rdc_max, KM_SLEEP);

	nxgep->rx_rbr_rings->ndmas = nxgep->rx_rcr_rings->ndmas =
	    p_cfgp->max_rdcs;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL,
	    "<== nxge_alloc_rx_mem_pool:status 0x%08x", status));

nxge_alloc_rx_mem_pool_exit:
	return (status);
}

/*
 * nxge_alloc_rxb
 *
 *	Allocate buffers for an RDC.
 *
 * Arguments:
 * 	nxgep
 * 	channel	The channel to map into our kernel space.
 *
 * Notes:
 *
 * NPI function calls:
 *
 * NXGE function calls:
 *
 * Registers accessed:
 *
 * Context:
 *
 * Taking apart:
 *
 * Open questions:
 *
 */
nxge_status_t
nxge_alloc_rxb(
	p_nxge_t nxgep,
	int channel)
{
	size_t			rx_buf_alloc_size;
	nxge_status_t		status = NXGE_OK;

	nxge_dma_common_t	**data;
	nxge_dma_common_t	**control;
	uint32_t 		*num_chunks;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_alloc_rbb"));

	/*
	 * Allocate memory for the receive buffers and descriptor rings.
	 * Replace these allocation functions with the interface functions
	 * provided by the partition manager if/when they are available.
	 */

	/*
	 * Allocate memory for the receive buffer blocks.
	 */
	rx_buf_alloc_size = (nxgep->rx_default_block_size *
	    (nxgep->nxge_port_rbr_size + nxgep->nxge_port_rbr_spare_size));

	data = &nxgep->rx_buf_pool_p->dma_buf_pool_p[channel];
	num_chunks = &nxgep->rx_buf_pool_p->num_chunks[channel];

	if ((status = nxge_alloc_rx_buf_dma(
	    nxgep, channel, data, rx_buf_alloc_size,
	    nxgep->rx_default_block_size, num_chunks)) != NXGE_OK) {
		return (status);
	}

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL, "<== nxge_alloc_rxb(): "
	    "dma %d dma_buf_p %llx &dma_buf_p %llx", channel, *data, data));

	/*
	 * Allocate memory for descriptor rings and mailbox.
	 */
	control = &nxgep->rx_cntl_pool_p->dma_buf_pool_p[channel];

	if ((status = nxge_alloc_rx_cntl_dma(
	    nxgep, channel, control, nxgep->nxge_port_rx_cntl_alloc_size))
	    != NXGE_OK) {
		nxge_free_rx_cntl_dma(nxgep, *control);
		(*data)->buf_alloc_state |= BUF_ALLOCATED_WAIT_FREE;
		nxge_free_rx_buf_dma(nxgep, *data, *num_chunks);
		return (status);
	}

	NXGE_DEBUG_MSG((nxgep, DMA_CTL,
	    "<== nxge_alloc_rx_mem_pool:status 0x%08x", status));

	return (status);
}

void
nxge_free_rxb(
	p_nxge_t nxgep,
	int channel)
{
	nxge_dma_common_t	*data;
	nxge_dma_common_t	*control;
	uint32_t 		num_chunks;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_alloc_rbb"));

	data = nxgep->rx_buf_pool_p->dma_buf_pool_p[channel];
	num_chunks = nxgep->rx_buf_pool_p->num_chunks[channel];
	nxge_free_rx_buf_dma(nxgep, data, num_chunks);

	nxgep->rx_buf_pool_p->dma_buf_pool_p[channel] = 0;
	nxgep->rx_buf_pool_p->num_chunks[channel] = 0;

	control = nxgep->rx_cntl_pool_p->dma_buf_pool_p[channel];
	nxge_free_rx_cntl_dma(nxgep, control);

	nxgep->rx_cntl_pool_p->dma_buf_pool_p[channel] = 0;

	KMEM_FREE(data, sizeof (nxge_dma_common_t) * NXGE_DMA_BLOCK);
	KMEM_FREE(control, sizeof (nxge_dma_common_t));

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "<== nxge_alloc_rbb"));
}

static void
nxge_free_rx_mem_pool(p_nxge_t nxgep)
{
	int rdc_max = NXGE_MAX_RDCS;

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL, "==> nxge_free_rx_mem_pool"));

	if (!nxgep->rx_buf_pool_p || !nxgep->rx_buf_pool_p->buf_allocated) {
		NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
		    "<== nxge_free_rx_mem_pool "
		    "(null rx buf pool or buf not allocated"));
		return;
	}
	if (!nxgep->rx_cntl_pool_p || !nxgep->rx_cntl_pool_p->buf_allocated) {
		NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
		    "<== nxge_free_rx_mem_pool "
		    "(null rx cntl buf pool or cntl buf not allocated"));
		return;
	}

	KMEM_FREE(nxgep->rx_cntl_pool_p->dma_buf_pool_p,
	    sizeof (p_nxge_dma_common_t) * rdc_max);
	KMEM_FREE(nxgep->rx_cntl_pool_p, sizeof (nxge_dma_pool_t));

	KMEM_FREE(nxgep->rx_buf_pool_p->num_chunks,
	    sizeof (uint32_t) * rdc_max);
	KMEM_FREE(nxgep->rx_buf_pool_p->dma_buf_pool_p,
	    sizeof (p_nxge_dma_common_t) * rdc_max);
	KMEM_FREE(nxgep->rx_buf_pool_p, sizeof (nxge_dma_pool_t));

	nxgep->rx_buf_pool_p = 0;
	nxgep->rx_cntl_pool_p = 0;

	KMEM_FREE(nxgep->rx_rbr_rings->rbr_rings,
	    sizeof (p_rx_rbr_ring_t) * rdc_max);
	KMEM_FREE(nxgep->rx_rbr_rings, sizeof (rx_rbr_rings_t));
	KMEM_FREE(nxgep->rx_rcr_rings->rcr_rings,
	    sizeof (p_rx_rcr_ring_t) * rdc_max);
	KMEM_FREE(nxgep->rx_rcr_rings, sizeof (rx_rcr_rings_t));
	KMEM_FREE(nxgep->rx_mbox_areas_p->rxmbox_areas,
	    sizeof (p_rx_mbox_t) * rdc_max);
	KMEM_FREE(nxgep->rx_mbox_areas_p, sizeof (rx_mbox_areas_t));

	nxgep->rx_rbr_rings = 0;
	nxgep->rx_rcr_rings = 0;
	nxgep->rx_mbox_areas_p = 0;

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL, "<== nxge_free_rx_mem_pool"));
}


static nxge_status_t
nxge_alloc_rx_buf_dma(p_nxge_t nxgep, uint16_t dma_channel,
	p_nxge_dma_common_t *dmap,
	size_t alloc_size, size_t block_size, uint32_t *num_chunks)
{
	p_nxge_dma_common_t 	rx_dmap;
	nxge_status_t		status = NXGE_OK;
	size_t			total_alloc_size;
	size_t			allocated = 0;
	int			i, size_index, array_size;
	boolean_t		use_kmem_alloc = B_FALSE;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_alloc_rx_buf_dma"));

	rx_dmap = (p_nxge_dma_common_t)
	    KMEM_ZALLOC(sizeof (nxge_dma_common_t) * NXGE_DMA_BLOCK,
	    KM_SLEEP);

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
	    " alloc_rx_buf_dma rdc %d asize %x bsize %x bbuf %llx ",
	    dma_channel, alloc_size, block_size, dmap));

	total_alloc_size = alloc_size;

#if defined(RX_USE_RECLAIM_POST)
	total_alloc_size = alloc_size + alloc_size/4;
#endif

	i = 0;
	size_index = 0;
	array_size =  sizeof (alloc_sizes)/sizeof (size_t);
	while ((alloc_sizes[size_index] < alloc_size) &&
	    (size_index < array_size))
		size_index++;
	if (size_index >= array_size) {
		size_index = array_size - 1;
	}

	/* For Neptune, use kmem_alloc if the kmem flag is set. */
	if (nxgep->niu_type != N2_NIU && nxge_use_kmem_alloc) {
		use_kmem_alloc = B_TRUE;
#if defined(__i386) || defined(__amd64)
		size_index = 0;
#endif
		NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
		    "==> nxge_alloc_rx_buf_dma: "
		    "Neptune use kmem_alloc() - size_index %d",
		    size_index));
	}

	while ((allocated < total_alloc_size) &&
	    (size_index >= 0) && (i < NXGE_DMA_BLOCK)) {
		rx_dmap[i].dma_chunk_index = i;
		rx_dmap[i].block_size = block_size;
		rx_dmap[i].alength = alloc_sizes[size_index];
		rx_dmap[i].orig_alength = rx_dmap[i].alength;
		rx_dmap[i].nblocks = alloc_sizes[size_index] / block_size;
		rx_dmap[i].dma_channel = dma_channel;
		rx_dmap[i].contig_alloc_type = B_FALSE;
		rx_dmap[i].kmem_alloc_type = B_FALSE;
		rx_dmap[i].buf_alloc_type = DDI_MEM_ALLOC;

		/*
		 * N2/NIU: data buffers must be contiguous as the driver
		 *	   needs to call Hypervisor api to set up
		 *	   logical pages.
		 */
		if ((nxgep->niu_type == N2_NIU) && (NXGE_DMA_BLOCK == 1)) {
			rx_dmap[i].contig_alloc_type = B_TRUE;
			rx_dmap[i].buf_alloc_type = CONTIG_MEM_ALLOC;
		} else if (use_kmem_alloc) {
			/* For Neptune, use kmem_alloc */
			NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
			    "==> nxge_alloc_rx_buf_dma: "
			    "Neptune use kmem_alloc()"));
			rx_dmap[i].kmem_alloc_type = B_TRUE;
			rx_dmap[i].buf_alloc_type = KMEM_ALLOC;
		}

		NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
		    "alloc_rx_buf_dma rdc %d chunk %d bufp %llx size %x "
		    "i %d nblocks %d alength %d",
		    dma_channel, i, &rx_dmap[i], block_size,
		    i, rx_dmap[i].nblocks,
		    rx_dmap[i].alength));
		status = nxge_dma_mem_alloc(nxgep, nxge_force_dma,
		    &nxge_rx_dma_attr,
		    rx_dmap[i].alength,
		    &nxge_dev_buf_dma_acc_attr,
		    DDI_DMA_READ | DDI_DMA_STREAMING,
		    (p_nxge_dma_common_t)(&rx_dmap[i]));
		if (status != NXGE_OK) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "nxge_alloc_rx_buf_dma: Alloc Failed: "
			    "dma %d size_index %d size requested %d",
			    dma_channel,
			    size_index,
			    rx_dmap[i].alength));
			size_index--;
		} else {
			rx_dmap[i].buf_alloc_state = BUF_ALLOCATED;
			NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
			    " nxge_alloc_rx_buf_dma DONE  alloc mem: "
			    "dma %d dma_buf_p $%p kaddrp $%p alength %d "
			    "buf_alloc_state %d alloc_type %d",
			    dma_channel,
			    &rx_dmap[i],
			    rx_dmap[i].kaddrp,
			    rx_dmap[i].alength,
			    rx_dmap[i].buf_alloc_state,
			    rx_dmap[i].buf_alloc_type));
			NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
			    " alloc_rx_buf_dma allocated rdc %d "
			    "chunk %d size %x dvma %x bufp %llx kaddrp $%p",
			    dma_channel, i, rx_dmap[i].alength,
			    rx_dmap[i].ioaddr_pp, &rx_dmap[i],
			    rx_dmap[i].kaddrp));
			i++;
			allocated += alloc_sizes[size_index];
		}
	}

	if (allocated < total_alloc_size) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "==> nxge_alloc_rx_buf_dma: not enough for channel %d "
		    "allocated 0x%x requested 0x%x",
		    dma_channel,
		    allocated, total_alloc_size));
		status = NXGE_ERROR;
		goto nxge_alloc_rx_mem_fail1;
	}

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
	    "==> nxge_alloc_rx_buf_dma: Allocated for channel %d "
	    "allocated 0x%x requested 0x%x",
	    dma_channel,
	    allocated, total_alloc_size));

	NXGE_DEBUG_MSG((nxgep, DMA_CTL,
	    " alloc_rx_buf_dma rdc %d allocated %d chunks",
	    dma_channel, i));
	*num_chunks = i;
	*dmap = rx_dmap;

	goto nxge_alloc_rx_mem_exit;

nxge_alloc_rx_mem_fail1:
	KMEM_FREE(rx_dmap, sizeof (nxge_dma_common_t) * NXGE_DMA_BLOCK);

nxge_alloc_rx_mem_exit:
	NXGE_DEBUG_MSG((nxgep, DMA_CTL,
	    "<== nxge_alloc_rx_buf_dma status 0x%08x", status));

	return (status);
}

/*ARGSUSED*/
static void
nxge_free_rx_buf_dma(p_nxge_t nxgep, p_nxge_dma_common_t dmap,
    uint32_t num_chunks)
{
	int		i;

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
	    "==> nxge_free_rx_buf_dma: # of chunks %d", num_chunks));

	if (dmap == 0)
		return;

	for (i = 0; i < num_chunks; i++) {
		NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
		    "==> nxge_free_rx_buf_dma: chunk %d dmap 0x%llx",
		    i, dmap));
		nxge_dma_free_rx_data_buf(dmap++);
	}

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL, "==> nxge_free_rx_buf_dma"));
}

/*ARGSUSED*/
static nxge_status_t
nxge_alloc_rx_cntl_dma(p_nxge_t nxgep, uint16_t dma_channel,
    p_nxge_dma_common_t *dmap, size_t size)
{
	p_nxge_dma_common_t 	rx_dmap;
	nxge_status_t		status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_alloc_rx_cntl_dma"));

	rx_dmap = (p_nxge_dma_common_t)
	    KMEM_ZALLOC(sizeof (nxge_dma_common_t), KM_SLEEP);

	rx_dmap->contig_alloc_type = B_FALSE;
	rx_dmap->kmem_alloc_type = B_FALSE;

	status = nxge_dma_mem_alloc(nxgep, nxge_force_dma,
	    &nxge_desc_dma_attr,
	    size,
	    &nxge_dev_desc_dma_acc_attr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    rx_dmap);
	if (status != NXGE_OK) {
		goto nxge_alloc_rx_cntl_dma_fail1;
	}

	*dmap = rx_dmap;
	goto nxge_alloc_rx_cntl_dma_exit;

nxge_alloc_rx_cntl_dma_fail1:
	KMEM_FREE(rx_dmap, sizeof (nxge_dma_common_t));

nxge_alloc_rx_cntl_dma_exit:
	NXGE_DEBUG_MSG((nxgep, DMA_CTL,
	    "<== nxge_alloc_rx_cntl_dma status 0x%08x", status));

	return (status);
}

/*ARGSUSED*/
static void
nxge_free_rx_cntl_dma(p_nxge_t nxgep, p_nxge_dma_common_t dmap)
{
	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_free_rx_cntl_dma"));

	if (dmap == 0)
		return;

	nxge_dma_mem_free(dmap);

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "<== nxge_free_rx_cntl_dma"));
}

typedef struct {
	size_t	tx_size;
	size_t	cr_size;
	size_t	threshhold;
} nxge_tdc_sizes_t;

static
nxge_status_t
nxge_tdc_sizes(
	nxge_t *nxgep,
	nxge_tdc_sizes_t *sizes)
{
	uint32_t threshhold;	/* The bcopy() threshhold */
	size_t tx_size;		/* Transmit buffer size */
	size_t cr_size;		/* Completion ring size */

	/*
	 * Assume that each DMA channel will be configured with the
	 * default transmit buffer size for copying transmit data.
	 * (If a packet is bigger than this, it will not be copied.)
	 */
	if (nxgep->niu_type == N2_NIU) {
		threshhold = TX_BCOPY_SIZE;
	} else {
		threshhold = nxge_bcopy_thresh;
	}
	tx_size = nxge_tx_ring_size * threshhold;

	cr_size = nxge_tx_ring_size * sizeof (tx_desc_t);
	cr_size += sizeof (txdma_mailbox_t);

#if	defined(sun4v) && defined(NIU_LP_WORKAROUND)
	if (nxgep->niu_type == N2_NIU) {
		if (!ISP2(tx_size)) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "==> nxge_tdc_sizes: Tx size"
			    " must be power of 2"));
			return (NXGE_ERROR);
		}

		if (tx_size > (1 << 22)) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "==> nxge_tdc_sizes: Tx size"
			    " limited to 4M"));
			return (NXGE_ERROR);
		}

		if (cr_size < 0x2000)
			cr_size = 0x2000;
	}
#endif

	sizes->threshhold = threshhold;
	sizes->tx_size = tx_size;
	sizes->cr_size = cr_size;

	return (NXGE_OK);
}
/*
 * nxge_alloc_txb
 *
 *	Allocate buffers for an TDC.
 *
 * Arguments:
 * 	nxgep
 * 	channel	The channel to map into our kernel space.
 *
 * Notes:
 *
 * NPI function calls:
 *
 * NXGE function calls:
 *
 * Registers accessed:
 *
 * Context:
 *
 * Taking apart:
 *
 * Open questions:
 *
 */
nxge_status_t
nxge_alloc_txb(
	p_nxge_t nxgep,
	int channel)
{
	nxge_dma_common_t	**dma_buf_p;
	nxge_dma_common_t	**dma_cntl_p;
	uint32_t 		*num_chunks;
	nxge_status_t		status = NXGE_OK;

	nxge_tdc_sizes_t	sizes;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_alloc_tbb"));

	if (nxge_tdc_sizes(nxgep, &sizes) != NXGE_OK)
		return (NXGE_ERROR);

	/*
	 * Allocate memory for transmit buffers and descriptor rings.
	 * Replace these allocation functions with the interface functions
	 * provided by the partition manager Real Soon Now.
	 */
	dma_buf_p = &nxgep->tx_buf_pool_p->dma_buf_pool_p[channel];
	num_chunks = &nxgep->tx_buf_pool_p->num_chunks[channel];

	dma_cntl_p = &nxgep->tx_cntl_pool_p->dma_buf_pool_p[channel];

	/*
	 * Allocate memory for transmit buffers and descriptor rings.
	 * Replace allocation functions with interface functions provided
	 * by the partition manager when it is available.
	 *
	 * Allocate memory for the transmit buffer pool.
	 */
	NXGE_DEBUG_MSG((nxgep, DMA_CTL,
	    "sizes: tx: %ld, cr:%ld, th:%ld",
	    sizes.tx_size, sizes.cr_size, sizes.threshhold));

	*num_chunks = 0;
	status = nxge_alloc_tx_buf_dma(nxgep, channel, dma_buf_p,
	    sizes.tx_size, sizes.threshhold, num_chunks);
	if (status != NXGE_OK) {
		cmn_err(CE_NOTE, "nxge_alloc_tx_buf_dma failed!");
		return (status);
	}

	/*
	 * Allocate memory for descriptor rings and mailbox.
	 */
	status = nxge_alloc_tx_cntl_dma(nxgep, channel, dma_cntl_p,
	    sizes.cr_size);
	if (status != NXGE_OK) {
		nxge_free_tx_buf_dma(nxgep, *dma_buf_p, *num_chunks);
		cmn_err(CE_NOTE, "nxge_alloc_tx_cntl_dma failed!");
		return (status);
	}

	return (NXGE_OK);
}

void
nxge_free_txb(
	p_nxge_t nxgep,
	int channel)
{
	nxge_dma_common_t	*data;
	nxge_dma_common_t	*control;
	uint32_t 		num_chunks;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_free_txb"));

	data = nxgep->tx_buf_pool_p->dma_buf_pool_p[channel];
	num_chunks = nxgep->tx_buf_pool_p->num_chunks[channel];
	nxge_free_tx_buf_dma(nxgep, data, num_chunks);

	nxgep->tx_buf_pool_p->dma_buf_pool_p[channel] = 0;
	nxgep->tx_buf_pool_p->num_chunks[channel] = 0;

	control = nxgep->tx_cntl_pool_p->dma_buf_pool_p[channel];
	nxge_free_tx_cntl_dma(nxgep, control);

	nxgep->tx_cntl_pool_p->dma_buf_pool_p[channel] = 0;

	KMEM_FREE(data, sizeof (nxge_dma_common_t) * NXGE_DMA_BLOCK);
	KMEM_FREE(control, sizeof (nxge_dma_common_t));

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "<== nxge_free_txb"));
}

/*
 * nxge_alloc_tx_mem_pool
 *
 *	This function allocates all of the per-port TDC control data structures.
 *	The per-channel (TDC) data structures are allocated when needed.
 *
 * Arguments:
 * 	nxgep
 *
 * Notes:
 *
 * Context:
 *	Any domain
 */
nxge_status_t
nxge_alloc_tx_mem_pool(p_nxge_t nxgep)
{
	nxge_hw_pt_cfg_t	*p_cfgp;
	nxge_dma_pool_t		*dma_poolp;
	nxge_dma_common_t	**dma_buf_p;
	nxge_dma_pool_t		*dma_cntl_poolp;
	nxge_dma_common_t	**dma_cntl_p;
	uint32_t		*num_chunks; /* per dma */
	int			tdc_max;

	NXGE_DEBUG_MSG((nxgep, MEM_CTL, "==> nxge_alloc_tx_mem_pool"));

	p_cfgp = &nxgep->pt_config.hw_config;
	tdc_max = NXGE_MAX_TDCS;

	/*
	 * Allocate memory for each transmit DMA channel.
	 */
	dma_poolp = (p_nxge_dma_pool_t)KMEM_ZALLOC(sizeof (nxge_dma_pool_t),
	    KM_SLEEP);
	dma_buf_p = (p_nxge_dma_common_t *)KMEM_ZALLOC(
	    sizeof (p_nxge_dma_common_t) * tdc_max, KM_SLEEP);

	dma_cntl_poolp = (p_nxge_dma_pool_t)
	    KMEM_ZALLOC(sizeof (nxge_dma_pool_t), KM_SLEEP);
	dma_cntl_p = (p_nxge_dma_common_t *)KMEM_ZALLOC(
	    sizeof (p_nxge_dma_common_t) * tdc_max, KM_SLEEP);

	if (nxge_tx_ring_size > TDC_DEFAULT_MAX) {
		NXGE_DEBUG_MSG((nxgep, MEM_CTL,
		    "nxge_alloc_tx_mem_pool: TDC too high %d, "
		    "set to default %d",
		    nxge_tx_ring_size, TDC_DEFAULT_MAX));
		nxge_tx_ring_size = TDC_DEFAULT_MAX;
	}

#if	defined(sun4v) && defined(NIU_LP_WORKAROUND)
	/*
	 * N2/NIU has limitation on the descriptor sizes (contiguous
	 * memory allocation on data buffers to 4M (contig_mem_alloc)
	 * and little endian for control buffers (must use the ddi/dki mem alloc
	 * function). The transmit ring is limited to 8K (includes the
	 * mailbox).
	 */
	if (nxgep->niu_type == N2_NIU) {
		if ((nxge_tx_ring_size > NXGE_NIU_CONTIG_TX_MAX) ||
		    (!ISP2(nxge_tx_ring_size))) {
			nxge_tx_ring_size = NXGE_NIU_CONTIG_TX_MAX;
		}
	}
#endif

	nxgep->nxge_port_tx_ring_size = nxge_tx_ring_size;

	num_chunks = (uint32_t *)KMEM_ZALLOC(
	    sizeof (uint32_t) * tdc_max, KM_SLEEP);

	dma_poolp->ndmas = p_cfgp->tdc.owned;
	dma_poolp->num_chunks = num_chunks;
	dma_poolp->dma_buf_pool_p = dma_buf_p;
	nxgep->tx_buf_pool_p = dma_poolp;

	dma_poolp->buf_allocated = B_TRUE;

	dma_cntl_poolp->ndmas = p_cfgp->tdc.owned;
	dma_cntl_poolp->dma_buf_pool_p = dma_cntl_p;
	nxgep->tx_cntl_pool_p = dma_cntl_poolp;

	dma_cntl_poolp->buf_allocated = B_TRUE;

	nxgep->tx_rings =
	    KMEM_ZALLOC(sizeof (tx_rings_t), KM_SLEEP);
	nxgep->tx_rings->rings =
	    KMEM_ZALLOC(sizeof (p_tx_ring_t) * tdc_max, KM_SLEEP);
	nxgep->tx_mbox_areas_p =
	    KMEM_ZALLOC(sizeof (tx_mbox_areas_t), KM_SLEEP);
	nxgep->tx_mbox_areas_p->txmbox_areas_p =
	    KMEM_ZALLOC(sizeof (p_tx_mbox_t) * tdc_max, KM_SLEEP);

	nxgep->tx_rings->ndmas = p_cfgp->tdc.owned;

	NXGE_DEBUG_MSG((nxgep, MEM_CTL,
	    "==> nxge_alloc_tx_mem_pool: ndmas %d poolp->ndmas %d",
	    tdc_max, dma_poolp->ndmas));

	return (NXGE_OK);
}

nxge_status_t
nxge_alloc_tx_buf_dma(p_nxge_t nxgep, uint16_t dma_channel,
    p_nxge_dma_common_t *dmap, size_t alloc_size,
    size_t block_size, uint32_t *num_chunks)
{
	p_nxge_dma_common_t 	tx_dmap;
	nxge_status_t		status = NXGE_OK;
	size_t			total_alloc_size;
	size_t			allocated = 0;
	int			i, size_index, array_size;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_alloc_tx_buf_dma"));

	tx_dmap = (p_nxge_dma_common_t)
	    KMEM_ZALLOC(sizeof (nxge_dma_common_t) * NXGE_DMA_BLOCK,
	    KM_SLEEP);

	total_alloc_size = alloc_size;
	i = 0;
	size_index = 0;
	array_size =  sizeof (alloc_sizes) /  sizeof (size_t);
	while ((alloc_sizes[size_index] < alloc_size) &&
	    (size_index < array_size))
		size_index++;
	if (size_index >= array_size) {
		size_index = array_size - 1;
	}

	while ((allocated < total_alloc_size) &&
	    (size_index >= 0) && (i < NXGE_DMA_BLOCK)) {

		tx_dmap[i].dma_chunk_index = i;
		tx_dmap[i].block_size = block_size;
		tx_dmap[i].alength = alloc_sizes[size_index];
		tx_dmap[i].orig_alength = tx_dmap[i].alength;
		tx_dmap[i].nblocks = alloc_sizes[size_index] / block_size;
		tx_dmap[i].dma_channel = dma_channel;
		tx_dmap[i].contig_alloc_type = B_FALSE;
		tx_dmap[i].kmem_alloc_type = B_FALSE;

		/*
		 * N2/NIU: data buffers must be contiguous as the driver
		 *	   needs to call Hypervisor api to set up
		 *	   logical pages.
		 */
		if ((nxgep->niu_type == N2_NIU) && (NXGE_DMA_BLOCK == 1)) {
			tx_dmap[i].contig_alloc_type = B_TRUE;
		}

		status = nxge_dma_mem_alloc(nxgep, nxge_force_dma,
		    &nxge_tx_dma_attr,
		    tx_dmap[i].alength,
		    &nxge_dev_buf_dma_acc_attr,
		    DDI_DMA_WRITE | DDI_DMA_STREAMING,
		    (p_nxge_dma_common_t)(&tx_dmap[i]));
		if (status != NXGE_OK) {
			size_index--;
		} else {
			i++;
			allocated += alloc_sizes[size_index];
		}
	}

	if (allocated < total_alloc_size) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "==> nxge_alloc_tx_buf_dma: not enough channel %d: "
		    "allocated 0x%x requested 0x%x",
		    dma_channel,
		    allocated, total_alloc_size));
		status = NXGE_ERROR;
		goto nxge_alloc_tx_mem_fail1;
	}

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
	    "==> nxge_alloc_tx_buf_dma: Allocated for channel %d: "
	    "allocated 0x%x requested 0x%x",
	    dma_channel,
	    allocated, total_alloc_size));

	*num_chunks = i;
	*dmap = tx_dmap;
	NXGE_DEBUG_MSG((nxgep, DMA_CTL,
	    "==> nxge_alloc_tx_buf_dma dmap 0x%016llx num chunks %d",
	    *dmap, i));
	goto nxge_alloc_tx_mem_exit;

nxge_alloc_tx_mem_fail1:
	KMEM_FREE(tx_dmap, sizeof (nxge_dma_common_t) * NXGE_DMA_BLOCK);

nxge_alloc_tx_mem_exit:
	NXGE_DEBUG_MSG((nxgep, DMA_CTL,
	    "<== nxge_alloc_tx_buf_dma status 0x%08x", status));

	return (status);
}

/*ARGSUSED*/
static void
nxge_free_tx_buf_dma(p_nxge_t nxgep, p_nxge_dma_common_t dmap,
    uint32_t num_chunks)
{
	int		i;

	NXGE_DEBUG_MSG((nxgep, MEM_CTL, "==> nxge_free_tx_buf_dma"));

	if (dmap == 0)
		return;

	for (i = 0; i < num_chunks; i++) {
		nxge_dma_mem_free(dmap++);
	}

	NXGE_DEBUG_MSG((nxgep, MEM_CTL, "<== nxge_free_tx_buf_dma"));
}

/*ARGSUSED*/
nxge_status_t
nxge_alloc_tx_cntl_dma(p_nxge_t nxgep, uint16_t dma_channel,
    p_nxge_dma_common_t *dmap, size_t size)
{
	p_nxge_dma_common_t 	tx_dmap;
	nxge_status_t		status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_alloc_tx_cntl_dma"));
	tx_dmap = (p_nxge_dma_common_t)
	    KMEM_ZALLOC(sizeof (nxge_dma_common_t), KM_SLEEP);

	tx_dmap->contig_alloc_type = B_FALSE;
	tx_dmap->kmem_alloc_type = B_FALSE;

	status = nxge_dma_mem_alloc(nxgep, nxge_force_dma,
	    &nxge_desc_dma_attr,
	    size,
	    &nxge_dev_desc_dma_acc_attr,
	    DDI_DMA_RDWR | DDI_DMA_CONSISTENT,
	    tx_dmap);
	if (status != NXGE_OK) {
		goto nxge_alloc_tx_cntl_dma_fail1;
	}

	*dmap = tx_dmap;
	goto nxge_alloc_tx_cntl_dma_exit;

nxge_alloc_tx_cntl_dma_fail1:
	KMEM_FREE(tx_dmap, sizeof (nxge_dma_common_t));

nxge_alloc_tx_cntl_dma_exit:
	NXGE_DEBUG_MSG((nxgep, DMA_CTL,
	    "<== nxge_alloc_tx_cntl_dma status 0x%08x", status));

	return (status);
}

/*ARGSUSED*/
static void
nxge_free_tx_cntl_dma(p_nxge_t nxgep, p_nxge_dma_common_t dmap)
{
	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "==> nxge_free_tx_cntl_dma"));

	if (dmap == 0)
		return;

	nxge_dma_mem_free(dmap);

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "<== nxge_free_tx_cntl_dma"));
}

/*
 * nxge_free_tx_mem_pool
 *
 *	This function frees all of the per-port TDC control data structures.
 *	The per-channel (TDC) data structures are freed when the channel
 *	is stopped.
 *
 * Arguments:
 * 	nxgep
 *
 * Notes:
 *
 * Context:
 *	Any domain
 */
static void
nxge_free_tx_mem_pool(p_nxge_t nxgep)
{
	int tdc_max = NXGE_MAX_TDCS;

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL, "==> nxge_free_tx_mem_pool"));

	if (!nxgep->tx_buf_pool_p || !nxgep->tx_buf_pool_p->buf_allocated) {
		NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
		    "<== nxge_free_tx_mem_pool "
		    "(null tx buf pool or buf not allocated"));
		return;
	}
	if (!nxgep->tx_cntl_pool_p || !nxgep->tx_cntl_pool_p->buf_allocated) {
		NXGE_DEBUG_MSG((nxgep, MEM2_CTL,
		    "<== nxge_free_tx_mem_pool "
		    "(null tx cntl buf pool or cntl buf not allocated"));
		return;
	}

	/* 1. Free the mailboxes. */
	KMEM_FREE(nxgep->tx_mbox_areas_p->txmbox_areas_p,
	    sizeof (p_tx_mbox_t) * tdc_max);
	KMEM_FREE(nxgep->tx_mbox_areas_p, sizeof (tx_mbox_areas_t));

	nxgep->tx_mbox_areas_p = 0;

	/* 2. Free the transmit ring arrays. */
	KMEM_FREE(nxgep->tx_rings->rings,
	    sizeof (p_tx_ring_t) * tdc_max);
	KMEM_FREE(nxgep->tx_rings, sizeof (tx_rings_t));

	nxgep->tx_rings = 0;

	/* 3. Free the completion ring data structures. */
	KMEM_FREE(nxgep->tx_cntl_pool_p->dma_buf_pool_p,
	    sizeof (p_nxge_dma_common_t) * tdc_max);
	KMEM_FREE(nxgep->tx_cntl_pool_p, sizeof (nxge_dma_pool_t));

	nxgep->tx_cntl_pool_p = 0;

	/* 4. Free the data ring data structures. */
	KMEM_FREE(nxgep->tx_buf_pool_p->num_chunks,
	    sizeof (uint32_t) * tdc_max);
	KMEM_FREE(nxgep->tx_buf_pool_p->dma_buf_pool_p,
	    sizeof (p_nxge_dma_common_t) * tdc_max);
	KMEM_FREE(nxgep->tx_buf_pool_p, sizeof (nxge_dma_pool_t));

	nxgep->tx_buf_pool_p = 0;

	NXGE_DEBUG_MSG((nxgep, MEM2_CTL, "<== nxge_free_tx_mem_pool"));
}

/*ARGSUSED*/
static nxge_status_t
nxge_dma_mem_alloc(p_nxge_t nxgep, dma_method_t method,
	struct ddi_dma_attr *dma_attrp,
	size_t length, ddi_device_acc_attr_t *acc_attr_p, uint_t xfer_flags,
	p_nxge_dma_common_t dma_p)
{
	caddr_t 		kaddrp;
	int			ddi_status = DDI_SUCCESS;
	boolean_t		contig_alloc_type;
	boolean_t		kmem_alloc_type;

	contig_alloc_type = dma_p->contig_alloc_type;

	if (contig_alloc_type && (nxgep->niu_type != N2_NIU)) {
		/*
		 * contig_alloc_type for contiguous memory only allowed
		 * for N2/NIU.
		 */
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "nxge_dma_mem_alloc: alloc type not allowed (%d)",
		    dma_p->contig_alloc_type));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	dma_p->dma_handle = NULL;
	dma_p->acc_handle = NULL;
	dma_p->kaddrp = dma_p->last_kaddrp = NULL;
	dma_p->first_ioaddr_pp = dma_p->last_ioaddr_pp = NULL;
	ddi_status = ddi_dma_alloc_handle(nxgep->dip, dma_attrp,
	    DDI_DMA_DONTWAIT, NULL, &dma_p->dma_handle);
	if (ddi_status != DDI_SUCCESS) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "nxge_dma_mem_alloc:ddi_dma_alloc_handle failed."));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	kmem_alloc_type = dma_p->kmem_alloc_type;

	switch (contig_alloc_type) {
	case B_FALSE:
		switch (kmem_alloc_type) {
		case B_FALSE:
			ddi_status = ddi_dma_mem_alloc(dma_p->dma_handle,
			    length,
			    acc_attr_p,
			    xfer_flags,
			    DDI_DMA_DONTWAIT, 0, &kaddrp, &dma_p->alength,
			    &dma_p->acc_handle);
			if (ddi_status != DDI_SUCCESS) {
				NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				    "nxge_dma_mem_alloc: "
				    "ddi_dma_mem_alloc failed"));
				ddi_dma_free_handle(&dma_p->dma_handle);
				dma_p->dma_handle = NULL;
				return (NXGE_ERROR | NXGE_DDI_FAILED);
			}
			if (dma_p->alength < length) {
				NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				    "nxge_dma_mem_alloc:di_dma_mem_alloc "
				    "< length."));
				ddi_dma_mem_free(&dma_p->acc_handle);
				ddi_dma_free_handle(&dma_p->dma_handle);
				dma_p->acc_handle = NULL;
				dma_p->dma_handle = NULL;
				return (NXGE_ERROR);
			}

			ddi_status = ddi_dma_addr_bind_handle(dma_p->dma_handle,
			    NULL,
			    kaddrp, dma_p->alength, xfer_flags,
			    DDI_DMA_DONTWAIT,
			    0, &dma_p->dma_cookie, &dma_p->ncookies);
			if (ddi_status != DDI_DMA_MAPPED) {
				NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				    "nxge_dma_mem_alloc: ddi_dma_addr_bind "
				    "failed "
				    "(staus 0x%x ncookies %d.)", ddi_status,
				    dma_p->ncookies));
				if (dma_p->acc_handle) {
					ddi_dma_mem_free(&dma_p->acc_handle);
					dma_p->acc_handle = NULL;
				}
				ddi_dma_free_handle(&dma_p->dma_handle);
				dma_p->dma_handle = NULL;
				return (NXGE_ERROR | NXGE_DDI_FAILED);
			}

			if (dma_p->ncookies != 1) {
				NXGE_DEBUG_MSG((nxgep, DMA_CTL,
				    "nxge_dma_mem_alloc:ddi_dma_addr_bind "
				    "> 1 cookie"
				    "(staus 0x%x ncookies %d.)", ddi_status,
				    dma_p->ncookies));
				if (dma_p->acc_handle) {
					ddi_dma_mem_free(&dma_p->acc_handle);
					dma_p->acc_handle = NULL;
				}
				(void) ddi_dma_unbind_handle(dma_p->dma_handle);
				ddi_dma_free_handle(&dma_p->dma_handle);
				dma_p->dma_handle = NULL;
				return (NXGE_ERROR);
			}
			break;

		case B_TRUE:
			kaddrp = KMEM_ALLOC(length, KM_NOSLEEP);
			if (kaddrp == NULL) {
				NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				    "nxge_dma_mem_alloc:ddi_dma_mem_alloc "
				    "kmem alloc failed"));
				return (NXGE_ERROR);
			}

			dma_p->alength = length;
			ddi_status = ddi_dma_addr_bind_handle(dma_p->dma_handle,
			    NULL, kaddrp, dma_p->alength, xfer_flags,
			    DDI_DMA_DONTWAIT, 0,
			    &dma_p->dma_cookie, &dma_p->ncookies);
			if (ddi_status != DDI_DMA_MAPPED) {
				NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				    "nxge_dma_mem_alloc:ddi_dma_addr_bind: "
				    "(kmem_alloc) failed kaddrp $%p length %d "
				    "(staus 0x%x (%d) ncookies %d.)",
				    kaddrp, length,
				    ddi_status, ddi_status, dma_p->ncookies));
				KMEM_FREE(kaddrp, length);
				dma_p->acc_handle = NULL;
				ddi_dma_free_handle(&dma_p->dma_handle);
				dma_p->dma_handle = NULL;
				dma_p->kaddrp = NULL;
				return (NXGE_ERROR | NXGE_DDI_FAILED);
			}

			if (dma_p->ncookies != 1) {
				NXGE_DEBUG_MSG((nxgep, DMA_CTL,
				    "nxge_dma_mem_alloc:ddi_dma_addr_bind "
				    "(kmem_alloc) > 1 cookie"
				    "(staus 0x%x ncookies %d.)", ddi_status,
				    dma_p->ncookies));
				KMEM_FREE(kaddrp, length);
				dma_p->acc_handle = NULL;
				(void) ddi_dma_unbind_handle(dma_p->dma_handle);
				ddi_dma_free_handle(&dma_p->dma_handle);
				dma_p->dma_handle = NULL;
				dma_p->kaddrp = NULL;
				return (NXGE_ERROR);
			}

			dma_p->kaddrp = kaddrp;

			NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL,
			    "nxge_dma_mem_alloc: kmem_alloc dmap $%p "
			    "kaddr $%p alength %d",
			    dma_p,
			    kaddrp,
			    dma_p->alength));
			break;
		}
		break;

#if	defined(sun4v) && defined(NIU_LP_WORKAROUND)
	case B_TRUE:
		kaddrp = (caddr_t)contig_mem_alloc(length);
		if (kaddrp == NULL) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "nxge_dma_mem_alloc:contig_mem_alloc failed."));
			ddi_dma_free_handle(&dma_p->dma_handle);
			return (NXGE_ERROR | NXGE_DDI_FAILED);
		}

		dma_p->alength = length;
		ddi_status = ddi_dma_addr_bind_handle(dma_p->dma_handle, NULL,
		    kaddrp, dma_p->alength, xfer_flags, DDI_DMA_DONTWAIT, 0,
		    &dma_p->dma_cookie, &dma_p->ncookies);
		if (ddi_status != DDI_DMA_MAPPED) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "nxge_dma_mem_alloc:di_dma_addr_bind failed "
			    "(status 0x%x ncookies %d.)", ddi_status,
			    dma_p->ncookies));

			NXGE_DEBUG_MSG((nxgep, DMA_CTL,
			    "==> nxge_dma_mem_alloc: (not mapped)"
			    "length %lu (0x%x) "
			    "free contig kaddrp $%p "
			    "va_to_pa $%p",
			    length, length,
			    kaddrp,
			    va_to_pa(kaddrp)));


			contig_mem_free((void *)kaddrp, length);
			ddi_dma_free_handle(&dma_p->dma_handle);

			dma_p->dma_handle = NULL;
			dma_p->acc_handle = NULL;
			dma_p->alength = NULL;
			dma_p->kaddrp = NULL;

			return (NXGE_ERROR | NXGE_DDI_FAILED);
		}

		if (dma_p->ncookies != 1 ||
		    (dma_p->dma_cookie.dmac_laddress == NULL)) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "nxge_dma_mem_alloc:di_dma_addr_bind > 1 "
			    "cookie or "
			    "dmac_laddress is NULL $%p size %d "
			    " (status 0x%x ncookies %d.)",
			    ddi_status,
			    dma_p->dma_cookie.dmac_laddress,
			    dma_p->dma_cookie.dmac_size,
			    dma_p->ncookies));

			contig_mem_free((void *)kaddrp, length);
			(void) ddi_dma_unbind_handle(dma_p->dma_handle);
			ddi_dma_free_handle(&dma_p->dma_handle);

			dma_p->alength = 0;
			dma_p->dma_handle = NULL;
			dma_p->acc_handle = NULL;
			dma_p->kaddrp = NULL;

			return (NXGE_ERROR | NXGE_DDI_FAILED);
		}
		break;

#else
	case B_TRUE:
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "nxge_dma_mem_alloc: invalid alloc type for !sun4v"));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
#endif
	}

	dma_p->kaddrp = kaddrp;
	dma_p->last_kaddrp = (unsigned char *)kaddrp +
	    dma_p->alength - RXBUF_64B_ALIGNED;
#if defined(__i386)
	dma_p->ioaddr_pp =
	    (unsigned char *)(uint32_t)dma_p->dma_cookie.dmac_laddress;
#else
	dma_p->ioaddr_pp = (unsigned char *)dma_p->dma_cookie.dmac_laddress;
#endif
	dma_p->last_ioaddr_pp =
#if defined(__i386)
	    (unsigned char *)(uint32_t)dma_p->dma_cookie.dmac_laddress +
#else
	    (unsigned char *)dma_p->dma_cookie.dmac_laddress +
#endif
	    dma_p->alength - RXBUF_64B_ALIGNED;

	NPI_DMA_ACC_HANDLE_SET(dma_p, dma_p->acc_handle);

#if	defined(sun4v) && defined(NIU_LP_WORKAROUND)
	dma_p->orig_ioaddr_pp =
	    (unsigned char *)dma_p->dma_cookie.dmac_laddress;
	dma_p->orig_alength = length;
	dma_p->orig_kaddrp = kaddrp;
	dma_p->orig_vatopa = (uint64_t)va_to_pa(kaddrp);
#endif

	NXGE_DEBUG_MSG((nxgep, DMA_CTL, "<== nxge_dma_mem_alloc: "
	    "dma buffer allocated: dma_p $%p "
	    "return dmac_ladress from cookie $%p cookie dmac_size %d "
	    "dma_p->ioaddr_p $%p "
	    "dma_p->orig_ioaddr_p $%p "
	    "orig_vatopa $%p "
	    "alength %d (0x%x) "
	    "kaddrp $%p "
	    "length %d (0x%x)",
	    dma_p,
	    dma_p->dma_cookie.dmac_laddress, dma_p->dma_cookie.dmac_size,
	    dma_p->ioaddr_pp,
	    dma_p->orig_ioaddr_pp,
	    dma_p->orig_vatopa,
	    dma_p->alength, dma_p->alength,
	    kaddrp,
	    length, length));

	return (NXGE_OK);
}

static void
nxge_dma_mem_free(p_nxge_dma_common_t dma_p)
{
	if (dma_p->dma_handle != NULL) {
		if (dma_p->ncookies) {
			(void) ddi_dma_unbind_handle(dma_p->dma_handle);
			dma_p->ncookies = 0;
		}
		ddi_dma_free_handle(&dma_p->dma_handle);
		dma_p->dma_handle = NULL;
	}

	if (dma_p->acc_handle != NULL) {
		ddi_dma_mem_free(&dma_p->acc_handle);
		dma_p->acc_handle = NULL;
		NPI_DMA_ACC_HANDLE_SET(dma_p, NULL);
	}

#if	defined(sun4v) && defined(NIU_LP_WORKAROUND)
	if (dma_p->contig_alloc_type &&
	    dma_p->orig_kaddrp && dma_p->orig_alength) {
		NXGE_DEBUG_MSG((NULL, DMA_CTL, "nxge_dma_mem_free: "
		    "kaddrp $%p (orig_kaddrp $%p)"
		    "mem type %d ",
		    "orig_alength %d "
		    "alength 0x%x (%d)",
		    dma_p->kaddrp,
		    dma_p->orig_kaddrp,
		    dma_p->contig_alloc_type,
		    dma_p->orig_alength,
		    dma_p->alength, dma_p->alength));

		contig_mem_free(dma_p->orig_kaddrp, dma_p->orig_alength);
		dma_p->orig_alength = NULL;
		dma_p->orig_kaddrp = NULL;
		dma_p->contig_alloc_type = B_FALSE;
	}
#endif
	dma_p->kaddrp = NULL;
	dma_p->alength = NULL;
}

static void
nxge_dma_free_rx_data_buf(p_nxge_dma_common_t dma_p)
{
	uint64_t kaddr;
	uint32_t buf_size;

	NXGE_DEBUG_MSG((NULL, DMA_CTL, "==> nxge_dma_free_rx_data_buf"));

	if (dma_p->dma_handle != NULL) {
		if (dma_p->ncookies) {
			(void) ddi_dma_unbind_handle(dma_p->dma_handle);
			dma_p->ncookies = 0;
		}
		ddi_dma_free_handle(&dma_p->dma_handle);
		dma_p->dma_handle = NULL;
	}

	if (dma_p->acc_handle != NULL) {
		ddi_dma_mem_free(&dma_p->acc_handle);
		dma_p->acc_handle = NULL;
		NPI_DMA_ACC_HANDLE_SET(dma_p, NULL);
	}

	NXGE_DEBUG_MSG((NULL, DMA_CTL,
	    "==> nxge_dma_free_rx_data_buf: dmap $%p buf_alloc_state %d",
	    dma_p,
	    dma_p->buf_alloc_state));

	if (!(dma_p->buf_alloc_state & BUF_ALLOCATED_WAIT_FREE)) {
		NXGE_DEBUG_MSG((NULL, DMA_CTL,
		    "<== nxge_dma_free_rx_data_buf: "
		    "outstanding data buffers"));
		return;
	}

#if	defined(sun4v) && defined(NIU_LP_WORKAROUND)
	if (dma_p->contig_alloc_type &&
	    dma_p->orig_kaddrp && dma_p->orig_alength) {
		NXGE_DEBUG_MSG((NULL, DMA_CTL, "nxge_dma_free_rx_data_buf: "
		    "kaddrp $%p (orig_kaddrp $%p)"
		    "mem type %d ",
		    "orig_alength %d "
		    "alength 0x%x (%d)",
		    dma_p->kaddrp,
		    dma_p->orig_kaddrp,
		    dma_p->contig_alloc_type,
		    dma_p->orig_alength,
		    dma_p->alength, dma_p->alength));

		kaddr = (uint64_t)dma_p->orig_kaddrp;
		buf_size = dma_p->orig_alength;
		nxge_free_buf(CONTIG_MEM_ALLOC, kaddr, buf_size);
		dma_p->orig_alength = NULL;
		dma_p->orig_kaddrp = NULL;
		dma_p->contig_alloc_type = B_FALSE;
		dma_p->kaddrp = NULL;
		dma_p->alength = NULL;
		return;
	}
#endif

	if (dma_p->kmem_alloc_type) {
		NXGE_DEBUG_MSG((NULL, DMA_CTL,
		    "nxge_dma_free_rx_data_buf: free kmem "
		    "kaddrp $%p (orig_kaddrp $%p)"
		    "alloc type %d "
		    "orig_alength %d "
		    "alength 0x%x (%d)",
		    dma_p->kaddrp,
		    dma_p->orig_kaddrp,
		    dma_p->kmem_alloc_type,
		    dma_p->orig_alength,
		    dma_p->alength, dma_p->alength));
#if defined(__i386)
		kaddr = (uint64_t)(uint32_t)dma_p->kaddrp;
#else
		kaddr = (uint64_t)dma_p->kaddrp;
#endif
		buf_size = dma_p->orig_alength;
		NXGE_DEBUG_MSG((NULL, DMA_CTL,
		    "nxge_dma_free_rx_data_buf: free dmap $%p "
		    "kaddr $%p buf_size %d",
		    dma_p,
		    kaddr, buf_size));
		nxge_free_buf(KMEM_ALLOC, kaddr, buf_size);
		dma_p->alength = 0;
		dma_p->orig_alength = 0;
		dma_p->kaddrp = NULL;
		dma_p->kmem_alloc_type = B_FALSE;
	}

	NXGE_DEBUG_MSG((NULL, DMA_CTL, "<== nxge_dma_free_rx_data_buf"));
}

/*
 *	nxge_m_start() -- start transmitting and receiving.
 *
 *	This function is called by the MAC layer when the first
 *	stream is open to prepare the hardware ready for sending
 *	and transmitting packets.
 */
static int
nxge_m_start(void *arg)
{
	p_nxge_t 	nxgep = (p_nxge_t)arg;

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "==> nxge_m_start"));

	if (nxge_peu_reset_enable && !nxgep->nxge_link_poll_timerid) {
		(void) nxge_link_monitor(nxgep, LINK_MONITOR_START);
	}

	MUTEX_ENTER(nxgep->genlock);
	if (nxge_init(nxgep) != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "<== nxge_m_start: initialization failed"));
		MUTEX_EXIT(nxgep->genlock);
		return (EIO);
	}

	if (nxgep->nxge_mac_state == NXGE_MAC_STARTED)
		goto nxge_m_start_exit;
	/*
	 * Start timer to check the system error and tx hangs
	 */
	if (!isLDOMguest(nxgep))
		nxgep->nxge_timerid = nxge_start_timer(nxgep,
		    nxge_check_hw_state, NXGE_CHECK_TIMER);
#if	defined(sun4v)
	else
		nxge_hio_start_timer(nxgep);
#endif

	nxgep->link_notify = B_TRUE;

	nxgep->nxge_mac_state = NXGE_MAC_STARTED;

nxge_m_start_exit:
	MUTEX_EXIT(nxgep->genlock);
	NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "<== nxge_m_start"));
	return (0);
}

/*
 *	nxge_m_stop(): stop transmitting and receiving.
 */
static void
nxge_m_stop(void *arg)
{
	p_nxge_t 	nxgep = (p_nxge_t)arg;

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "==> nxge_m_stop"));

	if (nxgep->nxge_timerid) {
		nxge_stop_timer(nxgep, nxgep->nxge_timerid);
		nxgep->nxge_timerid = 0;
	}

	MUTEX_ENTER(nxgep->genlock);
	nxgep->nxge_mac_state = NXGE_MAC_STOPPING;
	nxge_uninit(nxgep);

	nxgep->nxge_mac_state = NXGE_MAC_STOPPED;

	MUTEX_EXIT(nxgep->genlock);

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "<== nxge_m_stop"));
}

static int
nxge_m_unicst(void *arg, const uint8_t *macaddr)
{
	p_nxge_t 	nxgep = (p_nxge_t)arg;
	struct 		ether_addr addrp;

	NXGE_DEBUG_MSG((nxgep, MAC_CTL, "==> nxge_m_unicst"));

	bcopy(macaddr, (uint8_t *)&addrp, ETHERADDRL);
	if (nxge_set_mac_addr(nxgep, &addrp)) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "<== nxge_m_unicst: set unitcast failed"));
		return (EINVAL);
	}

	NXGE_DEBUG_MSG((nxgep, MAC_CTL, "<== nxge_m_unicst"));

	return (0);
}

static int
nxge_m_multicst(void *arg, boolean_t add, const uint8_t *mca)
{
	p_nxge_t 	nxgep = (p_nxge_t)arg;
	struct 		ether_addr addrp;

	NXGE_DEBUG_MSG((nxgep, MAC_CTL,
	    "==> nxge_m_multicst: add %d", add));

	bcopy(mca, (uint8_t *)&addrp, ETHERADDRL);
	if (add) {
		if (nxge_add_mcast_addr(nxgep, &addrp)) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "<== nxge_m_multicst: add multicast failed"));
			return (EINVAL);
		}
	} else {
		if (nxge_del_mcast_addr(nxgep, &addrp)) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "<== nxge_m_multicst: del multicast failed"));
			return (EINVAL);
		}
	}

	NXGE_DEBUG_MSG((nxgep, MAC_CTL, "<== nxge_m_multicst"));

	return (0);
}

static int
nxge_m_promisc(void *arg, boolean_t on)
{
	p_nxge_t 	nxgep = (p_nxge_t)arg;

	NXGE_DEBUG_MSG((nxgep, MAC_CTL,
	    "==> nxge_m_promisc: on %d", on));

	if (nxge_set_promisc(nxgep, on)) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "<== nxge_m_promisc: set promisc failed"));
		return (EINVAL);
	}

	NXGE_DEBUG_MSG((nxgep, MAC_CTL,
	    "<== nxge_m_promisc: on %d", on));

	return (0);
}

static void
nxge_m_ioctl(void *arg,  queue_t *wq, mblk_t *mp)
{
	p_nxge_t 	nxgep = (p_nxge_t)arg;
	struct 		iocblk *iocp;
	boolean_t 	need_privilege;
	int 		err;
	int 		cmd;

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "==> nxge_m_ioctl"));

	iocp = (struct iocblk *)mp->b_rptr;
	iocp->ioc_error = 0;
	need_privilege = B_TRUE;
	cmd = iocp->ioc_cmd;
	NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "==> nxge_m_ioctl: cmd 0x%08x", cmd));
	switch (cmd) {
	default:
		miocnak(wq, mp, 0, EINVAL);
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "<== nxge_m_ioctl: invalid"));
		return;

	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
	case LB_GET_MODE:
		need_privilege = B_FALSE;
		break;
	case LB_SET_MODE:
		break;


	case NXGE_GET_MII:
	case NXGE_PUT_MII:
	case NXGE_GET64:
	case NXGE_PUT64:
	case NXGE_GET_TX_RING_SZ:
	case NXGE_GET_TX_DESC:
	case NXGE_TX_SIDE_RESET:
	case NXGE_RX_SIDE_RESET:
	case NXGE_GLOBAL_RESET:
	case NXGE_RESET_MAC:
	case NXGE_TX_REGS_DUMP:
	case NXGE_RX_REGS_DUMP:
	case NXGE_INT_REGS_DUMP:
	case NXGE_VIR_INT_REGS_DUMP:
	case NXGE_PUT_TCAM:
	case NXGE_GET_TCAM:
	case NXGE_RTRACE:
	case NXGE_RDUMP:

		need_privilege = B_FALSE;
		break;
	case NXGE_INJECT_ERR:
		cmn_err(CE_NOTE, "!nxge_m_ioctl: Inject error\n");
		nxge_err_inject(nxgep, wq, mp);
		break;
	}

	if (need_privilege) {
		err = secpolicy_net_config(iocp->ioc_cr, B_FALSE);
		if (err != 0) {
			miocnak(wq, mp, 0, err);
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "<== nxge_m_ioctl: no priv"));
			return;
		}
	}

	switch (cmd) {

	case LB_GET_MODE:
	case LB_SET_MODE:
	case LB_GET_INFO_SIZE:
	case LB_GET_INFO:
		nxge_loopback_ioctl(nxgep, wq, mp, iocp);
		break;

	case NXGE_GET_MII:
	case NXGE_PUT_MII:
	case NXGE_PUT_TCAM:
	case NXGE_GET_TCAM:
	case NXGE_GET64:
	case NXGE_PUT64:
	case NXGE_GET_TX_RING_SZ:
	case NXGE_GET_TX_DESC:
	case NXGE_TX_SIDE_RESET:
	case NXGE_RX_SIDE_RESET:
	case NXGE_GLOBAL_RESET:
	case NXGE_RESET_MAC:
	case NXGE_TX_REGS_DUMP:
	case NXGE_RX_REGS_DUMP:
	case NXGE_INT_REGS_DUMP:
	case NXGE_VIR_INT_REGS_DUMP:
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_m_ioctl: cmd 0x%x", cmd));
		nxge_hw_ioctl(nxgep, wq, mp, iocp);
		break;
	}

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "<== nxge_m_ioctl"));
}

extern void nxge_rx_hw_blank(void *arg, time_t ticks, uint_t count);

static void
nxge_m_resources(void *arg)
{
	p_nxge_t		nxgep = arg;
	mac_rx_fifo_t 		mrf;

	nxge_grp_set_t		*set = &nxgep->rx_set;
	uint8_t			rdc;

	rx_rcr_ring_t		*ring;

	NXGE_DEBUG_MSG((nxgep, RX_CTL, "==> nxge_m_resources"));

	MUTEX_ENTER(nxgep->genlock);

	if (set->owned.map == 0) {
		NXGE_ERROR_MSG((NULL, NXGE_ERR_CTL,
		    "nxge_m_resources: no receive resources"));
		goto nxge_m_resources_exit;
	}

	/*
	 * CR 6492541 Check to see if the drv_state has been initialized,
	 * if not * call nxge_init().
	 */
	if (!(nxgep->drv_state & STATE_HW_INITIALIZED)) {
		if (nxge_init(nxgep) != NXGE_OK)
			goto nxge_m_resources_exit;
	}

	mrf.mrf_type = MAC_RX_FIFO;
	mrf.mrf_blank = nxge_rx_hw_blank;
	mrf.mrf_arg = (void *)nxgep;

	mrf.mrf_normal_blank_time = 128;
	mrf.mrf_normal_pkt_count = 8;

	/*
	 * Export our receive resources to the MAC layer.
	 */
	for (rdc = 0; rdc < NXGE_MAX_RDCS; rdc++) {
		if ((1 << rdc) & set->owned.map) {
			ring = nxgep->rx_rcr_rings->rcr_rings[rdc];
			if (ring == 0) {
				/*
				 * This is a big deal only if we are
				 * *not* in an LDOMs environment.
				 */
				if (nxgep->environs == SOLARIS_DOMAIN) {
					cmn_err(CE_NOTE,
					    "==> nxge_m_resources: "
					    "ring %d == 0", rdc);
				}
				continue;
			}
			ring->rcr_mac_handle = mac_resource_add
			    (nxgep->mach, (mac_resource_t *)&mrf);

			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "==> nxge_m_resources: RDC %d RCR %p MAC handle %p",
			    rdc, ring, ring->rcr_mac_handle));
		}
	}

nxge_m_resources_exit:
	MUTEX_EXIT(nxgep->genlock);
	NXGE_DEBUG_MSG((nxgep, RX_CTL, "<== nxge_m_resources"));
}

void
nxge_mmac_kstat_update(p_nxge_t nxgep, mac_addr_slot_t slot, boolean_t factory)
{
	p_nxge_mmac_stats_t mmac_stats;
	int i;
	nxge_mmac_t *mmac_info;

	mmac_info = &nxgep->nxge_mmac_info;

	mmac_stats = &nxgep->statsp->mmac_stats;
	mmac_stats->mmac_max_cnt = mmac_info->num_mmac;
	mmac_stats->mmac_avail_cnt = mmac_info->naddrfree;

	for (i = 0; i < ETHERADDRL; i++) {
		if (factory) {
			mmac_stats->mmac_avail_pool[slot-1].ether_addr_octet[i]
			    = mmac_info->factory_mac_pool[slot][
			    (ETHERADDRL-1) - i];
		} else {
			mmac_stats->mmac_avail_pool[slot-1].ether_addr_octet[i]
			    = mmac_info->mac_pool[slot].addr[
			    (ETHERADDRL - 1) - i];
		}
	}
}

/*
 * nxge_altmac_set() -- Set an alternate MAC address
 */
static int
nxge_altmac_set(p_nxge_t nxgep, uint8_t *maddr, mac_addr_slot_t slot)
{
	uint8_t addrn;
	uint8_t portn;
	npi_mac_addr_t altmac;
	hostinfo_t mac_rdc;
	p_nxge_class_pt_cfg_t clscfgp;

	altmac.w2 = ((uint16_t)maddr[0] << 8) | ((uint16_t)maddr[1] & 0x0ff);
	altmac.w1 = ((uint16_t)maddr[2] << 8) | ((uint16_t)maddr[3] & 0x0ff);
	altmac.w0 = ((uint16_t)maddr[4] << 8) | ((uint16_t)maddr[5] & 0x0ff);

	portn = nxgep->mac.portnum;
	addrn = (uint8_t)slot - 1;

	if (npi_mac_altaddr_entry(nxgep->npi_handle, OP_SET, portn,
	    addrn, &altmac) != NPI_SUCCESS)
		return (EIO);

	/*
	 * Set the rdc table number for the host info entry
	 * for this mac address slot.
	 */
	clscfgp = (p_nxge_class_pt_cfg_t)&nxgep->class_config;
	mac_rdc.value = 0;
	mac_rdc.bits.w0.rdc_tbl_num = clscfgp->mac_host_info[addrn].rdctbl;
	mac_rdc.bits.w0.mac_pref = clscfgp->mac_host_info[addrn].mpr_npr;

	if (npi_mac_hostinfo_entry(nxgep->npi_handle, OP_SET,
	    nxgep->function_num, addrn, &mac_rdc) != NPI_SUCCESS) {
		return (EIO);
	}

	/*
	 * Enable comparison with the alternate MAC address.
	 * While the first alternate addr is enabled by bit 1 of register
	 * BMAC_ALTAD_CMPEN, it is enabled by bit 0 of register
	 * XMAC_ADDR_CMPEN, so slot needs to be converted to addrn
	 * accordingly before calling npi_mac_altaddr_entry.
	 */
	if (portn == XMAC_PORT_0 || portn == XMAC_PORT_1)
		addrn = (uint8_t)slot - 1;
	else
		addrn = (uint8_t)slot;

	if (npi_mac_altaddr_enable(nxgep->npi_handle, portn, addrn)
	    != NPI_SUCCESS)
		return (EIO);

	return (0);
}

/*
 * nxeg_m_mmac_add() - find an unused address slot, set the address
 * value to the one specified, enable the port to start filtering on
 * the new MAC address.  Returns 0 on success.
 */
int
nxge_m_mmac_add(void *arg, mac_multi_addr_t *maddr)
{
	p_nxge_t nxgep = arg;
	mac_addr_slot_t slot;
	nxge_mmac_t *mmac_info;
	int err;
	nxge_status_t status;

	mutex_enter(nxgep->genlock);

	/*
	 * Make sure that nxge is initialized, if _start() has
	 * not been called.
	 */
	if (!(nxgep->drv_state & STATE_HW_INITIALIZED)) {
		status = nxge_init(nxgep);
		if (status != NXGE_OK) {
			mutex_exit(nxgep->genlock);
			return (ENXIO);
		}
	}

	mmac_info = &nxgep->nxge_mmac_info;
	if (mmac_info->naddrfree == 0) {
		mutex_exit(nxgep->genlock);
		return (ENOSPC);
	}
	if (!mac_unicst_verify(nxgep->mach, maddr->mma_addr,
	    maddr->mma_addrlen)) {
		mutex_exit(nxgep->genlock);
		return (EINVAL);
	}
	/*
	 * 	Search for the first available slot. Because naddrfree
	 * is not zero, we are guaranteed to find one.
	 * 	Slot 0 is for unique (primary) MAC. The first alternate
	 * MAC slot is slot 1.
	 *	Each of the first two ports of Neptune has 16 alternate
	 * MAC slots but only the first 7 (of 15) slots have assigned factory
	 * MAC addresses. We first search among the slots without bundled
	 * factory MACs. If we fail to find one in that range, then we
	 * search the slots with bundled factory MACs.  A factory MAC
	 * will be wasted while the slot is used with a user MAC address.
	 * But the slot could be used by factory MAC again after calling
	 * nxge_m_mmac_remove and nxge_m_mmac_reserve.
	 */
	if (mmac_info->num_factory_mmac < mmac_info->num_mmac) {
		for (slot = mmac_info->num_factory_mmac + 1;
		    slot <= mmac_info->num_mmac; slot++) {
			if (!(mmac_info->mac_pool[slot].flags & MMAC_SLOT_USED))
				break;
		}
		if (slot > mmac_info->num_mmac) {
			for (slot = 1; slot <= mmac_info->num_factory_mmac;
			    slot++) {
				if (!(mmac_info->mac_pool[slot].flags
				    & MMAC_SLOT_USED))
					break;
			}
		}
	} else {
		for (slot = 1; slot <= mmac_info->num_mmac; slot++) {
			if (!(mmac_info->mac_pool[slot].flags & MMAC_SLOT_USED))
				break;
		}
	}
	ASSERT(slot <= mmac_info->num_mmac);
	if ((err = nxge_altmac_set(nxgep, maddr->mma_addr, slot)) != 0) {
		mutex_exit(nxgep->genlock);
		return (err);
	}
	bcopy(maddr->mma_addr, mmac_info->mac_pool[slot].addr, ETHERADDRL);
	mmac_info->mac_pool[slot].flags |= MMAC_SLOT_USED;
	mmac_info->mac_pool[slot].flags &= ~MMAC_VENDOR_ADDR;
	mmac_info->naddrfree--;
	nxge_mmac_kstat_update(nxgep, slot, B_FALSE);

	maddr->mma_slot = slot;

	mutex_exit(nxgep->genlock);
	return (0);
}

/*
 * This function reserves an unused slot and programs the slot and the HW
 * with a factory mac address.
 */
static int
nxge_m_mmac_reserve(void *arg, mac_multi_addr_t *maddr)
{
	p_nxge_t nxgep = arg;
	mac_addr_slot_t slot;
	nxge_mmac_t *mmac_info;
	int err;
	nxge_status_t status;

	mutex_enter(nxgep->genlock);

	/*
	 * Make sure that nxge is initialized, if _start() has
	 * not been called.
	 */
	if (!(nxgep->drv_state & STATE_HW_INITIALIZED)) {
		status = nxge_init(nxgep);
		if (status != NXGE_OK) {
			mutex_exit(nxgep->genlock);
			return (ENXIO);
		}
	}

	mmac_info = &nxgep->nxge_mmac_info;
	if (mmac_info->naddrfree == 0) {
		mutex_exit(nxgep->genlock);
		return (ENOSPC);
	}

	slot = maddr->mma_slot;
	if (slot == -1) {  /* -1: Take the first available slot */
		for (slot = 1; slot <= mmac_info->num_factory_mmac; slot++) {
			if (!(mmac_info->mac_pool[slot].flags & MMAC_SLOT_USED))
				break;
		}
		if (slot > mmac_info->num_factory_mmac) {
			mutex_exit(nxgep->genlock);
			return (ENOSPC);
		}
	}
	if (slot < 1 || slot > mmac_info->num_factory_mmac) {
		/*
		 * Do not support factory MAC at a slot greater than
		 * num_factory_mmac even when there are available factory
		 * MAC addresses because the alternate MACs are bundled with
		 * slot[1] through slot[num_factory_mmac]
		 */
		mutex_exit(nxgep->genlock);
		return (EINVAL);
	}
	if (mmac_info->mac_pool[slot].flags & MMAC_SLOT_USED) {
		mutex_exit(nxgep->genlock);
		return (EBUSY);
	}
	/* Verify the address to be reserved */
	if (!mac_unicst_verify(nxgep->mach,
	    mmac_info->factory_mac_pool[slot], ETHERADDRL)) {
		mutex_exit(nxgep->genlock);
		return (EINVAL);
	}
	if (err = nxge_altmac_set(nxgep,
	    mmac_info->factory_mac_pool[slot], slot)) {
		mutex_exit(nxgep->genlock);
		return (err);
	}
	bcopy(mmac_info->factory_mac_pool[slot], maddr->mma_addr, ETHERADDRL);
	mmac_info->mac_pool[slot].flags |= MMAC_SLOT_USED | MMAC_VENDOR_ADDR;
	mmac_info->naddrfree--;

	nxge_mmac_kstat_update(nxgep, slot, B_TRUE);
	mutex_exit(nxgep->genlock);

	/* Pass info back to the caller */
	maddr->mma_slot = slot;
	maddr->mma_addrlen = ETHERADDRL;
	maddr->mma_flags = MMAC_SLOT_USED | MMAC_VENDOR_ADDR;

	return (0);
}

/*
 * Remove the specified mac address and update the HW not to filter
 * the mac address anymore.
 */
int
nxge_m_mmac_remove(void *arg, mac_addr_slot_t slot)
{
	p_nxge_t nxgep = arg;
	nxge_mmac_t *mmac_info;
	uint8_t addrn;
	uint8_t portn;
	int err = 0;
	nxge_status_t status;

	mutex_enter(nxgep->genlock);

	/*
	 * Make sure that nxge is initialized, if _start() has
	 * not been called.
	 */
	if (!(nxgep->drv_state & STATE_HW_INITIALIZED)) {
		status = nxge_init(nxgep);
		if (status != NXGE_OK) {
			mutex_exit(nxgep->genlock);
			return (ENXIO);
		}
	}

	mmac_info = &nxgep->nxge_mmac_info;
	if (slot < 1 || slot > mmac_info->num_mmac) {
		mutex_exit(nxgep->genlock);
		return (EINVAL);
	}

	portn = nxgep->mac.portnum;
	if (portn == XMAC_PORT_0 || portn == XMAC_PORT_1)
		addrn = (uint8_t)slot - 1;
	else
		addrn = (uint8_t)slot;

	if (mmac_info->mac_pool[slot].flags & MMAC_SLOT_USED) {
		if (npi_mac_altaddr_disable(nxgep->npi_handle, portn, addrn)
		    == NPI_SUCCESS) {
			mmac_info->naddrfree++;
			mmac_info->mac_pool[slot].flags &= ~MMAC_SLOT_USED;
			/*
			 * Regardless if the MAC we just stopped filtering
			 * is a user addr or a facory addr, we must set
			 * the MMAC_VENDOR_ADDR flag if this slot has an
			 * associated factory MAC to indicate that a factory
			 * MAC is available.
			 */
			if (slot <= mmac_info->num_factory_mmac) {
				mmac_info->mac_pool[slot].flags
				    |= MMAC_VENDOR_ADDR;
			}
			/*
			 * Clear mac_pool[slot].addr so that kstat shows 0
			 * alternate MAC address if the slot is not used.
			 * (But nxge_m_mmac_get returns the factory MAC even
			 * when the slot is not used!)
			 */
			bzero(mmac_info->mac_pool[slot].addr, ETHERADDRL);
			nxge_mmac_kstat_update(nxgep, slot, B_FALSE);
		} else {
			err = EIO;
		}
	} else {
		err = EINVAL;
	}

	mutex_exit(nxgep->genlock);
	return (err);
}

/*
 * Modify a mac address added by nxge_m_mmac_add or nxge_m_mmac_reserve().
 */
static int
nxge_m_mmac_modify(void *arg, mac_multi_addr_t *maddr)
{
	p_nxge_t nxgep = arg;
	mac_addr_slot_t slot;
	nxge_mmac_t *mmac_info;
	int err = 0;
	nxge_status_t status;

	if (!mac_unicst_verify(nxgep->mach, maddr->mma_addr,
	    maddr->mma_addrlen))
		return (EINVAL);

	slot = maddr->mma_slot;

	mutex_enter(nxgep->genlock);

	/*
	 * Make sure that nxge is initialized, if _start() has
	 * not been called.
	 */
	if (!(nxgep->drv_state & STATE_HW_INITIALIZED)) {
		status = nxge_init(nxgep);
		if (status != NXGE_OK) {
			mutex_exit(nxgep->genlock);
			return (ENXIO);
		}
	}

	mmac_info = &nxgep->nxge_mmac_info;
	if (slot < 1 || slot > mmac_info->num_mmac) {
		mutex_exit(nxgep->genlock);
		return (EINVAL);
	}
	if (mmac_info->mac_pool[slot].flags & MMAC_SLOT_USED) {
		if ((err = nxge_altmac_set(nxgep, maddr->mma_addr, slot))
		    != 0) {
			bcopy(maddr->mma_addr, mmac_info->mac_pool[slot].addr,
			    ETHERADDRL);
			/*
			 * Assume that the MAC passed down from the caller
			 * is not a factory MAC address (The user should
			 * call mmac_remove followed by mmac_reserve if
			 * he wants to use the factory MAC for this slot).
			 */
			mmac_info->mac_pool[slot].flags &= ~MMAC_VENDOR_ADDR;
			nxge_mmac_kstat_update(nxgep, slot, B_FALSE);
		}
	} else {
		err = EINVAL;
	}
	mutex_exit(nxgep->genlock);
	return (err);
}

/*
 * nxge_m_mmac_get() - Get the MAC address and other information
 * related to the slot.  mma_flags should be set to 0 in the call.
 * Note: although kstat shows MAC address as zero when a slot is
 * not used, Crossbow expects nxge_m_mmac_get to copy factory MAC
 * to the caller as long as the slot is not using a user MAC address.
 * The following table shows the rules,
 *
 *				   USED    VENDOR    mma_addr
 * ------------------------------------------------------------
 * (1) Slot uses a user MAC:        yes      no     user MAC
 * (2) Slot uses a factory MAC:     yes      yes    factory MAC
 * (3) Slot is not used but is
 *     factory MAC capable:         no       yes    factory MAC
 * (4) Slot is not used and is
 *     not factory MAC capable:     no       no        0
 * ------------------------------------------------------------
 */
static int
nxge_m_mmac_get(void *arg, mac_multi_addr_t *maddr)
{
	nxge_t *nxgep = arg;
	mac_addr_slot_t slot;
	nxge_mmac_t *mmac_info;
	nxge_status_t status;

	slot = maddr->mma_slot;

	mutex_enter(nxgep->genlock);

	/*
	 * Make sure that nxge is initialized, if _start() has
	 * not been called.
	 */
	if (!(nxgep->drv_state & STATE_HW_INITIALIZED)) {
		status = nxge_init(nxgep);
		if (status != NXGE_OK) {
			mutex_exit(nxgep->genlock);
			return (ENXIO);
		}
	}

	mmac_info = &nxgep->nxge_mmac_info;

	if (slot < 1 || slot > mmac_info->num_mmac) {
		mutex_exit(nxgep->genlock);
		return (EINVAL);
	}
	maddr->mma_flags = 0;
	if (mmac_info->mac_pool[slot].flags & MMAC_SLOT_USED)
		maddr->mma_flags |= MMAC_SLOT_USED;

	if (mmac_info->mac_pool[slot].flags & MMAC_VENDOR_ADDR) {
		maddr->mma_flags |= MMAC_VENDOR_ADDR;
		bcopy(mmac_info->factory_mac_pool[slot],
		    maddr->mma_addr, ETHERADDRL);
		maddr->mma_addrlen = ETHERADDRL;
	} else {
		if (maddr->mma_flags & MMAC_SLOT_USED) {
			bcopy(mmac_info->mac_pool[slot].addr,
			    maddr->mma_addr, ETHERADDRL);
			maddr->mma_addrlen = ETHERADDRL;
		} else {
			bzero(maddr->mma_addr, ETHERADDRL);
			maddr->mma_addrlen = 0;
		}
	}
	mutex_exit(nxgep->genlock);
	return (0);
}

static boolean_t
nxge_m_getcapab(void *arg, mac_capab_t cap, void *cap_data)
{
	nxge_t *nxgep = arg;
	uint32_t *txflags = cap_data;
	multiaddress_capab_t *mmacp = cap_data;

	switch (cap) {
	case MAC_CAPAB_HCKSUM:
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_m_getcapab: checksum %d", nxge_cksum_offload));
		if (nxge_cksum_offload <= 1) {
			*txflags = HCKSUM_INET_PARTIAL;
		}
		break;

	case MAC_CAPAB_POLL:
		/*
		 * There's nothing for us to fill in, simply returning
		 * B_TRUE stating that we support polling is sufficient.
		 */
		break;

	case MAC_CAPAB_MULTIADDRESS:
		mmacp = (multiaddress_capab_t *)cap_data;
		mutex_enter(nxgep->genlock);

		mmacp->maddr_naddr = nxgep->nxge_mmac_info.num_mmac;
		mmacp->maddr_naddrfree = nxgep->nxge_mmac_info.naddrfree;
		mmacp->maddr_flag = 0; /* 0 is required by PSARC2006/265 */
		/*
		 * maddr_handle is driver's private data, passed back to
		 * entry point functions as arg.
		 */
		mmacp->maddr_handle	= nxgep;
		mmacp->maddr_add	= nxge_m_mmac_add;
		mmacp->maddr_remove	= nxge_m_mmac_remove;
		mmacp->maddr_modify	= nxge_m_mmac_modify;
		mmacp->maddr_get	= nxge_m_mmac_get;
		mmacp->maddr_reserve	= nxge_m_mmac_reserve;

		mutex_exit(nxgep->genlock);
		break;

	case MAC_CAPAB_LSO: {
		mac_capab_lso_t *cap_lso = cap_data;

		if (nxgep->soft_lso_enable) {
			if (nxge_cksum_offload <= 1) {
				cap_lso->lso_flags = LSO_TX_BASIC_TCP_IPV4;
				if (nxge_lso_max > NXGE_LSO_MAXLEN) {
					nxge_lso_max = NXGE_LSO_MAXLEN;
				}
				cap_lso->lso_basic_tcp_ipv4.lso_max =
				    nxge_lso_max;
			}
			break;
		} else {
			return (B_FALSE);
		}
	}

#if defined(sun4v)
	case MAC_CAPAB_RINGS: {
		mac_capab_rings_t *mrings = (mac_capab_rings_t *)cap_data;

		/*
		 * Only the service domain driver responds to
		 * this capability request.
		 */
		if (isLDOMservice(nxgep)) {
			mrings->mr_handle = (void *)nxgep;

			/*
			 * No dynamic allocation of groups and
			 * rings at this time.  Shares dictate the
			 * configuration.
			 */
			mrings->mr_gadd_ring = NULL;
			mrings->mr_grem_ring = NULL;
			mrings->mr_rget = NULL;
			mrings->mr_gget = nxge_hio_group_get;

			if (mrings->mr_type == MAC_RING_TYPE_RX) {
				mrings->mr_rnum = 8; /* XXX */
				mrings->mr_gnum = 6; /* XXX */
			} else {
				mrings->mr_rnum = 8; /* XXX */
				mrings->mr_gnum = 0; /* XXX */
			}
		} else
			return (B_FALSE);
		break;
	}

	case MAC_CAPAB_SHARES: {
		mac_capab_share_t *mshares = (mac_capab_share_t *)cap_data;

		/*
		 * Only the service domain driver responds to
		 * this capability request.
		 */
		if (isLDOMservice(nxgep)) {
			mshares->ms_snum = 3;
			mshares->ms_handle = (void *)nxgep;
			mshares->ms_salloc = nxge_hio_share_alloc;
			mshares->ms_sfree = nxge_hio_share_free;
			mshares->ms_sadd = NULL;
			mshares->ms_sremove = NULL;
			mshares->ms_squery = nxge_hio_share_query;
		} else
			return (B_FALSE);
		break;
	}
#endif
	default:
		return (B_FALSE);
	}
	return (B_TRUE);
}

static boolean_t
nxge_param_locked(mac_prop_id_t pr_num)
{
	/*
	 * All adv_* parameters are locked (read-only) while
	 * the device is in any sort of loopback mode ...
	 */
	switch (pr_num) {
		case MAC_PROP_ADV_1000FDX_CAP:
		case MAC_PROP_EN_1000FDX_CAP:
		case MAC_PROP_ADV_1000HDX_CAP:
		case MAC_PROP_EN_1000HDX_CAP:
		case MAC_PROP_ADV_100FDX_CAP:
		case MAC_PROP_EN_100FDX_CAP:
		case MAC_PROP_ADV_100HDX_CAP:
		case MAC_PROP_EN_100HDX_CAP:
		case MAC_PROP_ADV_10FDX_CAP:
		case MAC_PROP_EN_10FDX_CAP:
		case MAC_PROP_ADV_10HDX_CAP:
		case MAC_PROP_EN_10HDX_CAP:
		case MAC_PROP_AUTONEG:
		case MAC_PROP_FLOWCTRL:
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * callback functions for set/get of properties
 */
static int
nxge_m_setprop(void *barg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_valsize, const void *pr_val)
{
	nxge_t		*nxgep = barg;
	p_nxge_param_t	param_arr;
	p_nxge_stats_t	statsp;
	int		err = 0;
	uint8_t		val;
	uint32_t	cur_mtu, new_mtu, old_framesize;
	link_flowctrl_t	fl;

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "==> nxge_m_setprop"));
	param_arr = nxgep->param_arr;
	statsp = nxgep->statsp;
	mutex_enter(nxgep->genlock);
	if (statsp->port_stats.lb_mode != nxge_lb_normal &&
	    nxge_param_locked(pr_num)) {
		/*
		 * All adv_* parameters are locked (read-only)
		 * while the device is in any sort of loopback mode.
		 */
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_m_setprop: loopback mode: read only"));
		mutex_exit(nxgep->genlock);
		return (EBUSY);
	}

	val = *(uint8_t *)pr_val;
	switch (pr_num) {
		case MAC_PROP_EN_1000FDX_CAP:
			nxgep->param_en_1000fdx = val;
			param_arr[param_anar_1000fdx].value = val;

			goto reprogram;

		case MAC_PROP_EN_100FDX_CAP:
			nxgep->param_en_100fdx = val;
			param_arr[param_anar_100fdx].value = val;

			goto reprogram;

		case MAC_PROP_EN_10FDX_CAP:
			nxgep->param_en_10fdx = val;
			param_arr[param_anar_10fdx].value = val;

			goto reprogram;

		case MAC_PROP_EN_1000HDX_CAP:
		case MAC_PROP_EN_100HDX_CAP:
		case MAC_PROP_EN_10HDX_CAP:
		case MAC_PROP_ADV_1000FDX_CAP:
		case MAC_PROP_ADV_1000HDX_CAP:
		case MAC_PROP_ADV_100FDX_CAP:
		case MAC_PROP_ADV_100HDX_CAP:
		case MAC_PROP_ADV_10FDX_CAP:
		case MAC_PROP_ADV_10HDX_CAP:
		case MAC_PROP_STATUS:
		case MAC_PROP_SPEED:
		case MAC_PROP_DUPLEX:
			err = EINVAL; /* cannot set read-only properties */
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "==> nxge_m_setprop:  read only property %d",
			    pr_num));
			break;

		case MAC_PROP_AUTONEG:
			param_arr[param_autoneg].value = val;

			goto reprogram;

		case MAC_PROP_MTU:
			if (nxgep->nxge_mac_state == NXGE_MAC_STARTED) {
				err = EBUSY;
				break;
			}

			cur_mtu = nxgep->mac.default_mtu;
			bcopy(pr_val, &new_mtu, sizeof (new_mtu));
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "==> nxge_m_setprop: set MTU: %d is_jumbo %d",
			    new_mtu, nxgep->mac.is_jumbo));

			if (new_mtu == cur_mtu) {
				err = 0;
				break;
			}
			if (new_mtu < NXGE_DEFAULT_MTU ||
			    new_mtu > NXGE_MAXIMUM_MTU) {
				err = EINVAL;
				break;
			}

			if ((new_mtu > NXGE_DEFAULT_MTU) &&
			    !nxgep->mac.is_jumbo) {
				err = EINVAL;
				break;
			}

			old_framesize = (uint32_t)nxgep->mac.maxframesize;
			nxgep->mac.maxframesize = (uint16_t)
			    (new_mtu + NXGE_EHEADER_VLAN_CRC);
			if (nxge_mac_set_framesize(nxgep)) {
				nxgep->mac.maxframesize =
				    (uint16_t)old_framesize;
				err = EINVAL;
				break;
			}

			err = mac_maxsdu_update(nxgep->mach, new_mtu);
			if (err) {
				nxgep->mac.maxframesize =
				    (uint16_t)old_framesize;
				err = EINVAL;
				break;
			}

			nxgep->mac.default_mtu = new_mtu;
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "==> nxge_m_setprop: set MTU: %d maxframe %d",
			    new_mtu, nxgep->mac.maxframesize));
			break;

		case MAC_PROP_FLOWCTRL:
			bcopy(pr_val, &fl, sizeof (fl));
			switch (fl) {
			default:
				err = EINVAL;
				break;

			case LINK_FLOWCTRL_NONE:
				param_arr[param_anar_pause].value = 0;
				break;

			case LINK_FLOWCTRL_RX:
				param_arr[param_anar_pause].value = 1;
				break;

			case LINK_FLOWCTRL_TX:
			case LINK_FLOWCTRL_BI:
				err = EINVAL;
				break;
			}

reprogram:
			if (err == 0) {
				if (!nxge_param_link_update(nxgep)) {
					err = EINVAL;
				}
			}
			break;
		case MAC_PROP_PRIVATE:
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "==> nxge_m_setprop: private property"));
			err = nxge_set_priv_prop(nxgep, pr_name, pr_valsize,
			    pr_val);
			break;

		default:
			err = ENOTSUP;
			break;
	}

	mutex_exit(nxgep->genlock);

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
	    "<== nxge_m_setprop (return %d)", err));
	return (err);
}

static int
nxge_m_getprop(void *barg, const char *pr_name, mac_prop_id_t pr_num,
    uint_t pr_flags, uint_t pr_valsize, void *pr_val)
{
	nxge_t 		*nxgep = barg;
	p_nxge_param_t	param_arr = nxgep->param_arr;
	p_nxge_stats_t	statsp = nxgep->statsp;
	int		err = 0;
	link_flowctrl_t	fl;
	uint64_t	tmp = 0;
	link_state_t	ls;
	boolean_t	is_default = (pr_flags & MAC_PROP_DEFAULT);

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
	    "==> nxge_m_getprop: pr_num %d", pr_num));

	if (pr_valsize == 0)
		return (EINVAL);

	if ((is_default) && (pr_num != MAC_PROP_PRIVATE)) {
		err = nxge_get_def_val(nxgep, pr_num, pr_valsize, pr_val);
		return (err);
	}

	bzero(pr_val, pr_valsize);
	switch (pr_num) {
		case MAC_PROP_DUPLEX:
			*(uint8_t *)pr_val = statsp->mac_stats.link_duplex;
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "==> nxge_m_getprop: duplex mode %d",
			    *(uint8_t *)pr_val));
			break;

		case MAC_PROP_SPEED:
			if (pr_valsize < sizeof (uint64_t))
				return (EINVAL);
			tmp = statsp->mac_stats.link_speed * 1000000ull;
			bcopy(&tmp, pr_val, sizeof (tmp));
			break;

		case MAC_PROP_STATUS:
			if (pr_valsize < sizeof (link_state_t))
				return (EINVAL);
			if (!statsp->mac_stats.link_up)
				ls = LINK_STATE_DOWN;
			else
				ls = LINK_STATE_UP;
			bcopy(&ls, pr_val, sizeof (ls));
			break;

		case MAC_PROP_AUTONEG:
			*(uint8_t *)pr_val =
			    param_arr[param_autoneg].value;
			break;

		case MAC_PROP_FLOWCTRL:
			if (pr_valsize < sizeof (link_flowctrl_t))
				return (EINVAL);

			fl = LINK_FLOWCTRL_NONE;
			if (param_arr[param_anar_pause].value) {
				fl = LINK_FLOWCTRL_RX;
			}
			bcopy(&fl, pr_val, sizeof (fl));
			break;

		case MAC_PROP_ADV_1000FDX_CAP:
			*(uint8_t *)pr_val =
			    param_arr[param_anar_1000fdx].value;
			break;

		case MAC_PROP_EN_1000FDX_CAP:
			*(uint8_t *)pr_val = nxgep->param_en_1000fdx;
			break;

		case MAC_PROP_ADV_100FDX_CAP:
			*(uint8_t *)pr_val =
			    param_arr[param_anar_100fdx].value;
			break;

		case MAC_PROP_EN_100FDX_CAP:
			*(uint8_t *)pr_val = nxgep->param_en_100fdx;
			break;

		case MAC_PROP_ADV_10FDX_CAP:
			*(uint8_t *)pr_val =
			    param_arr[param_anar_10fdx].value;
			break;

		case MAC_PROP_EN_10FDX_CAP:
			*(uint8_t *)pr_val = nxgep->param_en_10fdx;
			break;

		case MAC_PROP_EN_1000HDX_CAP:
		case MAC_PROP_EN_100HDX_CAP:
		case MAC_PROP_EN_10HDX_CAP:
		case MAC_PROP_ADV_1000HDX_CAP:
		case MAC_PROP_ADV_100HDX_CAP:
		case MAC_PROP_ADV_10HDX_CAP:
			err = ENOTSUP;
			break;

		case MAC_PROP_PRIVATE:
			err = nxge_get_priv_prop(nxgep, pr_name, pr_flags,
			    pr_valsize, pr_val);
			break;
		default:
			err = EINVAL;
			break;
	}

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL, "<== nxge_m_getprop"));

	return (err);
}

/* ARGSUSED */
static int
nxge_set_priv_prop(p_nxge_t nxgep, const char *pr_name, uint_t pr_valsize,
    const void *pr_val)
{
	p_nxge_param_t	param_arr = nxgep->param_arr;
	int		err = 0;
	long		result;

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
	    "==> nxge_set_priv_prop: name %s", pr_name));

	if (strcmp(pr_name, "_accept_jumbo") == 0) {
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s "
		    "pr_val %s result %d "
		    "param %d is_jumbo %d",
		    pr_name, pr_val, result,
		    param_arr[param_accept_jumbo].value,
		    nxgep->mac.is_jumbo));

		if (result > 1 || result < 0) {
			err = EINVAL;
		} else {
			if (nxgep->mac.is_jumbo ==
			    (uint32_t)result) {
				NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
				    "no change (%d %d)",
				    nxgep->mac.is_jumbo,
				    result));
				return (0);
			}
		}

		param_arr[param_accept_jumbo].value = result;
		nxgep->mac.is_jumbo = B_FALSE;
		if (result) {
			nxgep->mac.is_jumbo = B_TRUE;
		}

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value %d) is_jumbo %d",
		    pr_name, result, nxgep->mac.is_jumbo));

		return (err);
	}

	/* Blanking */
	if (strcmp(pr_name, "_rxdma_intr_time") == 0) {
		err = nxge_param_rx_intr_time(nxgep, NULL, NULL,
		    (char *)pr_val,
		    (caddr_t)&param_arr[param_rxdma_intr_time]);
		if (err) {
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "<== nxge_set_priv_prop: "
			    "unable to set (%s)", pr_name));
			err = EINVAL;
		} else {
			err = 0;
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "<== nxge_set_priv_prop: "
			    "set (%s)", pr_name));
		}

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value %d)",
		    pr_name, result));

		return (err);
	}

	if (strcmp(pr_name, "_rxdma_intr_pkts") == 0) {
		err = nxge_param_rx_intr_pkts(nxgep, NULL, NULL,
		    (char *)pr_val,
		    (caddr_t)&param_arr[param_rxdma_intr_pkts]);
		if (err) {
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "<== nxge_set_priv_prop: "
			    "unable to set (%s)", pr_name));
			err = EINVAL;
		} else {
			err = 0;
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "<== nxge_set_priv_prop: "
			    "set (%s)", pr_name));
		}

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value %d)",
		    pr_name, result));

		return (err);
	}

	/* Classification */
	if (strcmp(pr_name, "_class_opt_ipv4_tcp") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		err = nxge_param_set_ip_opt(nxgep, NULL,
		    NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_class_opt_ipv4_tcp]);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value 0x%x)",
		    pr_name, result));

		return (err);
	}

	if (strcmp(pr_name, "_class_opt_ipv4_udp") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		err = nxge_param_set_ip_opt(nxgep, NULL,
		    NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_class_opt_ipv4_udp]);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value 0x%x)",
		    pr_name, result));

		return (err);
	}
	if (strcmp(pr_name, "_class_opt_ipv4_ah") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		err = nxge_param_set_ip_opt(nxgep, NULL,
		    NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_class_opt_ipv4_ah]);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value 0x%x)",
		    pr_name, result));

		return (err);
	}
	if (strcmp(pr_name, "_class_opt_ipv4_sctp") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		err = nxge_param_set_ip_opt(nxgep, NULL,
		    NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_class_opt_ipv4_sctp]);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value 0x%x)",
		    pr_name, result));

		return (err);
	}

	if (strcmp(pr_name, "_class_opt_ipv6_tcp") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		err = nxge_param_set_ip_opt(nxgep, NULL,
		    NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_class_opt_ipv6_tcp]);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value 0x%x)",
		    pr_name, result));

		return (err);
	}

	if (strcmp(pr_name, "_class_opt_ipv6_udp") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		err = nxge_param_set_ip_opt(nxgep, NULL,
		    NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_class_opt_ipv6_udp]);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value 0x%x)",
		    pr_name, result));

		return (err);
	}
	if (strcmp(pr_name, "_class_opt_ipv6_ah") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		err = nxge_param_set_ip_opt(nxgep, NULL,
		    NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_class_opt_ipv6_ah]);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value 0x%x)",
		    pr_name, result));

		return (err);
	}
	if (strcmp(pr_name, "_class_opt_ipv6_sctp") == 0) {
		if (pr_val == NULL) {
			err = EINVAL;
			return (err);
		}
		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);

		err = nxge_param_set_ip_opt(nxgep, NULL,
		    NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_class_opt_ipv6_sctp]);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value 0x%x)",
		    pr_name, result));

		return (err);
	}

	if (strcmp(pr_name, "_soft_lso_enable") == 0) {
		if (nxgep->nxge_mac_state == NXGE_MAC_STARTED) {
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "==> nxge_set_priv_prop: name %s (busy)", pr_name));
			err = EBUSY;
			return (err);
		}
		if (pr_val == NULL) {
			NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
			    "==> nxge_set_priv_prop: name %s (null)", pr_name));
			err = EINVAL;
			return (err);
		}

		(void) ddi_strtol(pr_val, (char **)NULL, 0, &result);
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s "
		    "(lso %d pr_val %s value %d)",
		    pr_name, nxgep->soft_lso_enable, pr_val, result));

		if (result > 1 || result < 0) {
			err = EINVAL;
		} else {
			if (nxgep->soft_lso_enable == (uint32_t)result) {
				NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
				    "no change (%d %d)",
				    nxgep->soft_lso_enable, result));
				return (0);
			}
		}

		nxgep->soft_lso_enable = (int)result;

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "<== nxge_set_priv_prop: name %s (value %d)",
		    pr_name, result));

		return (err);
	}
	/*
	 * Commands like "ndd -set /dev/nxge0 adv_10gfdx_cap 1" cause the
	 * following code to be executed.
	 */
	if (strcmp(pr_name, "_adv_10gfdx_cap") == 0) {
		err = nxge_param_set_mac(nxgep, NULL, NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_anar_10gfdx]);
		return (err);
	}
	if (strcmp(pr_name, "_adv_pause_cap") == 0) {
		err = nxge_param_set_mac(nxgep, NULL, NULL, (char *)pr_val,
		    (caddr_t)&param_arr[param_anar_pause]);
		return (err);
	}

	return (EINVAL);
}

static int
nxge_get_priv_prop(p_nxge_t nxgep, const char *pr_name, uint_t pr_flags,
    uint_t pr_valsize, void *pr_val)
{
	p_nxge_param_t	param_arr = nxgep->param_arr;
	char		valstr[MAXNAMELEN];
	int		err = EINVAL;
	uint_t		strsize;
	boolean_t	is_default = (pr_flags & MAC_PROP_DEFAULT);

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
	    "==> nxge_get_priv_prop: property %s", pr_name));

	/* function number */
	if (strcmp(pr_name, "_function_number") == 0) {
		if (is_default)
			return (ENOTSUP);
		(void) snprintf(valstr, sizeof (valstr), "%d",
		    nxgep->function_num);
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: name %s "
		    "(value %d valstr %s)",
		    pr_name, nxgep->function_num, valstr));

		err = 0;
		goto done;
	}

	/* Neptune firmware version */
	if (strcmp(pr_name, "_fw_version") == 0) {
		if (is_default)
			return (ENOTSUP);
		(void) snprintf(valstr, sizeof (valstr), "%s",
		    nxgep->vpd_info.ver);
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: name %s "
		    "(value %d valstr %s)",
		    pr_name, nxgep->vpd_info.ver, valstr));

		err = 0;
		goto done;
	}

	/* port PHY mode */
	if (strcmp(pr_name, "_port_mode") == 0) {
		if (is_default)
			return (ENOTSUP);
		switch (nxgep->mac.portmode) {
		case PORT_1G_COPPER:
			(void) snprintf(valstr, sizeof (valstr), "1G copper %s",
			    nxgep->hot_swappable_phy ?
			    "[Hot Swappable]" : "");
			break;
		case PORT_1G_FIBER:
			(void) snprintf(valstr, sizeof (valstr), "1G fiber %s",
			    nxgep->hot_swappable_phy ?
			    "[hot swappable]" : "");
			break;
		case PORT_10G_COPPER:
			(void) snprintf(valstr, sizeof (valstr),
			    "10G copper %s",
			    nxgep->hot_swappable_phy ?
			    "[hot swappable]" : "");
			break;
		case PORT_10G_FIBER:
			(void) snprintf(valstr, sizeof (valstr), "10G fiber %s",
			    nxgep->hot_swappable_phy ?
			    "[hot swappable]" : "");
			break;
		case PORT_10G_SERDES:
			(void) snprintf(valstr, sizeof (valstr),
			    "10G serdes %s", nxgep->hot_swappable_phy ?
			    "[hot swappable]" : "");
			break;
		case PORT_1G_SERDES:
			(void) snprintf(valstr, sizeof (valstr), "1G serdes %s",
			    nxgep->hot_swappable_phy ?
			    "[hot swappable]" : "");
			break;
		case PORT_1G_TN1010:
			(void) snprintf(valstr, sizeof (valstr),
			    "1G TN1010 copper %s", nxgep->hot_swappable_phy ?
			    "[hot swappable]" : "");
			break;
		case PORT_10G_TN1010:
			(void) snprintf(valstr, sizeof (valstr),
			    "10G TN1010 copper %s", nxgep->hot_swappable_phy ?
			    "[hot swappable]" : "");
			break;
		case PORT_1G_RGMII_FIBER:
			(void) snprintf(valstr, sizeof (valstr),
			    "1G rgmii fiber %s", nxgep->hot_swappable_phy ?
			    "[hot swappable]" : "");
			break;
		case PORT_HSP_MODE:
			(void) snprintf(valstr, sizeof (valstr),
			    "phy not present[hot swappable]");
			break;
		default:
			(void) snprintf(valstr, sizeof (valstr), "unknown %s",
			    nxgep->hot_swappable_phy ?
			    "[hot swappable]" : "");
			break;
		}

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: name %s (value %s)",
		    pr_name, valstr));

		err = 0;
		goto done;
	}

	/* Hot swappable PHY */
	if (strcmp(pr_name, "_hot_swap_phy") == 0) {
		if (is_default)
			return (ENOTSUP);
		(void) snprintf(valstr, sizeof (valstr), "%s",
		    nxgep->hot_swappable_phy ?
		    "yes" : "no");

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: name %s "
		    "(value %d valstr %s)",
		    pr_name, nxgep->hot_swappable_phy, valstr));

		err = 0;
		goto done;
	}


	/* accept jumbo */
	if (strcmp(pr_name, "_accept_jumbo") == 0) {
		if (is_default)
			(void) snprintf(valstr, sizeof (valstr),  "%d", 0);
		else
			(void) snprintf(valstr, sizeof (valstr),
			    "%d", nxgep->mac.is_jumbo);
		err = 0;
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: name %s (value %d (%d, %d))",
		    pr_name,
		    (uint32_t)param_arr[param_accept_jumbo].value,
		    nxgep->mac.is_jumbo,
		    nxge_jumbo_enable));

		goto done;
	}

	/* Receive Interrupt Blanking Parameters */
	if (strcmp(pr_name, "_rxdma_intr_time") == 0) {
		err = 0;
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr),
			    "%d", RXDMA_RCR_TO_DEFAULT);
			goto done;
		}

		(void) snprintf(valstr, sizeof (valstr), "%d",
		    nxgep->intr_timeout);
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: name %s (value %d)",
		    pr_name,
		    (uint32_t)nxgep->intr_timeout));
		goto done;
	}

	if (strcmp(pr_name, "_rxdma_intr_pkts") == 0) {
		err = 0;
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr),
			    "%d", RXDMA_RCR_PTHRES_DEFAULT);
			goto done;
		}
		(void) snprintf(valstr, sizeof (valstr), "%d",
		    nxgep->intr_threshold);
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: name %s (value %d)",
		    pr_name, (uint32_t)nxgep->intr_threshold));

		goto done;
	}

	/* Classification and Load Distribution Configuration */
	if (strcmp(pr_name, "_class_opt_ipv4_tcp") == 0) {
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr), "%x",
			    NXGE_CLASS_FLOW_GEN_SERVER);
			err = 0;
			goto done;
		}
		err = nxge_dld_get_ip_opt(nxgep,
		    (caddr_t)&param_arr[param_class_opt_ipv4_tcp]);

		(void) snprintf(valstr, sizeof (valstr), "%x",
		    (int)param_arr[param_class_opt_ipv4_tcp].value);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: %s", valstr));
		goto done;
	}

	if (strcmp(pr_name, "_class_opt_ipv4_udp") == 0) {
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr), "%x",
			    NXGE_CLASS_FLOW_GEN_SERVER);
			err = 0;
			goto done;
		}
		err = nxge_dld_get_ip_opt(nxgep,
		    (caddr_t)&param_arr[param_class_opt_ipv4_udp]);

		(void) snprintf(valstr, sizeof (valstr), "%x",
		    (int)param_arr[param_class_opt_ipv4_udp].value);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: %s", valstr));
		goto done;
	}
	if (strcmp(pr_name, "_class_opt_ipv4_ah") == 0) {
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr), "%x",
			    NXGE_CLASS_FLOW_GEN_SERVER);
			err = 0;
			goto done;
		}
		err = nxge_dld_get_ip_opt(nxgep,
		    (caddr_t)&param_arr[param_class_opt_ipv4_ah]);

		(void) snprintf(valstr, sizeof (valstr), "%x",
		    (int)param_arr[param_class_opt_ipv4_ah].value);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: %s", valstr));
		goto done;
	}

	if (strcmp(pr_name, "_class_opt_ipv4_sctp") == 0) {
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr), "%x",
			    NXGE_CLASS_FLOW_GEN_SERVER);
			err = 0;
			goto done;
		}
		err = nxge_dld_get_ip_opt(nxgep,
		    (caddr_t)&param_arr[param_class_opt_ipv4_sctp]);

		(void) snprintf(valstr, sizeof (valstr), "%x",
		    (int)param_arr[param_class_opt_ipv4_sctp].value);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: %s", valstr));
		goto done;
	}

	if (strcmp(pr_name, "_class_opt_ipv6_tcp") == 0) {
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr), "%x",
			    NXGE_CLASS_FLOW_GEN_SERVER);
			err = 0;
			goto done;
		}
		err = nxge_dld_get_ip_opt(nxgep,
		    (caddr_t)&param_arr[param_class_opt_ipv6_tcp]);

		(void) snprintf(valstr, sizeof (valstr), "%x",
		    (int)param_arr[param_class_opt_ipv6_tcp].value);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: %s", valstr));
		goto done;
	}

	if (strcmp(pr_name, "_class_opt_ipv6_udp") == 0) {
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr), "%x",
			    NXGE_CLASS_FLOW_GEN_SERVER);
			err = 0;
			goto done;
		}
		err = nxge_dld_get_ip_opt(nxgep,
		    (caddr_t)&param_arr[param_class_opt_ipv6_udp]);

		(void) snprintf(valstr, sizeof (valstr), "%x",
		    (int)param_arr[param_class_opt_ipv6_udp].value);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: %s", valstr));
		goto done;
	}

	if (strcmp(pr_name, "_class_opt_ipv6_ah") == 0) {
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr), "%x",
			    NXGE_CLASS_FLOW_GEN_SERVER);
			err = 0;
			goto done;
		}
		err = nxge_dld_get_ip_opt(nxgep,
		    (caddr_t)&param_arr[param_class_opt_ipv6_ah]);

		(void) snprintf(valstr, sizeof (valstr), "%x",
		    (int)param_arr[param_class_opt_ipv6_ah].value);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: %s", valstr));
		goto done;
	}

	if (strcmp(pr_name, "_class_opt_ipv6_sctp") == 0) {
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr), "%x",
			    NXGE_CLASS_FLOW_GEN_SERVER);
			err = 0;
			goto done;
		}
		err = nxge_dld_get_ip_opt(nxgep,
		    (caddr_t)&param_arr[param_class_opt_ipv6_sctp]);

		(void) snprintf(valstr, sizeof (valstr), "%x",
		    (int)param_arr[param_class_opt_ipv6_sctp].value);

		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: %s", valstr));
		goto done;
	}

	/* Software LSO */
	if (strcmp(pr_name, "_soft_lso_enable") == 0) {
		if (is_default) {
			(void) snprintf(valstr, sizeof (valstr), "%d", 0);
			err = 0;
			goto done;
		}
		(void) snprintf(valstr, sizeof (valstr),
		    "%d", nxgep->soft_lso_enable);
		err = 0;
		NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
		    "==> nxge_get_priv_prop: name %s (value %d)",
		    pr_name, nxgep->soft_lso_enable));

		goto done;
	}
	if (strcmp(pr_name, "_adv_10gfdx_cap") == 0) {
		err = 0;
		if (is_default ||
		    nxgep->param_arr[param_anar_10gfdx].value != 0) {
			(void) snprintf(valstr, sizeof (valstr), "%d", 1);
			goto done;
		} else {
			(void) snprintf(valstr, sizeof (valstr), "%d", 0);
			goto done;
		}
	}
	if (strcmp(pr_name, "_adv_pause_cap") == 0) {
		err = 0;
		if (is_default ||
		    nxgep->param_arr[param_anar_pause].value != 0) {
			(void) snprintf(valstr, sizeof (valstr), "%d", 1);
			goto done;
		} else {
			(void) snprintf(valstr, sizeof (valstr), "%d", 0);
			goto done;
		}
	}

done:
	if (err == 0) {
		strsize = (uint_t)strlen(valstr);
		if (pr_valsize < strsize) {
			err = ENOBUFS;
		} else {
			(void) strlcpy(pr_val, valstr, pr_valsize);
		}
	}

	NXGE_DEBUG_MSG((nxgep, NXGE_CTL,
	    "<== nxge_get_priv_prop: return %d", err));
	return (err);
}

/*
 * Module loading and removing entry points.
 */

DDI_DEFINE_STREAM_OPS(nxge_dev_ops, nulldev, nulldev, nxge_attach, nxge_detach,
    nodev, NULL, D_MP, NULL);

#define	NXGE_DESC_VER		"Sun NIU 10Gb Ethernet"

/*
 * Module linkage information for the kernel.
 */
static struct modldrv 	nxge_modldrv = {
	&mod_driverops,
	NXGE_DESC_VER,
	&nxge_dev_ops
};

static struct modlinkage modlinkage = {
	MODREV_1, (void *) &nxge_modldrv, NULL
};

int
_init(void)
{
	int		status;

	NXGE_DEBUG_MSG((NULL, MOD_CTL, "==> _init"));
	mac_init_ops(&nxge_dev_ops, "nxge");
	status = ddi_soft_state_init(&nxge_list, sizeof (nxge_t), 0);
	if (status != 0) {
		NXGE_ERROR_MSG((NULL, NXGE_ERR_CTL,
		    "failed to init device soft state"));
		goto _init_exit;
	}
	status = mod_install(&modlinkage);
	if (status != 0) {
		ddi_soft_state_fini(&nxge_list);
		NXGE_ERROR_MSG((NULL, NXGE_ERR_CTL, "Mod install failed"));
		goto _init_exit;
	}

	MUTEX_INIT(&nxge_common_lock, NULL, MUTEX_DRIVER, NULL);

_init_exit:
	NXGE_DEBUG_MSG((NULL, MOD_CTL, "_init status = 0x%X", status));

	return (status);
}

int
_fini(void)
{
	int		status;

	NXGE_DEBUG_MSG((NULL, MOD_CTL, "==> _fini"));

	NXGE_DEBUG_MSG((NULL, MOD_CTL, "==> _fini: mod_remove"));

	if (nxge_mblks_pending)
		return (EBUSY);

	status = mod_remove(&modlinkage);
	if (status != DDI_SUCCESS) {
		NXGE_DEBUG_MSG((NULL, MOD_CTL,
		    "Module removal failed 0x%08x",
		    status));
		goto _fini_exit;
	}

	mac_fini_ops(&nxge_dev_ops);

	ddi_soft_state_fini(&nxge_list);

	MUTEX_DESTROY(&nxge_common_lock);
_fini_exit:
	NXGE_DEBUG_MSG((NULL, MOD_CTL, "_fini status = 0x%08x", status));

	return (status);
}

int
_info(struct modinfo *modinfop)
{
	int		status;

	NXGE_DEBUG_MSG((NULL, MOD_CTL, "==> _info"));
	status = mod_info(&modlinkage, modinfop);
	NXGE_DEBUG_MSG((NULL, MOD_CTL, " _info status = 0x%X", status));

	return (status);
}

/*ARGSUSED*/
static nxge_status_t
nxge_add_intrs(p_nxge_t nxgep)
{

	int		intr_types;
	int		type = 0;
	int		ddi_status = DDI_SUCCESS;
	nxge_status_t	status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_intrs"));

	nxgep->nxge_intr_type.intr_registered = B_FALSE;
	nxgep->nxge_intr_type.intr_enabled = B_FALSE;
	nxgep->nxge_intr_type.msi_intx_cnt = 0;
	nxgep->nxge_intr_type.intr_added = 0;
	nxgep->nxge_intr_type.niu_msi_enable = B_FALSE;
	nxgep->nxge_intr_type.intr_type = 0;

	if (nxgep->niu_type == N2_NIU) {
		nxgep->nxge_intr_type.niu_msi_enable = B_TRUE;
	} else if (nxge_msi_enable) {
		nxgep->nxge_intr_type.niu_msi_enable = B_TRUE;
	}

	/* Get the supported interrupt types */
	if ((ddi_status = ddi_intr_get_supported_types(nxgep->dip, &intr_types))
	    != DDI_SUCCESS) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "<== nxge_add_intrs: "
		    "ddi_intr_get_supported_types failed: status 0x%08x",
		    ddi_status));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}
	nxgep->nxge_intr_type.intr_types = intr_types;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_intrs: "
	    "ddi_intr_get_supported_types: 0x%08x", intr_types));

	/*
	 * Solaris MSIX is not supported yet. use MSI for now.
	 * nxge_msi_enable (1):
	 *	1 - MSI		2 - MSI-X	others - FIXED
	 */
	switch (nxge_msi_enable) {
	default:
		type = DDI_INTR_TYPE_FIXED;
		NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_add_intrs: "
		    "use fixed (intx emulation) type %08x",
		    type));
		break;

	case 2:
		NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_add_intrs: "
		    "ddi_intr_get_supported_types: 0x%08x", intr_types));
		if (intr_types & DDI_INTR_TYPE_MSIX) {
			type = DDI_INTR_TYPE_MSIX;
			NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_intrs: "
			    "ddi_intr_get_supported_types: MSIX 0x%08x",
			    type));
		} else if (intr_types & DDI_INTR_TYPE_MSI) {
			type = DDI_INTR_TYPE_MSI;
			NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_intrs: "
			    "ddi_intr_get_supported_types: MSI 0x%08x",
			    type));
		} else if (intr_types & DDI_INTR_TYPE_FIXED) {
			type = DDI_INTR_TYPE_FIXED;
			NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_add_intrs: "
			    "ddi_intr_get_supported_types: MSXED0x%08x",
			    type));
		}
		break;

	case 1:
		if (intr_types & DDI_INTR_TYPE_MSI) {
			type = DDI_INTR_TYPE_MSI;
			NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_add_intrs: "
			    "ddi_intr_get_supported_types: MSI 0x%08x",
			    type));
		} else if (intr_types & DDI_INTR_TYPE_MSIX) {
			type = DDI_INTR_TYPE_MSIX;
			NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_intrs: "
			    "ddi_intr_get_supported_types: MSIX 0x%08x",
			    type));
		} else if (intr_types & DDI_INTR_TYPE_FIXED) {
			type = DDI_INTR_TYPE_FIXED;
			NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_intrs: "
			    "ddi_intr_get_supported_types: MSXED0x%08x",
			    type));
		}
	}

	nxgep->nxge_intr_type.intr_type = type;
	if ((type == DDI_INTR_TYPE_MSIX || type == DDI_INTR_TYPE_MSI ||
	    type == DDI_INTR_TYPE_FIXED) &&
	    nxgep->nxge_intr_type.niu_msi_enable) {
		if ((status = nxge_add_intrs_adv(nxgep)) != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    " nxge_add_intrs: "
			    " nxge_add_intrs_adv failed: status 0x%08x",
			    status));
			return (status);
		} else {
			NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_intrs: "
			    "interrupts registered : type %d", type));
			nxgep->nxge_intr_type.intr_registered = B_TRUE;

			NXGE_DEBUG_MSG((nxgep, DDI_CTL,
			    "\nAdded advanced nxge add_intr_adv "
			    "intr type 0x%x\n", type));

			return (status);
		}
	}

	if (!nxgep->nxge_intr_type.intr_registered) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "==> nxge_add_intrs: "
		    "failed to register interrupts"));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_add_intrs"));
	return (status);
}

/*ARGSUSED*/
static nxge_status_t
nxge_add_soft_intrs(p_nxge_t nxgep)
{

	int		ddi_status = DDI_SUCCESS;
	nxge_status_t	status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_soft_intrs"));

	nxgep->resched_id = NULL;
	nxgep->resched_running = B_FALSE;
	ddi_status = ddi_add_softintr(nxgep->dip, DDI_SOFTINT_LOW,
	    &nxgep->resched_id,
	    NULL, NULL, nxge_reschedule, (caddr_t)nxgep);
	if (ddi_status != DDI_SUCCESS) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "<== nxge_add_soft_intrs: "
		    "ddi_add_softintrs failed: status 0x%08x",
		    ddi_status));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_ddi_add_soft_intrs"));

	return (status);
}

static nxge_status_t
nxge_add_intrs_adv(p_nxge_t nxgep)
{
	int		intr_type;
	p_nxge_intr_t	intrp;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_intrs_adv"));

	intrp = (p_nxge_intr_t)&nxgep->nxge_intr_type;
	intr_type = intrp->intr_type;
	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_add_intrs_adv: type 0x%x",
	    intr_type));

	switch (intr_type) {
	case DDI_INTR_TYPE_MSI: /* 0x2 */
	case DDI_INTR_TYPE_MSIX: /* 0x4 */
		return (nxge_add_intrs_adv_type(nxgep, intr_type));

	case DDI_INTR_TYPE_FIXED: /* 0x1 */
		return (nxge_add_intrs_adv_type_fix(nxgep, intr_type));

	default:
		return (NXGE_ERROR);
	}
}


/*ARGSUSED*/
static nxge_status_t
nxge_add_intrs_adv_type(p_nxge_t nxgep, uint32_t int_type)
{
	dev_info_t		*dip = nxgep->dip;
	p_nxge_ldg_t		ldgp;
	p_nxge_intr_t		intrp;
	uint_t			*inthandler;
	void			*arg1, *arg2;
	int			behavior;
	int			nintrs, navail, nrequest;
	int			nactual, nrequired;
	int			inum = 0;
	int			x, y;
	int			ddi_status = DDI_SUCCESS;
	nxge_status_t		status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_add_intrs_adv_type"));
	intrp = (p_nxge_intr_t)&nxgep->nxge_intr_type;
	intrp->start_inum = 0;

	ddi_status = ddi_intr_get_nintrs(dip, int_type, &nintrs);
	if ((ddi_status != DDI_SUCCESS) || (nintrs == 0)) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "ddi_intr_get_nintrs() failed, status: 0x%x%, "
		    "nintrs: %d", ddi_status, nintrs));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	ddi_status = ddi_intr_get_navail(dip, int_type, &navail);
	if ((ddi_status != DDI_SUCCESS) || (navail == 0)) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "ddi_intr_get_navail() failed, status: 0x%x%, "
		    "nintrs: %d", ddi_status, navail));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	NXGE_DEBUG_MSG((nxgep, INT_CTL,
	    "ddi_intr_get_navail() returned: nintrs %d, navail %d",
	    nintrs, navail));

	/* PSARC/2007/453 MSI-X interrupt limit override */
	if (int_type == DDI_INTR_TYPE_MSIX) {
		nrequest = nxge_create_msi_property(nxgep);
		if (nrequest < navail) {
			navail = nrequest;
			NXGE_DEBUG_MSG((nxgep, INT_CTL,
			    "nxge_add_intrs_adv_type: nintrs %d "
			    "navail %d (nrequest %d)",
			    nintrs, navail, nrequest));
		}
	}

	if (int_type == DDI_INTR_TYPE_MSI && !ISP2(navail)) {
		/* MSI must be power of 2 */
		if ((navail & 16) == 16) {
			navail = 16;
		} else if ((navail & 8) == 8) {
			navail = 8;
		} else if ((navail & 4) == 4) {
			navail = 4;
		} else if ((navail & 2) == 2) {
			navail = 2;
		} else {
			navail = 1;
		}
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
		    "ddi_intr_get_navail(): (msi power of 2) nintrs %d, "
		    "navail %d", nintrs, navail));
	}

	behavior = ((int_type == DDI_INTR_TYPE_FIXED) ? DDI_INTR_ALLOC_STRICT :
	    DDI_INTR_ALLOC_NORMAL);
	intrp->intr_size = navail * sizeof (ddi_intr_handle_t);
	intrp->htable = kmem_alloc(intrp->intr_size, KM_SLEEP);
	ddi_status = ddi_intr_alloc(dip, intrp->htable, int_type, inum,
	    navail, &nactual, behavior);
	if (ddi_status != DDI_SUCCESS || nactual == 0) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    " ddi_intr_alloc() failed: %d",
		    ddi_status));
		kmem_free(intrp->htable, intrp->intr_size);
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	if ((ddi_status = ddi_intr_get_pri(intrp->htable[0],
	    (uint_t *)&intrp->pri)) != DDI_SUCCESS) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    " ddi_intr_get_pri() failed: %d",
		    ddi_status));
		/* Free already allocated interrupts */
		for (y = 0; y < nactual; y++) {
			(void) ddi_intr_free(intrp->htable[y]);
		}

		kmem_free(intrp->htable, intrp->intr_size);
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	nrequired = 0;
	switch (nxgep->niu_type) {
	default:
		status = nxge_ldgv_init(nxgep, &nactual, &nrequired);
		break;

	case N2_NIU:
		status = nxge_ldgv_init_n2(nxgep, &nactual, &nrequired);
		break;
	}

	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "nxge_add_intrs_adv_typ:nxge_ldgv_init "
		    "failed: 0x%x", status));
		/* Free already allocated interrupts */
		for (y = 0; y < nactual; y++) {
			(void) ddi_intr_free(intrp->htable[y]);
		}

		kmem_free(intrp->htable, intrp->intr_size);
		return (status);
	}

	ldgp = nxgep->ldgvp->ldgp;
	for (x = 0; x < nrequired; x++, ldgp++) {
		ldgp->vector = (uint8_t)x;
		ldgp->intdata = SID_DATA(ldgp->func, x);
		arg1 = ldgp->ldvp;
		arg2 = nxgep;
		if (ldgp->nldvs == 1) {
			inthandler = (uint_t *)ldgp->ldvp->ldv_intr_handler;
			NXGE_DEBUG_MSG((nxgep, INT_CTL,
			    "nxge_add_intrs_adv_type: "
			    "arg1 0x%x arg2 0x%x: "
			    "1-1 int handler (entry %d intdata 0x%x)\n",
			    arg1, arg2,
			    x, ldgp->intdata));
		} else if (ldgp->nldvs > 1) {
			inthandler = (uint_t *)ldgp->sys_intr_handler;
			NXGE_DEBUG_MSG((nxgep, INT_CTL,
			    "nxge_add_intrs_adv_type: "
			    "arg1 0x%x arg2 0x%x: "
			    "nldevs %d int handler "
			    "(entry %d intdata 0x%x)\n",
			    arg1, arg2,
			    ldgp->nldvs, x, ldgp->intdata));
		}

		NXGE_DEBUG_MSG((nxgep, INT_CTL,
		    "==> nxge_add_intrs_adv_type: ddi_add_intr(inum) #%d "
		    "htable 0x%llx", x, intrp->htable[x]));

		if ((ddi_status = ddi_intr_add_handler(intrp->htable[x],
		    (ddi_intr_handler_t *)inthandler, arg1, arg2))
		    != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "==> nxge_add_intrs_adv_type: failed #%d "
			    "status 0x%x", x, ddi_status));
			for (y = 0; y < intrp->intr_added; y++) {
				(void) ddi_intr_remove_handler(
				    intrp->htable[y]);
			}
			/* Free already allocated intr */
			for (y = 0; y < nactual; y++) {
				(void) ddi_intr_free(intrp->htable[y]);
			}
			kmem_free(intrp->htable, intrp->intr_size);

			(void) nxge_ldgv_uninit(nxgep);

			return (NXGE_ERROR | NXGE_DDI_FAILED);
		}
		intrp->intr_added++;
	}

	intrp->msi_intx_cnt = nactual;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "Requested: %d, Allowed: %d msi_intx_cnt %d intr_added %d",
	    navail, nactual,
	    intrp->msi_intx_cnt,
	    intrp->intr_added));

	(void) ddi_intr_get_cap(intrp->htable[0], &intrp->intr_cap);

	(void) nxge_intr_ldgv_init(nxgep);

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_add_intrs_adv_type"));

	return (status);
}

/*ARGSUSED*/
static nxge_status_t
nxge_add_intrs_adv_type_fix(p_nxge_t nxgep, uint32_t int_type)
{
	dev_info_t		*dip = nxgep->dip;
	p_nxge_ldg_t		ldgp;
	p_nxge_intr_t		intrp;
	uint_t			*inthandler;
	void			*arg1, *arg2;
	int			behavior;
	int			nintrs, navail;
	int			nactual, nrequired;
	int			inum = 0;
	int			x, y;
	int			ddi_status = DDI_SUCCESS;
	nxge_status_t		status = NXGE_OK;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_add_intrs_adv_type_fix"));
	intrp = (p_nxge_intr_t)&nxgep->nxge_intr_type;
	intrp->start_inum = 0;

	ddi_status = ddi_intr_get_nintrs(dip, int_type, &nintrs);
	if ((ddi_status != DDI_SUCCESS) || (nintrs == 0)) {
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
		    "ddi_intr_get_nintrs() failed, status: 0x%x%, "
		    "nintrs: %d", status, nintrs));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	ddi_status = ddi_intr_get_navail(dip, int_type, &navail);
	if ((ddi_status != DDI_SUCCESS) || (navail == 0)) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "ddi_intr_get_navail() failed, status: 0x%x%, "
		    "nintrs: %d", ddi_status, navail));
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	NXGE_DEBUG_MSG((nxgep, INT_CTL,
	    "ddi_intr_get_navail() returned: nintrs %d, naavail %d",
	    nintrs, navail));

	behavior = ((int_type == DDI_INTR_TYPE_FIXED) ? DDI_INTR_ALLOC_STRICT :
	    DDI_INTR_ALLOC_NORMAL);
	intrp->intr_size = navail * sizeof (ddi_intr_handle_t);
	intrp->htable = kmem_alloc(intrp->intr_size, KM_SLEEP);
	ddi_status = ddi_intr_alloc(dip, intrp->htable, int_type, inum,
	    navail, &nactual, behavior);
	if (ddi_status != DDI_SUCCESS || nactual == 0) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    " ddi_intr_alloc() failed: %d",
		    ddi_status));
		kmem_free(intrp->htable, intrp->intr_size);
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	if ((ddi_status = ddi_intr_get_pri(intrp->htable[0],
	    (uint_t *)&intrp->pri)) != DDI_SUCCESS) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    " ddi_intr_get_pri() failed: %d",
		    ddi_status));
		/* Free already allocated interrupts */
		for (y = 0; y < nactual; y++) {
			(void) ddi_intr_free(intrp->htable[y]);
		}

		kmem_free(intrp->htable, intrp->intr_size);
		return (NXGE_ERROR | NXGE_DDI_FAILED);
	}

	nrequired = 0;
	switch (nxgep->niu_type) {
	default:
		status = nxge_ldgv_init(nxgep, &nactual, &nrequired);
		break;

	case N2_NIU:
		status = nxge_ldgv_init_n2(nxgep, &nactual, &nrequired);
		break;
	}

	if (status != NXGE_OK) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "nxge_add_intrs_adv_type_fix:nxge_ldgv_init "
		    "failed: 0x%x", status));
		/* Free already allocated interrupts */
		for (y = 0; y < nactual; y++) {
			(void) ddi_intr_free(intrp->htable[y]);
		}

		kmem_free(intrp->htable, intrp->intr_size);
		return (status);
	}

	ldgp = nxgep->ldgvp->ldgp;
	for (x = 0; x < nrequired; x++, ldgp++) {
		ldgp->vector = (uint8_t)x;
		if (nxgep->niu_type != N2_NIU) {
			ldgp->intdata = SID_DATA(ldgp->func, x);
		}

		arg1 = ldgp->ldvp;
		arg2 = nxgep;
		if (ldgp->nldvs == 1) {
			inthandler = (uint_t *)ldgp->ldvp->ldv_intr_handler;
			NXGE_DEBUG_MSG((nxgep, INT_CTL,
			    "nxge_add_intrs_adv_type_fix: "
			    "1-1 int handler(%d) ldg %d ldv %d "
			    "arg1 $%p arg2 $%p\n",
			    x, ldgp->ldg, ldgp->ldvp->ldv,
			    arg1, arg2));
		} else if (ldgp->nldvs > 1) {
			inthandler = (uint_t *)ldgp->sys_intr_handler;
			NXGE_DEBUG_MSG((nxgep, INT_CTL,
			    "nxge_add_intrs_adv_type_fix: "
			    "shared ldv %d int handler(%d) ldv %d ldg %d"
			    "arg1 0x%016llx arg2 0x%016llx\n",
			    x, ldgp->nldvs, ldgp->ldg, ldgp->ldvp->ldv,
			    arg1, arg2));
		}

		if ((ddi_status = ddi_intr_add_handler(intrp->htable[x],
		    (ddi_intr_handler_t *)inthandler, arg1, arg2))
		    != DDI_SUCCESS) {
			NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
			    "==> nxge_add_intrs_adv_type_fix: failed #%d "
			    "status 0x%x", x, ddi_status));
			for (y = 0; y < intrp->intr_added; y++) {
				(void) ddi_intr_remove_handler(
				    intrp->htable[y]);
			}
			for (y = 0; y < nactual; y++) {
				(void) ddi_intr_free(intrp->htable[y]);
			}
			/* Free already allocated intr */
			kmem_free(intrp->htable, intrp->intr_size);

			(void) nxge_ldgv_uninit(nxgep);

			return (NXGE_ERROR | NXGE_DDI_FAILED);
		}
		intrp->intr_added++;
	}

	intrp->msi_intx_cnt = nactual;

	(void) ddi_intr_get_cap(intrp->htable[0], &intrp->intr_cap);

	status = nxge_intr_ldgv_init(nxgep);
	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_add_intrs_adv_type_fix"));

	return (status);
}

static void
nxge_remove_intrs(p_nxge_t nxgep)
{
	int		i, inum;
	p_nxge_intr_t	intrp;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_remove_intrs"));
	intrp = (p_nxge_intr_t)&nxgep->nxge_intr_type;
	if (!intrp->intr_registered) {
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
		    "<== nxge_remove_intrs: interrupts not registered"));
		return;
	}

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_remove_intrs:advanced"));

	if (intrp->intr_cap & DDI_INTR_FLAG_BLOCK) {
		(void) ddi_intr_block_disable(intrp->htable,
		    intrp->intr_added);
	} else {
		for (i = 0; i < intrp->intr_added; i++) {
			(void) ddi_intr_disable(intrp->htable[i]);
		}
	}

	for (inum = 0; inum < intrp->intr_added; inum++) {
		if (intrp->htable[inum]) {
			(void) ddi_intr_remove_handler(intrp->htable[inum]);
		}
	}

	for (inum = 0; inum < intrp->msi_intx_cnt; inum++) {
		if (intrp->htable[inum]) {
			NXGE_DEBUG_MSG((nxgep, DDI_CTL,
			    "nxge_remove_intrs: ddi_intr_free inum %d "
			    "msi_intx_cnt %d intr_added %d",
			    inum,
			    intrp->msi_intx_cnt,
			    intrp->intr_added));

			(void) ddi_intr_free(intrp->htable[inum]);
		}
	}

	kmem_free(intrp->htable, intrp->intr_size);
	intrp->intr_registered = B_FALSE;
	intrp->intr_enabled = B_FALSE;
	intrp->msi_intx_cnt = 0;
	intrp->intr_added = 0;

	(void) nxge_ldgv_uninit(nxgep);

	(void) ddi_prop_remove(DDI_DEV_T_NONE, nxgep->dip,
	    "#msix-request");

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_remove_intrs"));
}

/*ARGSUSED*/
static void
nxge_remove_soft_intrs(p_nxge_t nxgep)
{
	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_remove_soft_intrs"));
	if (nxgep->resched_id) {
		ddi_remove_softintr(nxgep->resched_id);
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
		    "==> nxge_remove_soft_intrs: removed"));
		nxgep->resched_id = NULL;
	}

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_remove_soft_intrs"));
}

/*ARGSUSED*/
static void
nxge_intrs_enable(p_nxge_t nxgep)
{
	p_nxge_intr_t	intrp;
	int		i;
	int		status;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_intrs_enable"));

	intrp = (p_nxge_intr_t)&nxgep->nxge_intr_type;

	if (!intrp->intr_registered) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL, "<== nxge_intrs_enable: "
		    "interrupts are not registered"));
		return;
	}

	if (intrp->intr_enabled) {
		NXGE_DEBUG_MSG((nxgep, INT_CTL,
		    "<== nxge_intrs_enable: already enabled"));
		return;
	}

	if (intrp->intr_cap & DDI_INTR_FLAG_BLOCK) {
		status = ddi_intr_block_enable(intrp->htable,
		    intrp->intr_added);
		NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_intrs_enable "
		    "block enable - status 0x%x total inums #%d\n",
		    status, intrp->intr_added));
	} else {
		for (i = 0; i < intrp->intr_added; i++) {
			status = ddi_intr_enable(intrp->htable[i]);
			NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_intrs_enable "
			    "ddi_intr_enable:enable - status 0x%x "
			    "total inums %d enable inum #%d\n",
			    status, intrp->intr_added, i));
			if (status == DDI_SUCCESS) {
				intrp->intr_enabled = B_TRUE;
			}
		}
	}

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_intrs_enable"));
}

/*ARGSUSED*/
static void
nxge_intrs_disable(p_nxge_t nxgep)
{
	p_nxge_intr_t	intrp;
	int		i;

	NXGE_DEBUG_MSG((nxgep, INT_CTL, "==> nxge_intrs_disable"));

	intrp = (p_nxge_intr_t)&nxgep->nxge_intr_type;

	if (!intrp->intr_registered) {
		NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_intrs_disable: "
		    "interrupts are not registered"));
		return;
	}

	if (intrp->intr_cap & DDI_INTR_FLAG_BLOCK) {
		(void) ddi_intr_block_disable(intrp->htable,
		    intrp->intr_added);
	} else {
		for (i = 0; i < intrp->intr_added; i++) {
			(void) ddi_intr_disable(intrp->htable[i]);
		}
	}

	intrp->intr_enabled = B_FALSE;
	NXGE_DEBUG_MSG((nxgep, INT_CTL, "<== nxge_intrs_disable"));
}

static nxge_status_t
nxge_mac_register(p_nxge_t nxgep)
{
	mac_register_t *macp;
	int		status;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_mac_register"));

	if ((macp = mac_alloc(MAC_VERSION)) == NULL)
		return (NXGE_ERROR);

	macp->m_type_ident = MAC_PLUGIN_IDENT_ETHER;
	macp->m_driver = nxgep;
	macp->m_dip = nxgep->dip;
	macp->m_src_addr = nxgep->ouraddr.ether_addr_octet;
	macp->m_callbacks = &nxge_m_callbacks;
	macp->m_min_sdu = 0;
	nxgep->mac.default_mtu = nxgep->mac.maxframesize -
	    NXGE_EHEADER_VLAN_CRC;
	macp->m_max_sdu = nxgep->mac.default_mtu;
	macp->m_margin = VLAN_TAGSZ;
	macp->m_priv_props = nxge_priv_props;
	macp->m_priv_prop_count = NXGE_MAX_PRIV_PROPS;

	NXGE_DEBUG_MSG((nxgep, MAC_CTL,
	    "==> nxge_mac_register: instance %d "
	    "max_sdu %d margin %d maxframe %d (header %d)",
	    nxgep->instance,
	    macp->m_max_sdu, macp->m_margin,
	    nxgep->mac.maxframesize,
	    NXGE_EHEADER_VLAN_CRC));

	status = mac_register(macp, &nxgep->mach);
	mac_free(macp);

	if (status != 0) {
		cmn_err(CE_WARN,
		    "!nxge_mac_register failed (status %d instance %d)",
		    status, nxgep->instance);
		return (NXGE_ERROR);
	}

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_mac_register success "
	    "(instance %d)", nxgep->instance));

	return (NXGE_OK);
}

void
nxge_err_inject(p_nxge_t nxgep, queue_t *wq, mblk_t *mp)
{
	ssize_t		size;
	mblk_t		*nmp;
	uint8_t		blk_id;
	uint8_t		chan;
	uint32_t	err_id;
	err_inject_t	*eip;

	NXGE_DEBUG_MSG((nxgep, STR_CTL, "==> nxge_err_inject"));

	size = 1024;
	nmp = mp->b_cont;
	eip = (err_inject_t *)nmp->b_rptr;
	blk_id = eip->blk_id;
	err_id = eip->err_id;
	chan = eip->chan;
	cmn_err(CE_NOTE, "!blk_id = 0x%x\n", blk_id);
	cmn_err(CE_NOTE, "!err_id = 0x%x\n", err_id);
	cmn_err(CE_NOTE, "!chan = 0x%x\n", chan);
	switch (blk_id) {
	case MAC_BLK_ID:
		break;
	case TXMAC_BLK_ID:
		break;
	case RXMAC_BLK_ID:
		break;
	case MIF_BLK_ID:
		break;
	case IPP_BLK_ID:
		nxge_ipp_inject_err(nxgep, err_id);
		break;
	case TXC_BLK_ID:
		nxge_txc_inject_err(nxgep, err_id);
		break;
	case TXDMA_BLK_ID:
		nxge_txdma_inject_err(nxgep, err_id, chan);
		break;
	case RXDMA_BLK_ID:
		nxge_rxdma_inject_err(nxgep, err_id, chan);
		break;
	case ZCP_BLK_ID:
		nxge_zcp_inject_err(nxgep, err_id);
		break;
	case ESPC_BLK_ID:
		break;
	case FFLP_BLK_ID:
		break;
	case PHY_BLK_ID:
		break;
	case ETHER_SERDES_BLK_ID:
		break;
	case PCIE_SERDES_BLK_ID:
		break;
	case VIR_BLK_ID:
		break;
	}

	nmp->b_wptr = nmp->b_rptr + size;
	NXGE_DEBUG_MSG((nxgep, STR_CTL, "<== nxge_err_inject"));

	miocack(wq, mp, (int)size, 0);
}

static int
nxge_init_common_dev(p_nxge_t nxgep)
{
	p_nxge_hw_list_t	hw_p;
	dev_info_t 		*p_dip;

	NXGE_DEBUG_MSG((nxgep, MOD_CTL, "==> nxge_init_common_device"));

	p_dip = nxgep->p_dip;
	MUTEX_ENTER(&nxge_common_lock);
	NXGE_DEBUG_MSG((nxgep, MOD_CTL,
	    "==> nxge_init_common_dev:func # %d",
	    nxgep->function_num));
	/*
	 * Loop through existing per neptune hardware list.
	 */
	for (hw_p = nxge_hw_list; hw_p; hw_p = hw_p->next) {
		NXGE_DEBUG_MSG((nxgep, MOD_CTL,
		    "==> nxge_init_common_device:func # %d "
		    "hw_p $%p parent dip $%p",
		    nxgep->function_num,
		    hw_p,
		    p_dip));
		if (hw_p->parent_devp == p_dip) {
			nxgep->nxge_hw_p = hw_p;
			hw_p->ndevs++;
			hw_p->nxge_p[nxgep->function_num] = nxgep;
			NXGE_DEBUG_MSG((nxgep, MOD_CTL,
			    "==> nxge_init_common_device:func # %d "
			    "hw_p $%p parent dip $%p "
			    "ndevs %d (found)",
			    nxgep->function_num,
			    hw_p,
			    p_dip,
			    hw_p->ndevs));
			break;
		}
	}

	if (hw_p == NULL) {
		NXGE_DEBUG_MSG((nxgep, MOD_CTL,
		    "==> nxge_init_common_device:func # %d "
		    "parent dip $%p (new)",
		    nxgep->function_num,
		    p_dip));
		hw_p = kmem_zalloc(sizeof (nxge_hw_list_t), KM_SLEEP);
		hw_p->parent_devp = p_dip;
		hw_p->magic = NXGE_NEPTUNE_MAGIC;
		nxgep->nxge_hw_p = hw_p;
		hw_p->ndevs++;
		hw_p->nxge_p[nxgep->function_num] = nxgep;
		hw_p->next = nxge_hw_list;
		if (nxgep->niu_type == N2_NIU) {
			hw_p->niu_type = N2_NIU;
			hw_p->platform_type = P_NEPTUNE_NIU;
		} else {
			hw_p->niu_type = NIU_TYPE_NONE;
			hw_p->platform_type = P_NEPTUNE_NONE;
		}

		MUTEX_INIT(&hw_p->nxge_cfg_lock, NULL, MUTEX_DRIVER, NULL);
		MUTEX_INIT(&hw_p->nxge_tcam_lock, NULL, MUTEX_DRIVER, NULL);
		MUTEX_INIT(&hw_p->nxge_vlan_lock, NULL, MUTEX_DRIVER, NULL);
		MUTEX_INIT(&hw_p->nxge_mdio_lock, NULL, MUTEX_DRIVER, NULL);

		nxge_hw_list = hw_p;

		(void) nxge_scan_ports_phy(nxgep, nxge_hw_list);
	}

	MUTEX_EXIT(&nxge_common_lock);

	nxgep->platform_type = hw_p->platform_type;
	if (nxgep->niu_type != N2_NIU) {
		nxgep->niu_type = hw_p->niu_type;
	}

	NXGE_DEBUG_MSG((nxgep, MOD_CTL,
	    "==> nxge_init_common_device (nxge_hw_list) $%p",
	    nxge_hw_list));
	NXGE_DEBUG_MSG((nxgep, MOD_CTL, "<== nxge_init_common_device"));

	return (NXGE_OK);
}

static void
nxge_uninit_common_dev(p_nxge_t nxgep)
{
	p_nxge_hw_list_t	hw_p, h_hw_p;
	p_nxge_dma_pt_cfg_t	p_dma_cfgp;
	p_nxge_hw_pt_cfg_t	p_cfgp;
	dev_info_t 		*p_dip;

	NXGE_DEBUG_MSG((nxgep, MOD_CTL, "==> nxge_uninit_common_device"));
	if (nxgep->nxge_hw_p == NULL) {
		NXGE_DEBUG_MSG((nxgep, MOD_CTL,
		    "<== nxge_uninit_common_device (no common)"));
		return;
	}

	MUTEX_ENTER(&nxge_common_lock);
	h_hw_p = nxge_hw_list;
	for (hw_p = nxge_hw_list; hw_p; hw_p = hw_p->next) {
		p_dip = hw_p->parent_devp;
		if (nxgep->nxge_hw_p == hw_p &&
		    p_dip == nxgep->p_dip &&
		    nxgep->nxge_hw_p->magic == NXGE_NEPTUNE_MAGIC &&
		    hw_p->magic == NXGE_NEPTUNE_MAGIC) {

			NXGE_DEBUG_MSG((nxgep, MOD_CTL,
			    "==> nxge_uninit_common_device:func # %d "
			    "hw_p $%p parent dip $%p "
			    "ndevs %d (found)",
			    nxgep->function_num,
			    hw_p,
			    p_dip,
			    hw_p->ndevs));

			/*
			 * Release the RDC table, a shared resoruce
			 * of the nxge hardware.  The RDC table was
			 * assigned to this instance of nxge in
			 * nxge_use_cfg_dma_config().
			 */
			p_dma_cfgp = (p_nxge_dma_pt_cfg_t)&nxgep->pt_config;
			p_cfgp = (p_nxge_hw_pt_cfg_t)&p_dma_cfgp->hw_config;
			(void) nxge_fzc_rdc_tbl_unbind(nxgep,
			    p_cfgp->def_mac_rxdma_grpid);

			if (hw_p->ndevs) {
				hw_p->ndevs--;
			}
			hw_p->nxge_p[nxgep->function_num] = NULL;
			if (!hw_p->ndevs) {
				MUTEX_DESTROY(&hw_p->nxge_vlan_lock);
				MUTEX_DESTROY(&hw_p->nxge_tcam_lock);
				MUTEX_DESTROY(&hw_p->nxge_cfg_lock);
				MUTEX_DESTROY(&hw_p->nxge_mdio_lock);
				NXGE_DEBUG_MSG((nxgep, MOD_CTL,
				    "==> nxge_uninit_common_device: "
				    "func # %d "
				    "hw_p $%p parent dip $%p "
				    "ndevs %d (last)",
				    nxgep->function_num,
				    hw_p,
				    p_dip,
				    hw_p->ndevs));

				nxge_hio_uninit(nxgep);

				if (hw_p == nxge_hw_list) {
					NXGE_DEBUG_MSG((nxgep, MOD_CTL,
					    "==> nxge_uninit_common_device:"
					    "remove head func # %d "
					    "hw_p $%p parent dip $%p "
					    "ndevs %d (head)",
					    nxgep->function_num,
					    hw_p,
					    p_dip,
					    hw_p->ndevs));
					nxge_hw_list = hw_p->next;
				} else {
					NXGE_DEBUG_MSG((nxgep, MOD_CTL,
					    "==> nxge_uninit_common_device:"
					    "remove middle func # %d "
					    "hw_p $%p parent dip $%p "
					    "ndevs %d (middle)",
					    nxgep->function_num,
					    hw_p,
					    p_dip,
					    hw_p->ndevs));
					h_hw_p->next = hw_p->next;
				}

				nxgep->nxge_hw_p = NULL;
				KMEM_FREE(hw_p, sizeof (nxge_hw_list_t));
			}
			break;
		} else {
			h_hw_p = hw_p;
		}
	}

	MUTEX_EXIT(&nxge_common_lock);
	NXGE_DEBUG_MSG((nxgep, MOD_CTL,
	    "==> nxge_uninit_common_device (nxge_hw_list) $%p",
	    nxge_hw_list));

	NXGE_DEBUG_MSG((nxgep, MOD_CTL, "<= nxge_uninit_common_device"));
}

/*
 * Determines the number of ports from the niu_type or the platform type.
 * Returns the number of ports, or returns zero on failure.
 */

int
nxge_get_nports(p_nxge_t nxgep)
{
	int	nports = 0;

	switch (nxgep->niu_type) {
	case N2_NIU:
	case NEPTUNE_2_10GF:
		nports = 2;
		break;
	case NEPTUNE_4_1GC:
	case NEPTUNE_2_10GF_2_1GC:
	case NEPTUNE_1_10GF_3_1GC:
	case NEPTUNE_1_1GC_1_10GF_2_1GC:
	case NEPTUNE_2_10GF_2_1GRF:
		nports = 4;
		break;
	default:
		switch (nxgep->platform_type) {
		case P_NEPTUNE_NIU:
		case P_NEPTUNE_ATLAS_2PORT:
			nports = 2;
			break;
		case P_NEPTUNE_ATLAS_4PORT:
		case P_NEPTUNE_MARAMBA_P0:
		case P_NEPTUNE_MARAMBA_P1:
		case P_NEPTUNE_ALONSO:
			nports = 4;
			break;
		default:
			break;
		}
		break;
	}

	return (nports);
}

/*
 * The following two functions are to support
 * PSARC/2007/453 MSI-X interrupt limit override.
 */
static int
nxge_create_msi_property(p_nxge_t nxgep)
{
	int	nmsi;
	extern	int ncpus;

	NXGE_DEBUG_MSG((nxgep, MOD_CTL, "==>nxge_create_msi_property"));

	switch (nxgep->mac.portmode) {
	case PORT_10G_COPPER:
	case PORT_10G_FIBER:
	case PORT_10G_TN1010:
		(void) ddi_prop_create(DDI_DEV_T_NONE, nxgep->dip,
		    DDI_PROP_CANSLEEP, "#msix-request", NULL, 0);
		/*
		 * The maximum MSI-X requested will be 8.
		 * If the # of CPUs is less than 8, we will reqeust
		 * # MSI-X based on the # of CPUs.
		 */
		if (ncpus >= NXGE_MSIX_REQUEST_10G) {
			nmsi = NXGE_MSIX_REQUEST_10G;
		} else {
			nmsi = ncpus;
		}
		NXGE_DEBUG_MSG((nxgep, MOD_CTL,
		    "==>nxge_create_msi_property(10G): exists 0x%x (nmsi %d)",
		    ddi_prop_exists(DDI_DEV_T_NONE, nxgep->dip,
		    DDI_PROP_CANSLEEP, "#msix-request"), nmsi));
		break;

	default:
		nmsi = NXGE_MSIX_REQUEST_1G;
		NXGE_DEBUG_MSG((nxgep, MOD_CTL,
		    "==>nxge_create_msi_property(1G): exists 0x%x (nmsi %d)",
		    ddi_prop_exists(DDI_DEV_T_NONE, nxgep->dip,
		    DDI_PROP_CANSLEEP, "#msix-request"), nmsi));
		break;
	}

	NXGE_DEBUG_MSG((nxgep, MOD_CTL, "<==nxge_create_msi_property"));
	return (nmsi);
}

/* ARGSUSED */
static int
nxge_get_def_val(nxge_t *nxgep, mac_prop_id_t pr_num, uint_t pr_valsize,
    void *pr_val)
{
	int err = 0;
	link_flowctrl_t fl;

	switch (pr_num) {
	case MAC_PROP_AUTONEG:
		*(uint8_t *)pr_val = 1;
		break;
	case MAC_PROP_FLOWCTRL:
		if (pr_valsize < sizeof (link_flowctrl_t))
			return (EINVAL);
		fl = LINK_FLOWCTRL_RX;
		bcopy(&fl, pr_val, sizeof (fl));
		break;
	case MAC_PROP_ADV_1000FDX_CAP:
	case MAC_PROP_EN_1000FDX_CAP:
		*(uint8_t *)pr_val = 1;
		break;
	case MAC_PROP_ADV_100FDX_CAP:
	case MAC_PROP_EN_100FDX_CAP:
		*(uint8_t *)pr_val = 1;
		break;
	default:
		err = ENOTSUP;
		break;
	}
	return (err);
}


/*
 * The following is a software around for the Neptune hardware's
 * interrupt bugs; The Neptune hardware may generate spurious interrupts when
 * an interrupr handler is removed.
 */
#define	NXGE_PCI_PORT_LOGIC_OFFSET	0x98
#define	NXGE_PIM_RESET			(1ULL << 29)
#define	NXGE_GLU_RESET			(1ULL << 30)
#define	NXGE_NIU_RESET			(1ULL << 31)
#define	NXGE_PCI_RESET_ALL		(NXGE_PIM_RESET |	\
					NXGE_GLU_RESET |	\
					NXGE_NIU_RESET)

#define	NXGE_WAIT_QUITE_TIME		200000
#define	NXGE_WAIT_QUITE_RETRY		40
#define	NXGE_PCI_RESET_WAIT		1000000 /* one second */

static void
nxge_niu_peu_reset(p_nxge_t nxgep)
{
	uint32_t	rvalue;
	p_nxge_hw_list_t hw_p;
	p_nxge_t	fnxgep;
	int		i, j;

	NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL, "==> nxge_niu_peu_reset"));
	if ((hw_p = nxgep->nxge_hw_p) == NULL) {
		NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
		    "==> nxge_niu_peu_reset: NULL hardware pointer"));
		return;
	}

	NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL,
	    "==> nxge_niu_peu_reset: flags 0x%x link timer id %d timer id %d",
	    hw_p->flags, nxgep->nxge_link_poll_timerid,
	    nxgep->nxge_timerid));

	MUTEX_ENTER(&hw_p->nxge_cfg_lock);
	/*
	 * Make sure other instances from the same hardware
	 * stop sending PIO and in quiescent state.
	 */
	for (i = 0; i < NXGE_MAX_PORTS; i++) {
		fnxgep = hw_p->nxge_p[i];
		NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL,
		    "==> nxge_niu_peu_reset: checking entry %d "
		    "nxgep $%p", i, fnxgep));
#ifdef	NXGE_DEBUG
		if (fnxgep) {
			NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL,
			    "==> nxge_niu_peu_reset: entry %d (function %d) "
			    "link timer id %d hw timer id %d",
			    i, fnxgep->function_num,
			    fnxgep->nxge_link_poll_timerid,
			    fnxgep->nxge_timerid));
		}
#endif
		if (fnxgep && fnxgep != nxgep &&
		    (fnxgep->nxge_timerid || fnxgep->nxge_link_poll_timerid)) {
			NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL,
			    "==> nxge_niu_peu_reset: checking $%p "
			    "(function %d) timer ids",
			    fnxgep, fnxgep->function_num));
			for (j = 0; j < NXGE_WAIT_QUITE_RETRY; j++) {
				NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL,
				    "==> nxge_niu_peu_reset: waiting"));
				NXGE_DELAY(NXGE_WAIT_QUITE_TIME);
				if (!fnxgep->nxge_timerid &&
				    !fnxgep->nxge_link_poll_timerid) {
					break;
				}
			}
			NXGE_DELAY(NXGE_WAIT_QUITE_TIME);
			if (fnxgep->nxge_timerid ||
			    fnxgep->nxge_link_poll_timerid) {
				MUTEX_EXIT(&hw_p->nxge_cfg_lock);
				NXGE_ERROR_MSG((nxgep, NXGE_ERR_CTL,
				    "<== nxge_niu_peu_reset: cannot reset "
				    "hardware (devices are still in use)"));
				return;
			}
		}
	}

	if ((hw_p->flags & COMMON_RESET_NIU_PCI) != COMMON_RESET_NIU_PCI) {
		hw_p->flags |= COMMON_RESET_NIU_PCI;
		rvalue = pci_config_get32(nxgep->dev_regs->nxge_pciregh,
		    NXGE_PCI_PORT_LOGIC_OFFSET);
		NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL,
		    "nxge_niu_peu_reset: read offset 0x%x (%d) "
		    "(data 0x%x)",
		    NXGE_PCI_PORT_LOGIC_OFFSET,
		    NXGE_PCI_PORT_LOGIC_OFFSET,
		    rvalue));

		rvalue |= NXGE_PCI_RESET_ALL;
		pci_config_put32(nxgep->dev_regs->nxge_pciregh,
		    NXGE_PCI_PORT_LOGIC_OFFSET, rvalue);
		NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL,
		    "nxge_niu_peu_reset: RESETTING NIU: write NIU reset 0x%x",
		    rvalue));

		NXGE_DELAY(NXGE_PCI_RESET_WAIT);
	}

	MUTEX_EXIT(&hw_p->nxge_cfg_lock);
	NXGE_DEBUG_MSG((nxgep, NXGE_ERR_CTL, "<== nxge_niu_peu_reset"));
}

static void
nxge_set_pci_replay_timeout(p_nxge_t nxgep)
{
	p_dev_regs_t 	dev_regs;
	uint32_t	value;

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "==> nxge_set_pci_replay_timeout"));

	if (!nxge_set_replay_timer) {
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "==> nxge_set_pci_replay_timeout: will not change "
		    "the timeout"));
		return;
	}

	dev_regs = nxgep->dev_regs;
	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "==> nxge_set_pci_replay_timeout: dev_regs 0x%p pcireg 0x%p",
	    dev_regs, dev_regs->nxge_pciregh));

	if (dev_regs == NULL || (dev_regs->nxge_pciregh == NULL)) {
		NXGE_DEBUG_MSG((nxgep, DDI_CTL,
		    "==> nxge_set_pci_replay_timeout: NULL dev_regs $%p or "
		    "no PCI handle",
		    dev_regs));
		return;
	}
	value = (pci_config_get32(dev_regs->nxge_pciregh,
	    PCI_REPLAY_TIMEOUT_CFG_OFFSET) |
	    (nxge_replay_timeout << PCI_REPLAY_TIMEOUT_SHIFT));

	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "nxge_set_pci_replay_timeout: replay timeout value before set 0x%x "
	    "(timeout value to set 0x%x at offset 0x%x) value 0x%x",
	    pci_config_get32(dev_regs->nxge_pciregh,
	    PCI_REPLAY_TIMEOUT_CFG_OFFSET), nxge_replay_timeout,
	    PCI_REPLAY_TIMEOUT_CFG_OFFSET, value));

	pci_config_put32(dev_regs->nxge_pciregh, PCI_REPLAY_TIMEOUT_CFG_OFFSET,
	    value);

	NXGE_DEBUG_MSG((nxgep, DDI_CTL,
	    "nxge_set_pci_replay_timeout: replay timeout value after set 0x%x",
	    pci_config_get32(dev_regs->nxge_pciregh,
	    PCI_REPLAY_TIMEOUT_CFG_OFFSET)));

	NXGE_DEBUG_MSG((nxgep, DDI_CTL, "<== nxge_set_pci_replay_timeout"));
}
