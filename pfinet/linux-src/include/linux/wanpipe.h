/*****************************************************************************
* wanpipe.h	WANPIPE(tm) Multiprotocol WAN Link Driver.
*		User-level API definitions.
*
* Author:	Gene Kozin	<genek@compuserve.com>
*		Jaspreet Singh	<jaspreet@sangoma.com>
*
* Copyright:	(c) 1995-1997 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Nov 26, 1997	Jaspreet Singh	Added 'load_sharing' structure.  Also added 
*				'devs_struct','dev_to_devtint_next' to 'sdla_t'	
* Nov 24, 1997	Jaspreet Singh	Added 'irq_dis_if_send_count', 
*				'irq_dis_poll_count' to 'sdla_t'.
* Nov 06, 1997	Jaspreet Singh	Added a define called 'INTR_TEST_MODE'
* Oct 20, 1997	Jaspreet Singh	Added 'buff_intr_mode_unbusy' and 
*				'dlci_intr_mode_unbusy' to 'sdla_t'
* Oct 18, 1997	Jaspreet Singh	Added structure to maintain global driver
*				statistics.
* Jan 15, 1997	Gene Kozin	Version 3.1.0
*				 o added UDP management stuff
* Jan 02, 1997	Gene Kozin	Version 3.0.0
*****************************************************************************/
#ifndef	_WANPIPE_H
#define	_WANPIPE_H

#include <linux/wanrouter.h>

/* Defines */

#ifndef	PACKED
#define	PACKED	__attribute__((packed))
#endif

#define	WANPIPE_MAGIC	0x414C4453L	/* signatire: 'SDLA' reversed */

/* IOCTL numbers (up to 16) */
#define	WANPIPE_DUMP	(ROUTER_USER+0)	/* dump adapter's memory */
#define	WANPIPE_EXEC	(ROUTER_USER+1)	/* execute firmware command */

/*
 * Data structures for IOCTL calls.
 */

typedef struct sdla_dump	/* WANPIPE_DUMP */
{
	unsigned long magic;	/* for verification */
	unsigned long offset;	/* absolute adapter memory address */
	unsigned long length;	/* block length */
	void* ptr;		/* -> buffer */
} sdla_dump_t;

typedef struct sdla_exec	/* WANPIPE_EXEC */
{
	unsigned long magic;	/* for verification */
	void* cmd;		/* -> command structure */
	void* data;		/* -> data buffer */
} sdla_exec_t;

/* UDP management stuff */

typedef struct wum_header
{
	unsigned char signature[8];	/* 00h: signature */
	unsigned char type;		/* 08h: request/reply */
	unsigned char command;		/* 09h: commnand */
	unsigned char reserved[6];	/* 0Ah: reserved */
} wum_header_t;

/*************************************************************************
 Data Structure for global statistics
*************************************************************************/

typedef struct global_stats
{
	unsigned long isr_entry;
	unsigned long isr_already_critical;		
	unsigned long isr_rx;
	unsigned long isr_tx;
	unsigned long isr_intr_test;
	unsigned long isr_spurious;
	unsigned long isr_enable_tx_int;
	unsigned long rx_intr_corrupt_rx_bfr;
	unsigned long rx_intr_on_orphaned_DLCI;
	unsigned long rx_intr_dev_not_started;
	unsigned long tx_intr_dev_not_started;
	unsigned long poll_entry;
	unsigned long poll_already_critical;
	unsigned long poll_processed;
	unsigned long poll_tbusy_bad_status;
	unsigned long poll_host_disable_irq;
	unsigned long poll_host_enable_irq;

} global_stats_t;

/* This structure is used for maitaining a circular linked list of all
 * interfaces(devices) per card. It is used in the Interrupt Service routine
 * for a transmit interrupt where the start of the loop to dev_tint all
 * interfaces changes.
 */
typedef struct load_sharing
{
        struct device*  dev_ptr;
        struct load_sharing* next;
} load_sharing_t;

/* This is used for interrupt testing */
#define INTR_TEST_MODE	0x02

#define	WUM_SIGNATURE_L	0x50495046
#define	WUM_SIGNATURE_H	0x444E3845

#define	WUM_KILL	0x50
#define	WUM_EXEC	0x51

#ifdef	__KERNEL__
/****** Kernel Interface ****************************************************/

#include <linux/sdladrv.h>	/* SDLA support module API definitions */
#include <linux/sdlasfm.h>	/* SDLA firmware module definitions */

#ifndef	min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef	max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

#define	is_digit(ch) (((ch)>=(unsigned)'0'&&(ch)<=(unsigned)'9')?1:0)
#define	is_alpha(ch) ((((ch)>=(unsigned)'a'&&(ch)<=(unsigned)'z')||\
	 	  ((ch)>=(unsigned)'A'&&(ch)<=(unsigned)'Z'))?1:0)
#define	is_hex_digit(ch) ((((ch)>=(unsigned)'0'&&(ch)<=(unsigned)'9')||\
	 	  ((ch)>=(unsigned)'a'&&(ch)<=(unsigned)'f')||\
	 	  ((ch)>=(unsigned)'A'&&(ch)<=(unsigned)'F'))?1:0)

/****** Data Structures *****************************************************/

/* Adapter Data Space.
 * This structure is needed because we handle multiple cards, otherwise
 * static data would do it.
 */
typedef struct sdla
{
	char devname[WAN_DRVNAME_SZ+1];	/* card name */
	sdlahw_t hw;			/* hardware configuration */
	wan_device_t wandev;		/* WAN device data space */
	unsigned open_cnt;		/* number of open interfaces */
	unsigned long state_tick;	/* link state timestamp */
	unsigned intr_mode;		/* Type of Interrupt Mode */
	char in_isr;			/* interrupt-in-service flag */
	char buff_int_mode_unbusy;	/* flag for carrying out dev_tint */  
	char dlci_int_mode_unbusy;	/* flag for carrying out dev_tint */
	unsigned short irq_dis_if_send_count; /* Disabling irqs in if_send*/
	unsigned short irq_dis_poll_count;   /* Disabling irqs in poll routine*/
	global_stats_t statistics;	/* global statistics */
	
	/* The following is used as  a pointer to the structure in our 
	   circular linked list which changes the start of the loop for 
	   dev_tint of all interfaces */
	
	load_sharing_t* dev_to_devtint_next;
	load_sharing_t* devs_struct;	

	void* mbox;			/* -> mailbox */
	void* rxmb;			/* -> receive mailbox */
	void* flags;			/* -> adapter status flags */
	void (*isr)(struct sdla* card);	/* interrupt service routine */
	void (*poll)(struct sdla* card); /* polling routine */
	int (*exec)(struct sdla* card, void* u_cmd, void* u_data);
	union
	{
		struct
		{			/****** X.25 specific data **********/
			unsigned lo_pvc;
			unsigned hi_pvc;
			unsigned lo_svc;
			unsigned hi_svc;
		} x;
		struct
		{			/****** frame relay specific data ***/
			void* rxmb_base;	/* -> first Rx buffer */
			void* rxmb_last;	/* -> last Rx buffer */
			unsigned rx_base;	/* S508 receive buffer base */
			unsigned rx_top;	/* S508 receive buffer end */
			unsigned short node_dlci[100];
			unsigned short dlci_num;
		} f;
		struct			/****** PPP-specific data ***********/
		{
			char if_name[WAN_IFNAME_SZ+1];	/* interface name */
			void* txbuf;		/* -> current Tx buffer */
			void* txbuf_base;	/* -> first Tx buffer */
			void* txbuf_last;	/* -> last Tx buffer */
			void* rxbuf_base;	/* -> first Rx buffer */
			void* rxbuf_last;	/* -> last Rx buffer */
			unsigned rx_base;	/* S508 receive buffer base */
			unsigned rx_top;	/* S508 receive buffer end */
		} p;
	} u;
} sdla_t;

/****** Public Functions ****************************************************/

void wanpipe_open      (sdla_t* card);			/* wpmain.c */
void wanpipe_close     (sdla_t* card);			/* wpmain.c */
void wanpipe_set_state (sdla_t* card, int state);	/* wpmain.c */

int wpx_init (sdla_t* card, wandev_conf_t* conf);	/* wpx.c */
int wpf_init (sdla_t* card, wandev_conf_t* conf);	/* wpf.c */
int wpp_init (sdla_t* card, wandev_conf_t* conf);	/* wpp.c */

#endif	/* __KERNEL__ */
#endif	/* _WANPIPE_H */

