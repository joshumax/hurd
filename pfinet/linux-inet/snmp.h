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
};
 
struct udp_mib
{
 	unsigned long	UdpInDatagrams;
 	unsigned long	UdpNoPorts;
 	unsigned long	UdpInErrors;
 	unsigned long	UdpOutDatagrams;
};
 
 	
#endif
