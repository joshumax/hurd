/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		This file implements the various access functions for the
 *		PROC file system.  It is mainly used for debugging and
 *		statistics.
 *
 * Version:	@(#)proc.c	1.0.5	05/27/93
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
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <asm/system.h>
#include <linux/autoconf.h>
#include <linux/sched.h>
#include <linux/socket.h>
#include <linux/net.h>
#include <linux/un.h>
#include <linux/in.h>
#include <linux/param.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include "ip.h"
#include "icmp.h"
#include "protocol.h"
#include "tcp.h"
#include "udp.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "raw.h"

/*
 * Get__netinfo returns the length of that string.
 *
 * KNOWN BUGS
 *  As in get_unix_netinfo, the buffer might be too small. If this
 *  happens, get__netinfo returns only part of the available infos.
 */
static int
get__netinfo(struct proto *pro, char *buffer, int format, char **start, off_t offset, int length)
{
	struct sock **s_array;
	struct sock *sp;
	int i;
	int timer_active;
	unsigned long  dest, src;
	unsigned short destp, srcp;
	int len=0;
	off_t pos=0;
	off_t begin=0;
  
	s_array = pro->sock_array;
	len+=sprintf(buffer, "sl  local_address rem_address   st tx_queue rx_queue tr tm->when uid\n");
/*
 *	This was very pretty but didn't work when a socket is destroyed at the wrong moment
 *	(eg a syn recv socket getting a reset), or a memory timer destroy. Instead of playing
 *	with timers we just concede defeat and cli().
 */
	for(i = 0; i < SOCK_ARRAY_SIZE; i++) 
	{
	  	cli();
		sp = s_array[i];
		while(sp != NULL) 
		{
			dest  = sp->daddr;
			src   = sp->saddr;
			destp = sp->dummy_th.dest;
			srcp  = sp->dummy_th.source;

			/* Since we are Little Endian we need to swap the bytes :-( */
			destp = ntohs(destp);
			srcp  = ntohs(srcp);
			timer_active = del_timer(&sp->timer);
			if (!timer_active)
				sp->timer.expires = 0;
			len+=sprintf(buffer+len, "%2d: %08lX:%04X %08lX:%04X %02X %08lX:%08lX %02X:%08lX %08X %d %d\n",
				i, src, srcp, dest, destp, sp->state, 
				format==0?sp->write_seq-sp->rcv_ack_seq:sp->rmem_alloc, 
				format==0?sp->acked_seq-sp->copied_seq:sp->wmem_alloc,
				timer_active, sp->timer.expires, (unsigned) sp->retransmits,
				sp->socket?SOCK_INODE(sp->socket)->i_uid:0,
				timer_active?sp->timeout:0);
			if (timer_active)
				add_timer(&sp->timer);
			/*
			 * All sockets with (port mod SOCK_ARRAY_SIZE) = i
			 * are kept in sock_array[i], so we must follow the
			 * 'next' link to get them all.
			 */
			sp = sp->next;
			pos=begin+len;
			if(pos<offset)
			{
				len=0;
				begin=pos;
			}
			if(pos>offset+length)
				break;
		}
		sti();	/* We only turn interrupts back on for a moment, but because the interrupt queues anything built up
			   before this will clear before we jump back and cli, so it's not as bad as it looks */
		if(pos>offset+length)
			break;
	}
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
	  	len=length;
	return len;
} 


int tcp_get_info(char *buffer, char **start, off_t offset, int length)
{
	return get__netinfo(&tcp_prot, buffer,0, start, offset, length);
}


int udp_get_info(char *buffer, char **start, off_t offset, int length)
{
	return get__netinfo(&udp_prot, buffer,1, start, offset, length);
}


int raw_get_info(char *buffer, char **start, off_t offset, int length)
{
	return get__netinfo(&raw_prot, buffer,1, start, offset, length);
}


/*
 *	Report socket allocation statistics [mea@utu.fi]
 */
int afinet_get_info(char *buffer, char **start, off_t offset, int length)
{
	/* From  net/socket.c  */
	extern int socket_get_info(char *, char **, off_t, int);
	extern struct proto packet_prot;

	int len  = socket_get_info(buffer,start,offset,length);

	len += sprintf(buffer+len,"SOCK_ARRAY_SIZE=%d\n",SOCK_ARRAY_SIZE);
	len += sprintf(buffer+len,"TCP: inuse %d highest %d\n",
		       tcp_prot.inuse, tcp_prot.highestinuse);
	len += sprintf(buffer+len,"UDP: inuse %d highest %d\n",
		       udp_prot.inuse, udp_prot.highestinuse);
	len += sprintf(buffer+len,"RAW: inuse %d highest %d\n",
		       raw_prot.inuse, raw_prot.highestinuse);
	len += sprintf(buffer+len,"PAC: inuse %d highest %d\n",
		       packet_prot.inuse, packet_prot.highestinuse);
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	return len;
}


/* 
 *	Called from the PROCfs module. This outputs /proc/net/snmp.
 */
 
int snmp_get_info(char *buffer, char **start, off_t offset, int length)
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
		"Tcp: RtoAlgorithm RtoMin RtoMax MaxConn ActiveOpens PassiveOpens AttemptFails EstabResets CurrEstab InSegs OutSegs RetransSegs\n"
		"Tcp: %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		    tcp_statistics.TcpRtoAlgorithm, tcp_statistics.TcpRtoMin,
		    tcp_statistics.TcpRtoMax, tcp_statistics.TcpMaxConn,
		    tcp_statistics.TcpActiveOpens, tcp_statistics.TcpPassiveOpens,
		    tcp_statistics.TcpAttemptFails, tcp_statistics.TcpEstabResets,
		    tcp_statistics.TcpCurrEstab, tcp_statistics.TcpInSegs,
		    tcp_statistics.TcpOutSegs, tcp_statistics.TcpRetransSegs);
		
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
	return len;
}

