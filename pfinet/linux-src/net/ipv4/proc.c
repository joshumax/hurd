/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  It is mainly used for debugging and
 *		statistics.
 *
 * Version:	$Id: proc.c,v 1.34 1999/02/08 11:20:34 davem Exp $
 *
 * Authors:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Gerald J. Heim, <heim@peanuts.informatik.uni-tuebingen.de>
 *		Fred Baumgarten, <dc6iq@insu1.etec.uni-karlsruhe.de>
 *		Erik Schoenfelder, <schoenfr@ibr.cs.tu-bs.de>
 *
 * Fixes:
 *		Alan Cox	:	UDP sockets show the rxqueue/txqueue
 *					using hint flag for the netinfo.
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Make /proc safer.
 *	Erik Schoenfelder	:	/proc/net/snmp
 *		Alan Cox	:	Handle dead sockets properly.
 *	Gerhard Koerting	:	Show both timers
 *		Alan Cox	:	Allow inode to be NULL (kernel socket)
 *	Andi Kleen		:	Add support for open_requests and 
 *					split functions for more readibility.
 *	Andi Kleen		:	Add support for /proc/net/netstat
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/system.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/un.h>
#include <linux/in.h>
#include <linux/param.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>

/* Format a single open_request into tmpbuf. */
static inline void get__openreq(struct sock *sk, struct open_request *req, 
				char *tmpbuf, 
				int i)
{
	sprintf(tmpbuf, "%4d: %08lX:%04X %08lX:%04X"
		" %02X %08X:%08X %02X:%08lX %08X %5d %8d %u",
		i,
		(long unsigned int)req->af.v4_req.loc_addr,
		ntohs(sk->sport),
		(long unsigned int)req->af.v4_req.rmt_addr,
		ntohs(req->rmt_port),
		TCP_SYN_RECV,
		0,0, /* could print option size, but that is af dependent. */
		1,   /* timers active (only the expire timer) */  
		(unsigned long)(req->expires - jiffies), 
		req->retrans,
		sk->socket ? sk->socket->inode->i_uid : 0,
		0,  /* non standard timer */  
		0 /* open_requests have no inode */
		); 
}

/* Format a single socket into tmpbuf. */
static inline void get__sock(struct sock *sp, char *tmpbuf, int i, int format)
{
	unsigned long  dest, src;
	unsigned short destp, srcp;
	int timer_active, timer_active1, timer_active2;
	int tw_bucket = 0;
	unsigned long timer_expires;
	struct tcp_opt *tp = &sp->tp_pinfo.af_tcp;

	dest  = sp->daddr;
	src   = sp->rcv_saddr;
	destp = sp->dport;
	srcp  = sp->sport;
	
	/* FIXME: The fact that retransmit_timer occurs as a field
	 * in two different parts of the socket structure is,
	 * to say the least, confusing. This code now uses the
	 * right retransmit_timer variable, but I'm not sure
	 * the rest of the timer stuff is still correct.
	 * In particular I'm not sure what the timeout value
	 * is suppose to reflect (as opposed to tm->when). -- erics
	 */
	
	destp = ntohs(destp);
	srcp  = ntohs(srcp);
	if((format == 0) && (sp->state == TCP_TIME_WAIT)) {
		extern int tcp_tw_death_row_slot;
		struct tcp_tw_bucket *tw = (struct tcp_tw_bucket *)sp;
		int slot_dist;

		tw_bucket	= 1;
		timer_active1	= timer_active2 = 0;
		timer_active	= 3;
		slot_dist	= tw->death_slot;
		if(slot_dist > tcp_tw_death_row_slot)
			slot_dist = (TCP_TWKILL_SLOTS - slot_dist) + tcp_tw_death_row_slot;
		else
			slot_dist = tcp_tw_death_row_slot - slot_dist;
		timer_expires	= jiffies + (slot_dist * TCP_TWKILL_PERIOD);
	} else {
		timer_active1 = del_timer(&tp->retransmit_timer);
		timer_active2 = del_timer(&sp->timer);
		if (!timer_active1) tp->retransmit_timer.expires=0;
		if (!timer_active2) sp->timer.expires=0;
		timer_active	= 0;
		timer_expires	= (unsigned) -1;
	}
	if (timer_active1 && tp->retransmit_timer.expires < timer_expires) {
		timer_active	= 1;
		timer_expires	= tp->retransmit_timer.expires;
	}
	if (timer_active2 && sp->timer.expires < timer_expires) {
		timer_active	= 2;
		timer_expires	= sp->timer.expires;
	}
	if(timer_active == 0)
		timer_expires = jiffies;
	sprintf(tmpbuf, "%4d: %08lX:%04X %08lX:%04X"
		" %02X %08X:%08X %02X:%08lX %08X %5d %8d %ld",
		i, src, srcp, dest, destp, sp->state, 
		(tw_bucket ?
		 0 :
		 (format == 0) ?
		 tp->write_seq-tp->snd_una : atomic_read(&sp->wmem_alloc)),
		(tw_bucket ?
		 0 :
		 (format == 0) ?
		 tp->rcv_nxt-tp->copied_seq: atomic_read(&sp->rmem_alloc)),
		timer_active, timer_expires-jiffies,
		(tw_bucket ? 0 : tp->retransmits),
		(!tw_bucket && sp->socket) ? sp->socket->inode->i_uid : 0,
		(!tw_bucket && timer_active) ? sp->timeout : 0,
		(!tw_bucket && sp->socket) ? sp->socket->inode->i_ino : 0);
	
	if (timer_active1) add_timer(&tp->retransmit_timer);
	if (timer_active2) add_timer(&sp->timer);	
}

/*
 * Get__netinfo returns the length of that string.
 *
 * KNOWN BUGS
 *  As in get_unix_netinfo, the buffer might be too small. If this
 *  happens, get__netinfo returns only part of the available infos.
 *
 *  Assumes that buffer length is a multiply of 128 - if not it will
 *  write past the end.   
 */
static int
get__netinfo(struct proto *pro, char *buffer, int format, char **start, off_t offset, int length)
{
	struct sock *sp, *next;
	int len=0, i = 0;
	off_t pos=0;
	off_t begin;
	char tmpbuf[129];
  
	if (offset < 128) 
		len += sprintf(buffer, "%-127s\n",
			       "  sl  local_address rem_address   st tx_queue "
			       "rx_queue tr tm->when retrnsmt   uid  timeout inode");
	pos = 128;
	SOCKHASH_LOCK(); 
	sp = pro->sklist_next;
	while(sp != (struct sock *)pro) {
		if (format == 0 && sp->state == TCP_LISTEN) {
			struct open_request *req;

			for (req = sp->tp_pinfo.af_tcp.syn_wait_queue; req;
			     i++, req = req->dl_next) {
				if (req->sk)
					continue;
				pos += 128;
				if (pos < offset) 
					continue;
				get__openreq(sp, req, tmpbuf, i); 
				len += sprintf(buffer+len, "%-127s\n", tmpbuf);
				if(len >= length) 
					goto out;
			}
		}
		
		pos += 128;
		if (pos < offset)
			goto next;
		
		get__sock(sp, tmpbuf, i, format);
		
		len += sprintf(buffer+len, "%-127s\n", tmpbuf);
		if(len >= length)
			break;
	next:
		next = sp->sklist_next;
		sp = next;
		i++;
	}
out: 
	SOCKHASH_UNLOCK();
	
	begin = len - (pos - offset);
	*start = buffer + begin;
	len -= begin;
	if(len>length)
		len = length;
	if (len<0)
		len = 0; 
	return len;
} 

int tcp_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	return get__netinfo(&tcp_prot, buffer,0, start, offset, length);
}

int udp_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	return get__netinfo(&udp_prot, buffer,1, start, offset, length);
}

int raw_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	return get__netinfo(&raw_prot, buffer,1, start, offset, length);
}

/*
 *	Report socket allocation statistics [mea@utu.fi]
 */
int afinet_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	/* From  net/socket.c  */
	extern int socket_get_info(char *, char **, off_t, int);

	int len  = socket_get_info(buffer,start,offset,length);

	len += sprintf(buffer+len,"TCP: inuse %d highest %d\n",
		       tcp_prot.inuse, tcp_prot.highestinuse);
	len += sprintf(buffer+len,"UDP: inuse %d highest %d\n",
		       udp_prot.inuse, udp_prot.highestinuse);
	len += sprintf(buffer+len,"RAW: inuse %d highest %d\n",
		       raw_prot.inuse, raw_prot.highestinuse);
	if (offset >= len)
	{
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	if (len < 0)
		len = 0;
	return len;
}


/* 
 *	Called from the PROCfs module. This outputs /proc/net/snmp.
 */
 
int snmp_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	extern struct tcp_mib tcp_statistics;
	extern struct udp_mib udp_statistics;
	int len;
/*
  extern unsigned long tcp_rx_miss, tcp_rx_hit1,tcp_rx_hit2;
*/

	len = sprintf (buffer,
		"Ip: Forwarding DefaultTTL InReceives InHdrErrors InAddrErrors ForwDatagrams InUnknownProtos InDiscards InDelivers OutRequests OutDiscards OutNoRoutes ReasmTimeout ReasmReqds ReasmOKs ReasmFails FragOKs FragFails FragCreates\n"
		"Ip: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		    ip_statistics.IpForwarding, ip_statistics.IpDefaultTTL, 
		    ip_statistics.IpInReceives, ip_statistics.IpInHdrErrors, 
		    ip_statistics.IpInAddrErrors, ip_statistics.IpForwDatagrams, 
		    ip_statistics.IpInUnknownProtos, ip_statistics.IpInDiscards, 
		    ip_statistics.IpInDelivers, ip_statistics.IpOutRequests, 
		    ip_statistics.IpOutDiscards, ip_statistics.IpOutNoRoutes, 
		    ip_statistics.IpReasmTimeout, ip_statistics.IpReasmReqds, 
		    ip_statistics.IpReasmOKs, ip_statistics.IpReasmFails, 
		    ip_statistics.IpFragOKs, ip_statistics.IpFragFails, 
		    ip_statistics.IpFragCreates);
		    		
	len += sprintf (buffer + len,
		"Icmp: InMsgs InErrors InDestUnreachs InTimeExcds InParmProbs InSrcQuenchs InRedirects InEchos InEchoReps InTimestamps InTimestampReps InAddrMasks InAddrMaskReps OutMsgs OutErrors OutDestUnreachs OutTimeExcds OutParmProbs OutSrcQuenchs OutRedirects OutEchos OutEchoReps OutTimestamps OutTimestampReps OutAddrMasks OutAddrMaskReps\n"
		"Icmp: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		    icmp_statistics.IcmpInMsgs, icmp_statistics.IcmpInErrors,
		    icmp_statistics.IcmpInDestUnreachs, icmp_statistics.IcmpInTimeExcds,
		    icmp_statistics.IcmpInParmProbs, icmp_statistics.IcmpInSrcQuenchs,
		    icmp_statistics.IcmpInRedirects, icmp_statistics.IcmpInEchos,
		    icmp_statistics.IcmpInEchoReps, icmp_statistics.IcmpInTimestamps,
		    icmp_statistics.IcmpInTimestampReps, icmp_statistics.IcmpInAddrMasks,
		    icmp_statistics.IcmpInAddrMaskReps, icmp_statistics.IcmpOutMsgs,
		    icmp_statistics.IcmpOutErrors, icmp_statistics.IcmpOutDestUnreachs,
		    icmp_statistics.IcmpOutTimeExcds, icmp_statistics.IcmpOutParmProbs,
		    icmp_statistics.IcmpOutSrcQuenchs, icmp_statistics.IcmpOutRedirects,
		    icmp_statistics.IcmpOutEchos, icmp_statistics.IcmpOutEchoReps,
		    icmp_statistics.IcmpOutTimestamps, icmp_statistics.IcmpOutTimestampReps,
		    icmp_statistics.IcmpOutAddrMasks, icmp_statistics.IcmpOutAddrMaskReps);
	
	len += sprintf (buffer + len,
		"Tcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens PassiveOpens AttemptFails EstabResets CurrEstab InSegs OutSegs RetransSegs InErrs OutRsts\n"
		"Tcp: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		    tcp_statistics.TcpRtoAlgorithm, tcp_statistics.TcpRtoMin,
		    tcp_statistics.TcpRtoMax, tcp_statistics.TcpMaxConn,
		    tcp_statistics.TcpActiveOpens, tcp_statistics.TcpPassiveOpens,
		    tcp_statistics.TcpAttemptFails, tcp_statistics.TcpEstabResets,
		    tcp_statistics.TcpCurrEstab, tcp_statistics.TcpInSegs,
		    tcp_statistics.TcpOutSegs, tcp_statistics.TcpRetransSegs,
		    tcp_statistics.TcpInErrs, tcp_statistics.TcpOutRsts);
		
	len += sprintf (buffer + len,
		"Udp: InDatagrams NoPorts InErrors OutDatagrams\nUdp: %lu %lu %lu %lu\n",
		    udp_statistics.UdpInDatagrams, udp_statistics.UdpNoPorts,
		    udp_statistics.UdpInErrors, udp_statistics.UdpOutDatagrams);	    
/*	
	  len += sprintf( buffer + len,
	  	"TCP fast path RX:  H2: %ul H1: %ul L: %ul\n",
	  		tcp_rx_hit2,tcp_rx_hit1,tcp_rx_miss);
*/
	
	if (offset >= len)
	{
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	if (len < 0)
		len = 0; 
	return len;
}

/* 
 *	Output /proc/net/netstat
 */
 
int netstat_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	extern struct linux_mib net_statistics;
	int len;

	len = sprintf(buffer,
		      "TcpExt: SyncookiesSent SyncookiesRecv SyncookiesFailed"
		      " EmbryonicRsts PruneCalled RcvPruned OfoPruned"
		      " OutOfWindowIcmps LockDroppedIcmps\n" 	
		      "TcpExt: %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		      net_statistics.SyncookiesSent,
		      net_statistics.SyncookiesRecv,
		      net_statistics.SyncookiesFailed,
		      net_statistics.EmbryonicRsts,
		      net_statistics.PruneCalled,
		      net_statistics.RcvPruned,
		      net_statistics.OfoPruned,
		      net_statistics.OutOfWindowIcmps,
		      net_statistics.LockDroppedIcmps);

	if (offset >= len)
	{
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	if (len < 0)
		len = 0; 
	return len;
}
