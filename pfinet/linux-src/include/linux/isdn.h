/* $Id: isdn.h,v 1.81 1999/10/27 21:21:18 detabc Exp $
 *
 * Main header for the Linux ISDN subsystem (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: isdn.h,v $
 * Revision 1.81  1999/10/27 21:21:18  detabc
 * Added support for building logically-bind-group's per interface.
 * useful for outgoing call's with more then one isdn-card.
 *
 * Switchable support to dont reset the hangup-timeout for
 * receive frames. Most part's of the timru-rules for receiving frames
 * are now obsolete. If the input- or forwarding-firewall deny
 * the frame, the line will be not hold open.
 *
 * Revision 1.80  1999/10/26 21:09:29  armin
 * New bufferlen for phonenumber only with kernel 2.3.x
 *
 * Revision 1.79  1999/10/16 17:52:38  keil
 * Changing the MSN length need new data versions
 *
 * Revision 1.78  1999/10/08 18:59:33  armin
 * Bugfix of too small MSN buffer and checking phone number
 * in isdn_tty_getdial()
 *
 * Revision 1.77  1999/09/23 22:22:42  detabc
 * added tcp-keepalive-detect with local response (ipv4 only)
 * added host-only-interface support
 * (source ipaddr == interface ipaddr) (ipv4 only)
 * ok with kernel 2.3.18 and 2.2.12
 *
 * Revision 1.76  1999/09/14 10:16:21  keil
 * change ABC include
 *
 * Revision 1.75  1999/09/13 23:25:17  he
 * serialized xmitting frames from isdn_ppp and BSENT statcallb
 *
 * Revision 1.74  1999/09/12 16:19:39  detabc
 * added abc features
 * low cost routing for net-interfaces (only the HL side).
 * need more implementation in the isdnlog-utility
 * udp info support (first part).
 * different EAZ on outgoing call's.
 * more checks on D-Channel callbacks (double use of channels).
 * tested and running with kernel 2.3.17
 *
 * Revision 1.73  1999/09/06 07:29:36  fritz
 * Changed my mail-address.
 *
 * Revision 1.72  1999/09/04 22:20:19  detabc
 *
 * Revision 1.71  1999/08/23 15:54:22  keil
 * more backported changes from kernel 2.3.14
 *
 * Revision 1.70  1999/07/31 12:59:58  armin
 * Added tty fax capabilities.
 *
 * Revision 1.69  1999/07/13 20:47:53  werner
 * added channel bit ISDN_USAGE_DISABLED for limiting b-channel access.
 *
 * Revision 1.68  1999/07/11 17:07:37  armin
 * Added tty modem register S23.
 * Added new layer 2 and 3 protocols for Fax and DSP functions.
 *
 * Revision 1.67  1999/07/07 10:17:24  detabc
 * remove unused messages
 *
 * Revision 1.66  1999/07/01 08:35:37  keil
 * compatibility to 2.3
 *
 * Revision 1.65  1999/06/10 11:51:27  paul
 * fixed comment for NET_DV
 *
 * Revision 1.64  1999/04/18 14:57:14  fritz
 * Removed TIMRU stuff
 *
 * Revision 1.63  1999/04/18 14:07:18  fritz
 * Removed TIMRU stuff.
 *
 * Revision 1.62  1999/04/12 13:16:54  fritz
 * Changes from 2.0 tree.
 *
 * Revision 1.61  1999/03/02 11:43:21  armin
 * Added variable to store connect-message of Modem.
 * Added Timer-define for RegS7 (Wait for Carrier).
 *
 * Revision 1.60  1998/10/25 14:50:29  fritz
 * Backported from MIPS (Cobalt).
 *
 * Revision 1.59  1998/10/23 10:18:55  paul
 * Implementation of "dialmode" (successor of "status")
 * You also need current isdnctrl for this!
 *
 * Revision 1.58  1998/10/23 10:10:06  fritz
 * Test-Checkin
 *
 * Revision 1.57  1998/08/31 21:10:01  he
 * new ioctl IIOCNETGPN for /dev/isdninfo (get network interface'
 *     peer phone number)
 *
 * Revision 1.56  1998/07/26 18:46:52  armin
 * Added silence detection in voice receive mode.
 *
 * Revision 1.55  1998/06/26 15:13:17  fritz
 * Added handling of STAT_ICALL with incomplete CPN.
 * Added AT&L for ttyI emulator.
 * Added more locking stuff in tty_write.
 *
 * Revision 1.54  1998/06/18 23:32:01  fritz
 * Replaced cli()/restore_flags() in isdn_tty_write() by locking.
 * Removed direct-senddown feature in isdn_tty_write because it will
 * never succeed with locking and is useless anyway.
 *
 * Revision 1.53  1998/06/17 19:51:51  he
 * merged with 2.1.10[34] (cosmetics and udelay() -> mdelay())
 * brute force fix to avoid Ugh's in isdn_tty_write()
 * cleaned up some dead code
 *
 * Revision 1.46  1998/04/14 16:28:59  he
 * Fixed user space access with interrupts off and remaining
 * copy_{to,from}_user() -> -EFAULT return codes
 *
 * Revision 1.45  1998/03/24 16:33:12  hipp
 * More CCP changes. BSD compression now "works" on a local loopback link.
 * Moved some isdn_ppp stuff from isdn.h to isdn_ppp.h
 *
 * Revision 1.44  1998/03/22 18:50:56  hipp
 * Added BSD Compression for syncPPP .. UNTESTED at the moment
 *
 * Revision 1.43  1998/03/09 17:46:44  he
 * merged in 2.1.89 changes
 *
 *
 * Revision 1.40  1998/03/08 01:08:29  fritz
 * Increased NET_DV because of TIMRU
 *
 * Revision 1.39  1998/03/07 22:42:49  fritz
 * Starting generic module support (Nothing usable yet).
 *
 * Revision 1.38  1998/03/07 18:21:29  cal
 * Dynamic Timeout-Rule-Handling vs. 971110 included
 *
 * Revision 1.37  1998/02/22 19:45:24  fritz
 * Some changes regarding V.110
 *
 * Revision 1.36  1998/02/20 17:35:55  fritz
 * Added V.110 stuff.
 *
 * Revision 1.35  1998/01/31 22:14:14  keil
 * changes for 2.1.82
 *
 * Revision 1.34  1997/10/09 21:28:11  fritz
 * New HL<->LL interface:
 *   New BSENT callback with nr. of bytes included.
 *   Sending without ACK.
 *   New L1 error status (not yet in use).
 *   Cleaned up obsolete structures.
 * Implemented Cisco-SLARP.
 * Changed local net-interface data to be dynamically allocated.
 * Removed old 2.0 compatibility stuff.
 *
 * Revision 1.33  1997/08/21 14:44:22  fritz
 * Moved triggercps to end of struct for backwards-compatibility.
 *
 * Revision 1.32  1997/08/21 09:49:46  fritz
 * Increased NET_DV
 *
 * Revision 1.31  1997/06/22 11:57:07  fritz
 * Added ability to adjust slave triggerlevel.
 *
 * Revision 1.30  1997/06/17 13:07:23  hipp
 * compression changes , MP changes
 *
 * Revision 1.29  1997/05/27 15:18:02  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where appropriate.
 *   changed type of hard_header_cache parameter.
 *
 * Revision 1.28  1997/03/07 01:33:01  fritz
 * Added proper ifdef's for CONFIG_ISDN_AUDIO
 *
 * Revision 1.27  1997/03/05 21:11:49  fritz
 * Minor fixes.
 *
 * Revision 1.26  1997/02/28 02:37:53  fritz
 * Added some comments.
 *
 * Revision 1.25  1997/02/23 16:54:23  hipp
 * some initial changes for future PPP compresion
 *
 * Revision 1.24  1997/02/18 09:42:45  fritz
 * Bugfix: Increased ISDN_MODEM_ANZREG.
 * Increased TTY_DV.
 *
 * Revision 1.23  1997/02/10 22:07:13  fritz
 * Added 2 modem registers for numbering plan and screening info.
 *
 * Revision 1.22  1997/02/03 23:42:08  fritz
 * Added ISDN_TIMER_RINGING
 * Misc. changes for Kernel 2.1.X compatibility
 *
 * Revision 1.21  1997/01/17 01:19:10  fritz
 * Applied chargeint patch.
 *
 * Revision 1.20  1997/01/17 00:41:19  fritz
 * Increased TTY_DV.
 *
 * Revision 1.19  1997/01/14 01:41:07  fritz
 * Added ATI2 related variables.
 * Added variables for audio support in skbuffs.
 *
 * Revision 1.18  1996/11/06 17:37:50  keil
 * more changes for 2.1.X
 *
 * Revision 1.17  1996/09/07 12:53:57  hipp
 * moved a few isdn_ppp.c specific defines to drives/isdn/isdn_ppp.h
 *
 * Revision 1.16  1996/08/12 16:20:56  hipp
 * renamed ppp_minor to ppp_slot
 *
 * Revision 1.15  1996/06/15 14:56:57  fritz
 * Added version signatures for data structures used
 * by userlevel programs.
 *
 * Revision 1.14  1996/06/06 21:24:23  fritz
 * Started adding support for suspend/resume.
 *
 * Revision 1.13  1996/06/05 02:18:20  fritz
 * Added DTMF decoding stuff.
 *
 * Revision 1.12  1996/06/03 19:55:08  fritz
 * Fixed typos.
 *
 * Revision 1.11  1996/05/31 01:37:47  fritz
 * Minor changes, due to changes in isdn_tty.c
 *
 * Revision 1.10  1996/05/18 01:37:18  fritz
 * Added spelling corrections and some minor changes
 * to stay in sync with kernel.
 *
 * Revision 1.9  1996/05/17 03:58:20  fritz
 * Added flags for DLE handling.
 *
 * Revision 1.8  1996/05/11 21:49:55  fritz
 * Removed queue management variables.
 * Changed queue management to use sk_buffs.
 *
 * Revision 1.7  1996/05/07 09:10:06  fritz
 * Reorganized tty-related structs.
 *
 * Revision 1.6  1996/05/06 11:38:27  hipp
 * minor change in ippp struct
 *
 * Revision 1.5  1996/04/30 11:03:16  fritz
 * Added Michael's ippp-bind patch.
 *
 * Revision 1.4  1996/04/29 23:00:02  fritz
 * Added variables for voice-support.
 *
 * Revision 1.3  1996/04/20 16:54:58  fritz
 * Increased maximum number of channels.
 * Added some flags for isdn_net to handle callback more reliable.
 * Fixed delay-definitions to be more accurate.
 * Misc. typos
 *
 * Revision 1.2  1996/02/11 02:10:02  fritz
 * Changed IOCTL-names
 * Added rx_netdev, st_netdev, first_skb, org_hcb, and org_hcu to
 * Netdevice-local struct.
 *
 * Revision 1.1  1996/01/10 20:55:07  fritz
 * Initial revision
 *
 */

#ifndef isdn_h
#define isdn_h

#include <linux/config.h>
#include <linux/ioctl.h>

#define ISDN_TTY_MAJOR    43
#define ISDN_TTYAUX_MAJOR 44
#define ISDN_MAJOR        45

/* The minor-devicenumbers for Channel 0 and 1 are used as arguments for
 * physical Channel-Mapping, so they MUST NOT be changed without changing
 * the correspondent code in isdn.c
 */

#ifdef CONFIG_COBALT_MICRO_SERVER
/* Save memory */
#define ISDN_MAX_DRIVERS    2
#define ISDN_MAX_CHANNELS   8
#else
#define ISDN_MAX_DRIVERS    32
#define ISDN_MAX_CHANNELS   64
#endif
#define ISDN_MINOR_B        0
#define ISDN_MINOR_BMAX     (ISDN_MAX_CHANNELS-1)
#define ISDN_MINOR_CTRL     64
#define ISDN_MINOR_CTRLMAX  (64 + (ISDN_MAX_CHANNELS-1))
#define ISDN_MINOR_PPP      128
#define ISDN_MINOR_PPPMAX   (128 + (ISDN_MAX_CHANNELS-1))
#define ISDN_MINOR_STATUS   255

#undef CONFIG_ISDN_WITH_ABC_CALLB
#undef CONFIG_ISDN_WITH_ABC_UDP_CHECK
#undef CONFIG_ISDN_WITH_ABC_UDP_CHECK_HANGUP
#undef CONFIG_ISDN_WITH_ABC_UDP_CHECK_DIAL
#undef CONFIG_ISDN_WITH_ABC_OUTGOING_EAZ
#undef CONFIG_ISDN_WITH_ABC_LCR_SUPPORT
#undef CONFIG_ISDN_WITH_ABC_IPV4_TCP_KEEPALIVE
#undef CONFIG_ISDN_WITH_ABC_IPV4_DYNADDR
#undef CONFIG_ISDN_WITH_ABC_RCV_NO_HUPTIMER
#undef CONFIG_ISDN_WITH_ABC_ICALL_BIND


/* New ioctl-codes */
#define IIOCNETAIF  _IO('I',1)
#define IIOCNETDIF  _IO('I',2)
#define IIOCNETSCF  _IO('I',3)
#define IIOCNETGCF  _IO('I',4)
#define IIOCNETANM  _IO('I',5)
#define IIOCNETDNM  _IO('I',6)
#define IIOCNETGNM  _IO('I',7)
#define IIOCGETSET  _IO('I',8) /* no longer supported */
#define IIOCSETSET  _IO('I',9) /* no longer supported */
#define IIOCSETVER  _IO('I',10)
#define IIOCNETHUP  _IO('I',11)
#define IIOCSETGST  _IO('I',12)
#define IIOCSETBRJ  _IO('I',13)
#define IIOCSIGPRF  _IO('I',14)
#define IIOCGETPRF  _IO('I',15)
#define IIOCSETPRF  _IO('I',16)
#define IIOCGETMAP  _IO('I',17)
#define IIOCSETMAP  _IO('I',18)
#define IIOCNETASL  _IO('I',19)
#define IIOCNETDIL  _IO('I',20)
#define IIOCGETCPS  _IO('I',21)
#define IIOCGETDVR  _IO('I',22)
#define IIOCNETLCR  _IO('I',23) /* dwabc ioctl for LCR from isdnlog */

#define IIOCNETALN  _IO('I',32)
#define IIOCNETDLN  _IO('I',33)

#define IIOCNETGPN  _IO('I',34)

#define IIOCDBGVAR  _IO('I',127)

#define IIOCDRVCTL  _IO('I',128)

/* Packet encapsulations for net-interfaces */
#define ISDN_NET_ENCAP_ETHER      0
#define ISDN_NET_ENCAP_RAWIP      1
#define ISDN_NET_ENCAP_IPTYP      2
#define ISDN_NET_ENCAP_CISCOHDLC  3 /* Without SLARP and keepalive */
#define ISDN_NET_ENCAP_SYNCPPP    4
#define ISDN_NET_ENCAP_UIHDLC     5
#define ISDN_NET_ENCAP_CISCOHDLCK 6 /* With SLARP and keepalive    */
#define ISDN_NET_ENCAP_X25IFACE   7 /* Documentation/networking/x25-iface.txt*/
#define ISDN_NET_ENCAP_MAX_ENCAP  ISDN_NET_ENCAP_X25IFACE
/* Facility which currently uses an ISDN-channel */
#define ISDN_USAGE_NONE       0
#define ISDN_USAGE_RAW        1
#define ISDN_USAGE_MODEM      2
#define ISDN_USAGE_NET        3
#define ISDN_USAGE_VOICE      4
#define ISDN_USAGE_FAX        5
#define ISDN_USAGE_MASK       7 /* Mask to get plain usage */
#define ISDN_USAGE_DISABLED  32 /* This bit is set, if channel is disabled */
#define ISDN_USAGE_EXCLUSIVE 64 /* This bit is set, if channel is exclusive */
#define ISDN_USAGE_OUTGOING 128 /* This bit is set, if channel is outgoing  */

#define ISDN_MODEM_ANZREG    24        /* Number of Modem-Registers        */
#define ISDN_LMSNLEN         255 /* Length of tty's Listen-MSN string */
#define ISDN_CMSGLEN	     50	 /* Length of CONNECT-Message to add for Modem */

#define ISDN_MSNLEN          20
#define NET_DV 0x05  /* Data version for isdn_net_ioctl_cfg   */
#define TTY_DV 0x05  /* Data version for iprofd etc.          */

#define INF_DV 0x01  /* Data version for /dev/isdninfo        */

typedef struct {
  char drvid[25];
  unsigned long arg;
} isdn_ioctl_struct;

typedef struct {
  unsigned long isdndev;
  unsigned long atmodem[ISDN_MAX_CHANNELS];
  unsigned long info[ISDN_MAX_CHANNELS];
} debugvar_addr;

typedef struct {
  char name[10];
  char phone[ISDN_MSNLEN];
  int  outgoing;
} isdn_net_ioctl_phone;

typedef struct {
  char name[10];     /* Name of interface                     */
  char master[10];   /* Name of Master for Bundling           */
  char slave[10];    /* Name of Slave for Bundling            */
  char eaz[256];     /* EAZ/MSN                               */
  char drvid[25];    /* DriverId for Bindings                 */
  int  onhtime;      /* Hangup-Timeout                        */
  int  charge;       /* Charge-Units                          */
  int  l2_proto;     /* Layer-2 protocol                      */
  int  l3_proto;     /* Layer-3 protocol                      */
  int  p_encap;      /* Encapsulation                         */
  int  exclusive;    /* Channel, if bound exclusive           */
  int  dialmax;      /* Dial Retry-Counter                    */
  int  slavedelay;   /* Delay until slave starts up           */
  int  cbdelay;      /* Delay before Callback                 */
  int  chargehup;    /* Flag: Charge-Hangup                   */
  int  ihup;         /* Flag: Hangup-Timeout on incoming line */
  int  secure;       /* Flag: Secure                          */
  int  callback;     /* Flag: Callback                        */
  int  cbhup;        /* Flag: Reject Call before Callback     */
  int  pppbind;      /* ippp device for bindings              */
  int  chargeint;    /* Use fixed charge interval length      */
  int  triggercps;   /* BogoCPS needed for triggering slave   */
  int  dialtimeout;  /* Dial-Timeout                          */
  int  dialwait;     /* Time to wait after failed dial        */
  int  dialmode;     /* Flag: off / on / auto                 */
} isdn_net_ioctl_cfg;

#define ISDN_NET_DIALMODE_MASK 0xC0  /* bits for status                   */
#define  ISDN_NET_DM_OFF	0x00    /* this interface is stopped      */
#define  ISDN_NET_DM_MANUAL	0x40    /* this interface is on (manual)  */
#define  ISDN_NET_DM_AUTO	0x80    /* this interface is autodial     */
#define ISDN_NET_DIALMODE(x) ((&(x))->flags & ISDN_NET_DIALMODE_MASK)

#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/malloc.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/serial_reg.h>
#include <linux/fcntl.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/tcp.h>

#ifdef CONFIG_ISDN_PPP

#ifdef CONFIG_ISDN_PPP_VJ
#  include <net/slhc_vj.h>
#endif

#include <linux/ppp_defs.h>
#include <linux/if_ppp.h>
#include <linux/if_pppvar.h>

#include <linux/isdn_ppp.h>
#endif

#ifdef CONFIG_ISDN_X25
#  include <linux/concap.h>
#endif

#include <linux/isdnif.h>

#define ISDN_DRVIOCTL_MASK       0x7f  /* Mask for Device-ioctl */

/* Until now unused */
#define ISDN_SERVICE_VOICE 1
#define ISDN_SERVICE_AB    1<<1 
#define ISDN_SERVICE_X21   1<<2
#define ISDN_SERVICE_G4    1<<3
#define ISDN_SERVICE_BTX   1<<4
#define ISDN_SERVICE_DFUE  1<<5
#define ISDN_SERVICE_X25   1<<6
#define ISDN_SERVICE_TTX   1<<7
#define ISDN_SERVICE_MIXED 1<<8
#define ISDN_SERVICE_FW    1<<9
#define ISDN_SERVICE_GTEL  1<<10
#define ISDN_SERVICE_BTXN  1<<11
#define ISDN_SERVICE_BTEL  1<<12

/* Macros checking plain usage */
#define USG_NONE(x)         ((x & ISDN_USAGE_MASK)==ISDN_USAGE_NONE)
#define USG_RAW(x)          ((x & ISDN_USAGE_MASK)==ISDN_USAGE_RAW)
#define USG_MODEM(x)        ((x & ISDN_USAGE_MASK)==ISDN_USAGE_MODEM)
#define USG_VOICE(x)        ((x & ISDN_USAGE_MASK)==ISDN_USAGE_VOICE)
#define USG_NET(x)          ((x & ISDN_USAGE_MASK)==ISDN_USAGE_NET)
#define USG_FAX(x)          ((x & ISDN_USAGE_MASK)==ISDN_USAGE_FAX)
#define USG_OUTGOING(x)     ((x & ISDN_USAGE_OUTGOING)==ISDN_USAGE_OUTGOING)
#define USG_MODEMORVOICE(x) (((x & ISDN_USAGE_MASK)==ISDN_USAGE_MODEM) || \
                             ((x & ISDN_USAGE_MASK)==ISDN_USAGE_VOICE)     )

/* Timer-delays and scheduling-flags */
#define ISDN_TIMER_RES         3                         /* Main Timer-Resolution   */
#define ISDN_TIMER_02SEC       (HZ/(ISDN_TIMER_RES+1)/5) /* Slow-Timer1 .2 sec      */
#define ISDN_TIMER_1SEC        (HZ/(ISDN_TIMER_RES+1))   /* Slow-Timer2 1 sec       */
#define ISDN_TIMER_RINGING     5 /* tty RINGs = ISDN_TIMER_1SEC * this factor       */
#define ISDN_TIMER_KEEPINT    10 /* Cisco-Keepalive = ISDN_TIMER_1SEC * this factor */
#define ISDN_TIMER_MODEMREAD   1
#define ISDN_TIMER_MODEMPLUS   2
#define ISDN_TIMER_MODEMRING   4
#define ISDN_TIMER_MODEMXMIT   8
#define ISDN_TIMER_NETDIAL    16 
#define ISDN_TIMER_NETHANGUP  32
#define ISDN_TIMER_IPPP       64 
#define ISDN_TIMER_KEEPALIVE 128 /* Cisco-Keepalive */
#define ISDN_TIMER_CARRIER   256 /* Wait for Carrier */
#define ISDN_TIMER_FAST      (ISDN_TIMER_MODEMREAD | ISDN_TIMER_MODEMPLUS | \
                              ISDN_TIMER_MODEMXMIT)
#define ISDN_TIMER_SLOW      (ISDN_TIMER_MODEMRING | ISDN_TIMER_NETHANGUP | \
                              ISDN_TIMER_NETDIAL | ISDN_TIMER_KEEPALIVE | \
                              ISDN_TIMER_CARRIER)

/* Timeout-Values for isdn_net_dial() */
#define ISDN_TIMER_DTIMEOUT10 (10*HZ/(ISDN_TIMER_02SEC*(ISDN_TIMER_RES+1)))
#define ISDN_TIMER_DTIMEOUT15 (15*HZ/(ISDN_TIMER_02SEC*(ISDN_TIMER_RES+1)))
#define ISDN_TIMER_DTIMEOUT60 (60*HZ/(ISDN_TIMER_02SEC*(ISDN_TIMER_RES+1)))

/* GLOBAL_FLAGS */
#define ISDN_GLOBAL_STOPPED 1

/*=================== Start of ip-over-ISDN stuff =========================*/

/* Feature- and status-flags for a net-interface */
#define ISDN_NET_CONNECTED  0x01       /* Bound to ISDN-Channel             */
#define ISDN_NET_SECURE     0x02       /* Accept calls from phonelist only  */
#define ISDN_NET_CALLBACK   0x04       /* activate callback                 */
#define ISDN_NET_CBHUP      0x08       /* hangup before callback            */
#define ISDN_NET_CBOUT      0x10       /* remote machine does callback      */

#define ISDN_NET_MAGIC      0x49344C02 /* for paranoia-checking             */

/* Phone-list-element */
typedef struct {
  void *next;
  char num[ISDN_MSNLEN];
} isdn_net_phone;

/*
   Principles when extending structures for generic encapsulation protocol
   ("concap") support:
   - Stuff which is hardware specific (here i4l-specific) goes in 
     the netdev -> local structure (here: isdn_net_local)
   - Stuff which is encapsulation protocol specific goes in the structure
     which holds the linux device structure (here: isdn_net_device)
*/

/* Local interface-data */
typedef struct isdn_net_local_s {
  ulong                  magic;
  char                   name[10];     /* Name of device                   */
  struct enet_statistics stats;        /* Ethernet Statistics              */
  int                    isdn_device;  /* Index to isdn-device             */
  int                    isdn_channel; /* Index to isdn-channel            */
  int			 ppp_slot;     /* PPPD device slot number          */
  int                    pre_device;   /* Preselected isdn-device          */
  int                    pre_channel;  /* Preselected isdn-channel         */
  int                    exclusive;    /* If non-zero idx to reserved chan.*/
  int                    flags;        /* Connection-flags                 */
  int                    dialretry;    /* Counter for Dialout-retries      */
  int                    dialmax;      /* Max. Number of Dial-retries      */
  int                    cbdelay;      /* Delay before Callback starts     */
  int                    dtimer;       /* Timeout-counter for dialing      */
  char                   msn[ISDN_MSNLEN]; /* MSNs/EAZs for this interface */
  u_char                 cbhup;        /* Flag: Reject Call before Callback*/
  u_char                 dialstate;    /* State for dialing                */
  u_char                 p_encap;      /* Packet encapsulation             */
                                       /*   0 = Ethernet over ISDN         */
				       /*   1 = RAW-IP                     */
                                       /*   2 = IP with type field         */
  u_char                 l2_proto;     /* Layer-2-protocol                 */
				       /* See ISDN_PROTO_L2..-constants in */
                                       /* isdnif.h                         */
                                       /*   0 = X75/LAPB with I-Frames     */
				       /*   1 = X75/LAPB with UI-Frames    */
				       /*   2 = X75/LAPB with BUI-Frames   */
				       /*   3 = HDLC                       */
  u_char                 l3_proto;     /* Layer-3-protocol                 */
				       /* See ISDN_PROTO_L3..-constants in */
                                       /* isdnif.h                         */
                                       /*   0 = Transparent                */
  int                    huptimer;     /* Timeout-counter for auto-hangup  */
  int                    charge;       /* Counter for charging units       */
  int                    chargetime;   /* Timer for Charging info          */
  int                    hupflags;     /* Flags for charge-unit-hangup:    */
				       /* bit0: chargeint is invalid       */
				       /* bit1: Getting charge-interval    */
                                       /* bit2: Do charge-unit-hangup      */
                                       /* bit3: Do hangup even on incoming */
  int                    outgoing;     /* Flag: outgoing call              */
  int                    onhtime;      /* Time to keep link up             */
  int                    chargeint;    /* Interval between charge-infos    */
  int                    onum;         /* Flag: at least 1 outgoing number */
  int                    cps;          /* current speed of this interface  */
  int                    transcount;   /* byte-counter for cps-calculation */
  int                    sqfull;       /* Flag: netdev-queue overloaded    */
  ulong                  sqfull_stamp; /* Start-Time of overload           */
  ulong                  slavedelay;   /* Dynamic bundling delaytime       */
  int                    triggercps;   /* BogoCPS needed for trigger slave */
  struct device      *srobin;      /* Ptr to Master device for slaves  */
  isdn_net_phone         *phone[2];    /* List of remote-phonenumbers      */
				       /* phone[0] = Incoming Numbers      */
				       /* phone[1] = Outgoing Numbers      */
  isdn_net_phone         *dial;        /* Pointer to dialed number         */
  struct device      *master;      /* Ptr to Master device for slaves  */
  struct device      *slave;       /* Ptr to Slave device for masters  */
  struct isdn_net_local_s *next;       /* Ptr to next link in bundle       */
  struct isdn_net_local_s *last;       /* Ptr to last link in bundle       */
  struct isdn_net_dev_s  *netdev;      /* Ptr to netdev                    */
  struct sk_buff         *first_skb;   /* Ptr to skb that triggers dialing */
  struct sk_buff *volatile sav_skb;    /* Ptr to skb, rejected by LL-driver*/
                                       /* Ptr to orig. hard_header_cache   */
  int                    (*org_hhc)(
				    struct neighbour *neigh,
				    struct hh_cache *hh);
                                       /* Ptr to orig. header_cache_update */
  void                   (*org_hcu)(struct hh_cache *,
				    struct device *,
                                    unsigned char *);
  int  pppbind;                        /* ippp device for bindings         */
  int					dialtimeout;	/* How long shall we try on dialing? (jiffies) */
  int					dialwait;		/* How long shall we wait after failed attempt? (jiffies) */
  ulong					dialstarted;	/* jiffies of first dialing-attempt */
  ulong					dialwait_timer;	/* jiffies of earliest next dialing-attempt */
  int					huptimeout;		/* How long will the connection be up? (seconds) */
#ifdef CONFIG_ISDN_X25
  struct concap_device_ops *dops;      /* callbacks used by encapsulator   */
#endif
  int  cisco_loop;                     /* Loop counter for Cisco-SLARP     */
  ulong cisco_myseq;                   /* Local keepalive seq. for Cisco   */
  ulong cisco_yourseq;                 /* Remote keepalive seq. for Cisco  */
} isdn_net_local;

/* the interface itself */
typedef struct isdn_net_dev_s {
  isdn_net_local *local;
  isdn_net_local *queue;
  void           *next;                /* Pointer to next isdn-interface   */
  struct device dev;               /* interface to upper levels        */
#ifdef CONFIG_ISDN_PPP
  struct mpqueue *mp_last; 
  struct ippp_bundle ib;
#endif
#ifdef CONFIG_ISDN_X25
  struct concap_proto  *cprot; /* connection oriented encapsulation protocol */
#endif

} isdn_net_dev;

/*===================== End of ip-over-ISDN stuff ===========================*/

/*======================= Start of ISDN-tty stuff ===========================*/

#define ISDN_ASYNC_MAGIC          0x49344C01 /* for paranoia-checking        */
#define ISDN_ASYNC_INITIALIZED	  0x80000000 /* port was initialized         */
#define ISDN_ASYNC_CALLOUT_ACTIVE 0x40000000 /* Call out device active       */
#define ISDN_ASYNC_NORMAL_ACTIVE  0x20000000 /* Normal device active         */
#define ISDN_ASYNC_CLOSING	  0x08000000 /* Serial port is closing       */
#define ISDN_ASYNC_CTS_FLOW	  0x04000000 /* Do CTS flow control          */
#define ISDN_ASYNC_CHECK_CD	  0x02000000 /* i.e., CLOCAL                 */
#define ISDN_ASYNC_HUP_NOTIFY         0x0001 /* Notify tty on hangups/closes */
#define ISDN_ASYNC_SESSION_LOCKOUT    0x0100 /* Lock cua opens on session    */
#define ISDN_ASYNC_PGRP_LOCKOUT       0x0200 /* Lock cua opens on pgrp       */
#define ISDN_ASYNC_CALLOUT_NOHUP      0x0400 /* No hangup for cui            */
#define ISDN_ASYNC_SPLIT_TERMIOS      0x0008 /* Sep. termios for dialin/out  */
#define ISDN_SERIAL_XMIT_SIZE           1024 /* Default bufsize for write    */
#define ISDN_SERIAL_XMIT_MAX            4000 /* Maximum bufsize for write    */
#define ISDN_SERIAL_TYPE_NORMAL            1
#define ISDN_SERIAL_TYPE_CALLOUT           2

#ifdef CONFIG_ISDN_AUDIO
/* For using sk_buffs with audio we need some private variables
 * within each sk_buff. For this purpose, we declare a struct here,
 * and put it always at skb->head. A few macros help accessing the
 * variables. Of course, we need to check skb_headroom prior to
 * any access.
 */
typedef struct isdn_audio_skb {
  unsigned short dle_count;
  unsigned char  lock;
} isdn_audio_skb;

#define ISDN_AUDIO_SKB_DLECOUNT(skb) (((isdn_audio_skb*)skb->head)->dle_count)
#define ISDN_AUDIO_SKB_LOCK(skb) (((isdn_audio_skb*)skb->head)->lock)
#endif

/* Private data of AT-command-interpreter */
typedef struct atemu {
	u_char       profile[ISDN_MODEM_ANZREG]; /* Modem-Regs. Profile 0              */
	u_char       mdmreg[ISDN_MODEM_ANZREG];  /* Modem-Registers                    */
	char         pmsn[ISDN_MSNLEN];          /* EAZ/MSNs Profile 0                 */
	char         msn[ISDN_MSNLEN];           /* EAZ/MSN                            */
	char         plmsn[ISDN_LMSNLEN];        /* Listening MSNs Profile 0           */
	char         lmsn[ISDN_LMSNLEN];         /* Listening MSNs                     */
	char         cpn[ISDN_MSNLEN];           /* CalledPartyNumber on incoming call */
	char         connmsg[ISDN_CMSGLEN];	 /* CONNECT-Msg from HL-Driver	       */
#ifdef CONFIG_ISDN_AUDIO
	u_char       vpar[10];                   /* Voice-parameters                   */
	int          lastDLE;                    /* Flag for voice-coding: DLE seen    */
#endif
	int          mdmcmdl;                    /* Length of Modem-Commandbuffer      */
	int          pluscount;                  /* Counter for +++ sequence           */
	int          lastplus;                   /* Timestamp of last +                */
	int	     carrierwait;                /* Seconds of carrier waiting         */
	char         mdmcmd[255];                /* Modem-Commandbuffer                */
	unsigned int charge;                     /* Charge units of current connection */
} atemu;

/* Private data (similar to async_struct in <linux/serial.h>) */
typedef struct modem_info {
  int			magic;
  int			flags;		 /* defined in tty.h               */
  int			x_char;		 /* xon/xoff character             */
  int			mcr;		 /* Modem control register         */
  int                   msr;             /* Modem status register          */
  int                   lsr;             /* Line status register           */
  int			line;
  int			count;		 /* # of fd on device              */
  int			blocked_open;	 /* # of blocked opens             */
  long			session;	 /* Session of opening process     */
  long			pgrp;		 /* pgrp of opening process        */
  int                   online;          /* 1 = B-Channel is up, drop data */
					 /* 2 = B-Channel is up, deliver d.*/
  int                   dialing;         /* Dial in progress or ATA        */
  int                   rcvsched;        /* Receive needs schedule         */
  int                   isdn_driver;	 /* Index to isdn-driver           */
  int                   isdn_channel;    /* Index to isdn-channel          */
  int                   drv_index;       /* Index to dev->usage            */
  int                   ncarrier;        /* Flag: schedule NO CARRIER      */
  unsigned char         last_cause[8];   /* Last cause message             */
  unsigned char         last_num[ISDN_MSNLEN];
	                                 /* Last phone-number              */
  unsigned char         last_l2;         /* Last layer-2 protocol          */
  unsigned char         last_si;         /* Last service                   */
  unsigned char         last_lhup;       /* Last hangup local?             */
  unsigned char         last_dir;        /* Last direction (in or out)     */
  struct timer_list     nc_timer;        /* Timer for delayed NO CARRIER   */
  int                   send_outstanding;/* # of outstanding send-requests */
  int                   xmit_size;       /* max. # of chars in xmit_buf    */
  int                   xmit_count;      /* # of chars in xmit_buf         */
  unsigned char         *xmit_buf;       /* transmit buffer                */
  struct sk_buff_head   xmit_queue;      /* transmit queue                 */
  atomic_t              xmit_lock;       /* Semaphore for isdn_tty_write   */
#ifdef CONFIG_ISDN_AUDIO
  int                   vonline;         /* Voice-channel status           */
					 /* Bit 0 = recording              */
					 /* Bit 1 = playback               */
					 /* Bit 2 = playback, DLE-ETX seen */
  struct sk_buff_head   dtmf_queue;      /* queue for dtmf results         */
  void                  *adpcms;         /* state for adpcm decompression  */
  void                  *adpcmr;         /* state for adpcm compression    */
  void                  *dtmf_state;     /* state for dtmf decoder         */
  void                  *silence_state;  /* state for silence detection    */
#endif
#ifdef CONFIG_ISDN_TTY_FAX
  struct T30_s		*fax;		 /* T30 Fax Group 3 data/interface */
  int			faxonline;	 /* Fax-channel status             */
#endif
  struct tty_struct 	*tty;            /* Pointer to corresponding tty   */
  atemu                 emu;             /* AT-emulator data               */
  struct termios	normal_termios;  /* For saving termios structs     */
  struct termios	callout_termios;
  struct wait_queue	*open_wait;
  struct wait_queue	*close_wait;
  struct semaphore      write_sem;
} modem_info;

#define ISDN_MODEM_WINSIZE 8

/* Description of one ISDN-tty */
typedef struct {
  int                refcount;			   /* Number of opens        */
  struct tty_driver  tty_modem;			   /* tty-device             */
  struct tty_driver  cua_modem;			   /* cua-device             */
  struct tty_struct  *modem_table[ISDN_MAX_CHANNELS]; /* ?? copied from Orig */
  struct termios     *modem_termios[ISDN_MAX_CHANNELS];
  struct termios     *modem_termios_locked[ISDN_MAX_CHANNELS];
  modem_info         info[ISDN_MAX_CHANNELS];	   /* Private data           */
} modem;

/*======================= End of ISDN-tty stuff ============================*/

/*======================== Start of V.110 stuff ============================*/
#define V110_BUFSIZE 1024

typedef struct {
	int nbytes;                    /* 1 Matrixbyte -> nbytes in stream     */
	int nbits;                     /* Number of used bits in streambyte    */
	unsigned char key;             /* Bitmask in stream eg. 11 (nbits=2)   */
	int decodelen;                 /* Amount of data in decodebuf          */
	int SyncInit;                  /* Number of sync frames to send        */
	unsigned char *OnlineFrame;    /* Precalculated V110 idle frame        */
	unsigned char *OfflineFrame;   /* Precalculated V110 sync Frame        */
	int framelen;                  /* Length of frames                     */
	int skbuser;                   /* Number of unacked userdata skbs      */
	int skbidle;                   /* Number of unacked idle/sync skbs     */
	int introducer;                /* Local vars for decoder               */
	int dbit;
	unsigned char b;
	int skbres;                    /* space to reserve in outgoing skb     */
	int maxsize;                   /* maxbufsize of lowlevel driver        */
	unsigned char *encodebuf;      /* temporary buffer for encoding        */
	unsigned char decodebuf[V110_BUFSIZE]; /* incomplete V110 matrices     */
} isdn_v110_stream;

/*========================= End of V.110 stuff =============================*/

/*======================= Start of general stuff ===========================*/

typedef struct {
	char *next;
	char *private;
} infostruct;

typedef struct isdn_module {
	struct isdn_module *prev;
	struct isdn_module *next;
	char *name;
	int (*get_free_channel)(int, int, int, int, int);
	int (*free_channel)(int, int, int);
	int (*status_callback)(isdn_ctrl *);
	int (*command)(isdn_ctrl *);
	int (*receive_callback)(int, int, struct sk_buff *);
	int (*writebuf_skb)(int, int, int, struct sk_buff *);
	int (*net_start_xmit)(struct sk_buff *, struct device *);
	int (*net_receive)(struct device *, struct sk_buff *);
	int (*net_open)(struct device *);
	int (*net_close)(struct device *);
	int priority;
} isdn_module;

#define DRV_FLAG_RUNNING 1
#define DRV_FLAG_REJBUS  2
#define DRV_FLAG_LOADED  4

/* Description of hardware-level-driver */
typedef struct {
	ulong               online;           /* Channel-Online flags             */
	ulong               flags;            /* Misc driver Flags                */
	int                 locks;            /* Number of locks for this driver  */
	int                 channels;         /* Number of channels               */
	struct wait_queue  *st_waitq;         /* Wait-Queue for status-read's     */
	int                 maxbufsize;       /* Maximum Buffersize supported     */
	unsigned long       pktcount;         /* Until now: unused                */
	int                 stavail;          /* Chars avail on Status-device     */
	isdn_if            *interface;        /* Interface to driver              */
	int                *rcverr;           /* Error-counters for B-Ch.-receive */
	int                *rcvcount;         /* Byte-counters for B-Ch.-receive  */
#ifdef CONFIG_ISDN_AUDIO
	unsigned long      DLEflag;           /* Flags: Insert DLE at next read   */
#endif
	struct sk_buff_head *rpqueue;         /* Pointers to start of Rcv-Queue   */
	struct wait_queue  **rcv_waitq;       /* Wait-Queues for B-Channel-Reads  */
	struct wait_queue  **snd_waitq;       /* Wait-Queue for B-Channel-Send's  */
	char               msn2eaz[10][ISDN_MSNLEN];  /* Mapping-Table MSN->EAZ   */
} driver;

/* Main driver-data */
typedef struct isdn_devt {
	unsigned short    flags;		       /* Bitmapped Flags:           */
	/*                            */
	int               drivers;		       /* Current number of drivers  */
	int               channels;		       /* Current number of channels */
	int               net_verbose;               /* Verbose-Flag               */
	int               modempoll;		       /* Flag: tty-read active      */
	int               tflags;                    /* Timer-Flags:               */
	/*  see ISDN_TIMER_..defines  */
	int               global_flags;
	infostruct        *infochain;                /* List of open info-devs.    */
	struct wait_queue *info_waitq;               /* Wait-Queue for isdninfo    */
	struct timer_list timer;		       /* Misc.-function Timer       */
	int               chanmap[ISDN_MAX_CHANNELS];/* Map minor->device-channel  */
	int               drvmap[ISDN_MAX_CHANNELS]; /* Map minor->driver-index    */
	int               usage[ISDN_MAX_CHANNELS];  /* Used by tty/ip/voice       */
	char              num[ISDN_MAX_CHANNELS][ISDN_MSNLEN];
	/* Remote number of active ch.*/
	int               m_idx[ISDN_MAX_CHANNELS];  /* Index for mdm....          */
	driver            *drv[ISDN_MAX_DRIVERS];    /* Array of drivers           */
	isdn_net_dev      *netdev;		       /* Linked list of net-if's    */
	char              drvid[ISDN_MAX_DRIVERS][20];/* Driver-ID                 */
	struct task_struct *profd;                   /* For iprofd                 */
	modem             mdm;		       /* tty-driver-data            */
	isdn_net_dev      *rx_netdev[ISDN_MAX_CHANNELS]; /* rx netdev-pointers     */
	isdn_net_dev      *st_netdev[ISDN_MAX_CHANNELS]; /* stat netdev-pointers   */
	ulong             ibytes[ISDN_MAX_CHANNELS]; /* Statistics incoming bytes  */
	ulong             obytes[ISDN_MAX_CHANNELS]; /* Statistics outgoing bytes  */
	int               v110emu[ISDN_MAX_CHANNELS];/* V.110 emulator-mode 0=none */
	atomic_t          v110use[ISDN_MAX_CHANNELS];/* Usage-Semaphore for stream */
	isdn_v110_stream  *v110[ISDN_MAX_CHANNELS];  /* V.110 private data         */
	struct semaphore  sem;                       /* serialize list access*/
	isdn_module       *modules;
} isdn_dev;

extern isdn_dev *dev;



/* Utility-Macros */
#define MIN(a,b) ((a<b)?a:b)
#define MAX(a,b) ((a>b)?a:b)
#endif /* __KERNEL__ */
#endif /* isdn_h */
