/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  NET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the ARCnet handlers.
 *
 * Version:	$Id: arcdevice.h,v 1.3 1997/11/09 11:05:05 mj Exp $
 *
 * Authors:	Avery Pennarun <apenwarr@bond.net>
 *              David Woodhouse <dwmw2@cam.ac.uk>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */
#ifndef _LINUX_ARCDEVICE_H
#define _LINUX_ARCDEVICE_H

#include <linux/config.h>
#include <linux/if_arcnet.h>

#ifdef __KERNEL__

#define ARC_20020     1
#define ARC_RIM_I     2
#define ARC_90xx      3
#define ARC_90xx_IO   4

#define MAX_ARCNET_DEVS 8


/* The card sends the reconfiguration signal when it loses the connection to
 * the rest of its network. It is a 'Hello, is anybody there?' cry.  This
 * usually happens when a new computer on the network is powered on or when
 * the cable is broken.
 *
 * Define DETECT_RECONFIGS if you want to detect network reconfigurations.
 * Recons may be a real nuisance on a larger ARCnet network; if you are a
 * network administrator you probably would like to count them.
 * Reconfigurations will be recorded in stats.tx_carrier_errors (the last
 * field of the /proc/net/dev file).
 *
 * Define SHOW_RECONFIGS if you really want to see a log message whenever
 * a RECON occurs.
 */
#define DETECT_RECONFIGS
#undef SHOW_RECONFIGS


/* RECON_THRESHOLD is the maximum number of RECON messages to receive within
 * one minute before printing a "cabling problem" warning.  You must have
 * DETECT_RECONFIGS enabled if you want to use this.  The default value
 * should be fine.
 *
 * After that, a "cabling restored" message will be printed on the next IRQ
 * if no RECON messages have been received for 10 seconds.
 *
 * Do not define RECON_THRESHOLD at all if you want to disable this feature.
 */
#define RECON_THRESHOLD 30


/* Define this to the minimum "timeout" value.  If a transmit takes longer
 * than TX_TIMEOUT jiffies, Linux will abort the TX and retry.  On a large
 * network, or one with heavy network traffic, this timeout may need to be
 * increased.  The larger it is, though, the longer it will be between
 * necessary transmits - don't set this too large.
 */
#define TX_TIMEOUT (20*HZ/100)


/* Display warnings about the driver being an ALPHA version.
 */
#undef ALPHA_WARNING


/* New debugging bitflags: each option can be enabled individually.
 *
 * These can be set while the driver is running by typing:
 *	ifconfig arc0 down metric 1xxx HOSTNAME
 *		where 1xxx is 1000 + the debug level you want
 *		and HOSTNAME is your hostname/ip address
 * and then resetting your routes.
 *
 * An ioctl() should be used for this instead, someday.
 *
 * Note: only debug flags included in the ARCNET_DEBUG_MAX define will
 *   actually be available.  GCC will (at least, GCC 2.7.0 will) notice
 *   lines using a BUGLVL not in ARCNET_DEBUG_MAX and automatically optimize
 *   them out.
 */
#define D_NORMAL	1	/* important operational info		*/
#define D_EXTRA		2	/* useful, but non-vital information	*/
#define	D_INIT		4	/* show init/probe messages		*/
#define D_INIT_REASONS	8	/* show reasons for discarding probes	*/
/* debug levels below give LOTS of output during normal operation! */
#define D_DURING	16	/* trace operations (including irq's)	*/
#define D_TX		32	/* show tx packets			*/
#define D_RX		64	/* show rx packets			*/
#define D_SKB		128	/* show skb's				*/

#ifndef ARCNET_DEBUG_MAX
#define ARCNET_DEBUG_MAX (~0)		/* enable ALL debug messages	 */
#endif

#ifndef ARCNET_DEBUG
#define ARCNET_DEBUG (D_NORMAL|D_EXTRA)
#endif
extern int arcnet_debug;

/* macros to simplify debug checking */
#define BUGLVL(x) if ((ARCNET_DEBUG_MAX)&arcnet_debug&(x))
#define BUGMSG2(x,msg,args...) do { BUGLVL(x) printk(msg, ## args); } while (0)
#define BUGMSG(x,msg,args...) \
	BUGMSG2(x,"%s%6s: " msg, \
            x==D_NORMAL	? KERN_WARNING : \
      x<=D_INIT_REASONS	? KERN_INFO    : KERN_DEBUG , \
	dev->name , ## args)


#define SETMASK AINTMASK(lp->intmask)

	/* Time needed to resetthe card - in jiffies.  This works on my SMC
	 * PC100.  I can't find a reference that tells me just how long I
	 * should wait.
	 */
#define RESETtime (HZ * 3 / 10)		/* reset */

	/* these are the max/min lengths of packet data. (including
	 * ClientData header)
	 * note: packet sizes 250, 251, 252 are impossible (God knows why)
	 *  so exception packets become necessary.
	 *
	 * These numbers are compared with the length of the full packet,
	 * including ClientData header.
	 */
#define MTU	253	/* normal packet max size */
#define MinTU	257	/* extended packet min size */
#define XMTU	508	/* extended packet max size */

	/* status/interrupt mask bit fields */
#define TXFREEflag	0x01            /* transmitter available */
#define TXACKflag       0x02            /* transmitted msg. ackd */
#define RECONflag       0x04            /* system reconfigured */
#define TESTflag        0x08            /* test flag */
#define RESETflag       0x10            /* power-on-reset */
#define RES1flag        0x20            /* reserved - usually set by jumper */
#define RES2flag        0x40            /* reserved - usually set by jumper */
#define NORXflag        0x80            /* receiver inhibited */

       /* Flags used for IO-mapped memory operations */
#define AUTOINCflag     0x40    /* Increase location with each access */
#define IOMAPflag       0x02    /* (for 90xx) Use IO mapped memory, not mmap */
#define ENABLE16flag    0x80    /* (for 90xx) Enable 16-bit mode */

       /* in the command register, the following bits have these meanings:
        *                0-2     command
        *                3-4     page number (for enable rcv/xmt command)
        *                 7      receive broadcasts
        */
#define NOTXcmd         0x01            /* disable transmitter */
#define NORXcmd         0x02            /* disable receiver */
#define TXcmd           0x03            /* enable transmitter */
#define RXcmd           0x04            /* enable receiver */
#define CONFIGcmd       0x05            /* define configuration */
#define CFLAGScmd       0x06            /* clear flags */
#define TESTcmd         0x07            /* load test flags */

       /* flags for "clear flags" command */
#define RESETclear      0x08            /* power-on-reset */
#define CONFIGclear     0x10            /* system reconfigured */

	/* flags for "load test flags" command */
#define TESTload        0x08            /* test flag (diagnostic) */

	/* byte deposited into first address of buffers on reset */
#define TESTvalue       0321		 /* that's octal for 0xD1 :) */

	/* for "enable receiver" command */
#define RXbcasts        0x80            /* receive broadcasts */

	/* flags for "define configuration" command */
#define NORMALconf      0x00            /* 1-249 byte packets */
#define EXTconf         0x08            /* 250-504 byte packets */

	/* Starts receiving packets into recbuf.
	 */
#define EnableReceiver()	ACOMMAND(RXcmd|(recbuf<<3)|RXbcasts)



#define JIFFER(time) for (delayval=jiffies+time; time_before(jiffies,delayval);) ;

	/* a complete ARCnet packet */
union ArcPacket
{
	struct archdr hardheader;	/* the hardware header */
	u_char raw[512];		/* raw packet info, incl ClientData */
};


	/* the "client data" header - RFC1201 information
	 * notice that this screws up if it's not an even number of bytes
	 * <sigh>
	 */
struct ClientData
{
	/* data that's NOT part of real packet - we MUST get rid of it before
	 * actually sending!!
	 */
	u_char  saddr,		/* Source address - needed for IPX */
		daddr;		/* Destination address */

	/* data that IS part of real packet */
	u_char	protocol_id,	/* ARC_P_IP, ARC_P_ARP, etc */
		split_flag;	/* for use with split packets */
	u_short	sequence;	/* sequence number */
};
#define EXTRA_CLIENTDATA (sizeof(struct ClientData)-4)


	/* the "client data" header - RFC1051 information
	 * this also screws up if it's not an even number of bytes
	 * <sigh again>
	 */
struct S_ClientData
{
	/* data that's NOT part of real packet - we MUST get rid of it before
	 * actually sending!!
	 */
	u_char  saddr,		/* Source address - needed for IPX */
		daddr,		/* Destination address */
		junk;		/* padding to make an even length */

	/* data that IS part of real packet */
	u_char	protocol_id;	/* ARC_P_IP, ARC_P_ARP, etc */
};
#define S_EXTRA_CLIENTDATA (sizeof(struct S_ClientData)-1)


/* "Incoming" is information needed for each address that could be sending
 * to us.  Mostly for partially-received split packets.
 */
struct Incoming
{
	struct sk_buff *skb;		/* packet data buffer             */
	unsigned char lastpacket,	/* number of last packet (from 1) */
		      numpackets;	/* number of packets in split     */
	u_short sequence;		/* sequence number of assembly	  */
};

struct Outgoing
{
	struct sk_buff *skb;		/* buffer from upper levels */
	struct ClientData *hdr;		/* clientdata of last packet */
	u_char *data;			/* pointer to data in packet */
	short length,			/* bytes total */
	      dataleft,			/* bytes left */
	      segnum,			/* segment being sent */
	      numsegs,			/* number of segments */
	      seglen;			/* length of segment */
};


struct arcnet_local {
  struct net_device_stats stats;
  u_short sequence;	/* sequence number (incs with each packet) */
  u_short aborted_seq;
  u_char stationid,	/* our 8-bit station address */
    recbuf,		/* receive buffer # (0 or 1) */
    txbuf,		/* transmit buffer # (2 or 3) */
    txready,		/* buffer where a packet is ready to send */
    config,		/* current value of CONFIG register */
    timeout,		/* Extended timeout for COM20020 */
    backplane,		/* Backplane flag for COM20020 */     
    setup,		/* Contents of setup register */
    intmask;		/* current value of INTMASK register */
  short intx,		/* in TX routine? */
    in_txhandler,	/* in TX_IRQ handler? */
    sending,		/* transmit in progress? */
    lastload_dest,	/* can last loaded packet be acked? */
    lasttrans_dest;	/* can last TX'd packet be acked? */
  
#if defined(DETECT_RECONFIGS) && defined(RECON_THRESHOLD)
  time_t first_recon,	/* time of "first" RECON message to count */
    last_recon;		/* time of most recent RECON */
  int num_recons,	/* number of RECONs between first and last. */
    network_down;	/* do we think the network is down? */
#endif
  
  struct timer_list timer; /* the timer interrupt struct */
  struct Incoming incoming[256];	/* one from each address */
  struct Outgoing outgoing; /* packet currently being sent */
  
  int card_type;
  char *card_type_str;
  
  void (*inthandler) (struct device *dev);
  int (*arcnet_reset) (struct device *dev, int reset_delay);
  void (*asetmask) (struct device *dev, u_char mask);
  void (*acommand) (struct device *dev, u_char command);
  u_char (*astatus) (struct device *dev);
  void (*en_dis_able_TX) (struct device *dev, int enable); 
  void (*prepare_tx)(struct device *dev,u_char *hdr,int hdrlen,
		     char *data,int length,int daddr,int exceptA, int offset);
  void (*openclose_device)(int open);  
  
  struct device *adev;	/* RFC1201 protocol device */
  
  /* These are last to ensure that the chipset drivers don't depend on the
   * CONFIG_ARCNET_ETH and CONFIG_ARCNET_1051 options. 
   */
  
#ifdef CONFIG_ARCNET_ETH
  struct device *edev;	/* Ethernet-Encap device */
#endif
  
#ifdef CONFIG_ARCNET_1051
  struct device *sdev;	/* RFC1051 protocol device */
#endif
};

/* Functions exported by arcnet.c
 */

#if ARCNET_DEBUG_MAX & D_SKB
extern void arcnet_dump_skb(struct device *dev,struct sk_buff *skb,
			    char *desc);
#else
#define arcnet_dump_skb(dev,skb,desc) ;
#endif

#if (ARCNET_DEBUG_MAX & D_RX) || (ARCNET_DEBUG_MAX & D_TX)
extern void arcnet_dump_packet(struct device *dev,u_char *buffer,int ext,
			       char *desc);
#else
#define arcnet_dump_packet(dev,buffer,ext,desc) ;
#endif

extern void arcnet_tx_done(struct device *dev, struct arcnet_local *lp);
extern void arcnet_makename(char *device);
extern void arcnet_interrupt(int irq,void *dev_id,struct pt_regs *regs);
extern void arcnet_setup(struct device *dev);
extern int arcnet_go_tx(struct device *dev,int enable_irq);
extern void arcnetA_continue_tx(struct device *dev);
extern void arcnet_rx(struct arcnet_local *lp, u_char *arcsoft, short length, int saddr, int daddr);
extern void arcnet_use_count(int open);


#endif  /* __KERNEL__ */
#endif	/* _LINUX_ARCDEVICE_H */
