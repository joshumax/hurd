/*
 *
 *		SNMP MIB entries for the IP subsystem.
 *		
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *
 *		We don't chose to implement SNMP in the kernel (this would
 *		be silly as SNMP is a pain in the backside in places). We do
 *		however need to collect the MIB statistics and export them
 *		out of /proc (eventually)
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 */
 
#ifndef _SNMP_H
#define _SNMP_H
 
/*
 *	We use all unsigned longs. Linux will soon be so reliable that even these
 *	will rapidly get too small 8-). Seriously consider the IpInReceives count
 *	on the 20Gb/s + networks people expect in a few years time!
 */
  
struct ip_mib
{
 	unsigned long	IpForwarding;
 	unsigned long	IpDefaultTTL;
 	unsigned long	IpInReceives;
 	unsigned long	IpInHdrErrors;
 	unsigned long	IpInAddrErrors;
 	unsigned long	IpForwDatagrams;
 	unsigned long	IpInUnknownProtos;
 	unsigned long	IpInDiscards;
 	unsigned long	IpInDelivers;
 	unsigned long	IpOutRequests;
 	unsigned long	IpOutDiscards;
 	unsigned long	IpOutNoRoutes;
 	unsigned long	IpReasmTimeout;
 	unsigned long	IpReasmReqds;
 	unsigned long	IpReasmOKs;
 	unsigned long	IpReasmFails;
 	unsigned long	IpFragOKs;
 	unsigned long	IpFragFails;
 	unsigned long	IpFragCreates;
};
 
struct ipv6_mib
{
	unsigned long	Ip6InReceives;
 	unsigned long	Ip6InHdrErrors;
 	unsigned long	Ip6InTooBigErrors;
 	unsigned long	Ip6InNoRoutes;
 	unsigned long	Ip6InAddrErrors;
 	unsigned long	Ip6InUnknownProtos;
 	unsigned long	Ip6InTruncatedPkts;
 	unsigned long	Ip6InDiscards;
 	unsigned long	Ip6InDelivers;
 	unsigned long	Ip6OutForwDatagrams;
 	unsigned long	Ip6OutRequests;
 	unsigned long	Ip6OutDiscards;
 	unsigned long	Ip6OutNoRoutes;
 	unsigned long	Ip6ReasmTimeout;
 	unsigned long	Ip6ReasmReqds;
 	unsigned long	Ip6ReasmOKs;
 	unsigned long	Ip6ReasmFails;
 	unsigned long	Ip6FragOKs;
 	unsigned long	Ip6FragFails;
 	unsigned long	Ip6FragCreates;
 	unsigned long	Ip6InMcastPkts;
 	unsigned long	Ip6OutMcastPkts;
};
 
struct icmp_mib
{
 	unsigned long	IcmpInMsgs;
 	unsigned long	IcmpInErrors;
  	unsigned long	IcmpInDestUnreachs;
 	unsigned long	IcmpInTimeExcds;
 	unsigned long	IcmpInParmProbs;
 	unsigned long	IcmpInSrcQuenchs;
 	unsigned long	IcmpInRedirects;
 	unsigned long	IcmpInEchos;
 	unsigned long	IcmpInEchoReps;
 	unsigned long	IcmpInTimestamps;
 	unsigned long	IcmpInTimestampReps;
 	unsigned long	IcmpInAddrMasks;
 	unsigned long	IcmpInAddrMaskReps;
 	unsigned long	IcmpOutMsgs;
 	unsigned long	IcmpOutErrors;
 	unsigned long	IcmpOutDestUnreachs;
 	unsigned long	IcmpOutTimeExcds;
 	unsigned long	IcmpOutParmProbs;
 	unsigned long	IcmpOutSrcQuenchs;
 	unsigned long	IcmpOutRedirects;
 	unsigned long	IcmpOutEchos;
 	unsigned long	IcmpOutEchoReps;
 	unsigned long	IcmpOutTimestamps;
 	unsigned long	IcmpOutTimestampReps;
 	unsigned long	IcmpOutAddrMasks;
 	unsigned long	IcmpOutAddrMaskReps;
};

struct icmpv6_mib
{
	unsigned long	Icmp6InMsgs;
	unsigned long	Icmp6InErrors;

	unsigned long	Icmp6InDestUnreachs;
	unsigned long	Icmp6InPktTooBigs;
	unsigned long	Icmp6InTimeExcds;
	unsigned long	Icmp6InParmProblems;

	unsigned long	Icmp6InEchos;
	unsigned long	Icmp6InEchoReplies;
	unsigned long	Icmp6InGroupMembQueries;
	unsigned long	Icmp6InGroupMembResponses;
	unsigned long	Icmp6InGroupMembReductions;
	unsigned long	Icmp6InRouterSolicits;
	unsigned long	Icmp6InRouterAdvertisements;
	unsigned long	Icmp6InNeighborSolicits;
	unsigned long	Icmp6InNeighborAdvertisements;
	unsigned long	Icmp6InRedirects;

	unsigned long	Icmp6OutMsgs;

	unsigned long	Icmp6OutDestUnreachs;
	unsigned long	Icmp6OutPktTooBigs;
	unsigned long	Icmp6OutTimeExcds;
	unsigned long	Icmp6OutParmProblems;

	unsigned long	Icmp6OutEchoReplies;
	unsigned long	Icmp6OutRouterSolicits;
	unsigned long	Icmp6OutNeighborSolicits;
	unsigned long	Icmp6OutNeighborAdvertisements;
	unsigned long	Icmp6OutRedirects;
	unsigned long	Icmp6OutGroupMembResponses;
	unsigned long	Icmp6OutGroupMembReductions;
};
 
struct tcp_mib
{
 	unsigned long	TcpRtoAlgorithm;
 	unsigned long	TcpRtoMin;
 	unsigned long	TcpRtoMax;
 	unsigned long	TcpMaxConn;
 	unsigned long	TcpActiveOpens;
 	unsigned long	TcpPassiveOpens;
 	unsigned long	TcpAttemptFails;
 	unsigned long	TcpEstabResets;
 	unsigned long	TcpCurrEstab;
 	unsigned long	TcpInSegs;
 	unsigned long	TcpOutSegs;
 	unsigned long	TcpRetransSegs;
 	unsigned long	TcpInErrs;
 	unsigned long	TcpOutRsts;
};
 
struct udp_mib
{
 	unsigned long	UdpInDatagrams;
 	unsigned long	UdpNoPorts;
 	unsigned long	UdpInErrors;
 	unsigned long	UdpOutDatagrams;
};

struct linux_mib 
{
	unsigned long	SyncookiesSent;
	unsigned long	SyncookiesRecv;
	unsigned long	SyncookiesFailed;
	unsigned long	EmbryonicRsts;
	unsigned long	PruneCalled; 
	unsigned long	RcvPruned;
	unsigned long	OfoPruned;
	unsigned long	OutOfWindowIcmps; 
	unsigned long	LockDroppedIcmps; 
};
 	
#endif
