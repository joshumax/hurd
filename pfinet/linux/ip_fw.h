/*
 *	IP firewalling code. This is taken from 4.4BSD. Please note the 
 *	copyright message below. As per the GPL it must be maintained
 *	and the licenses thus do not conflict. While this port is subject
 *	to the GPL I also place my modifications under the original 
 *	license in recognition of the original copyright. 
 *
 *	Ported from BSD to Linux,
 *		Alan Cox 22/Nov/1994.
 *	Merged and included the FreeBSD-Current changes at Ugen's request
 *	(but hey it's a lot cleaner now). Ugen would prefer in some ways
 *	we waited for his final product but since Linux 1.2.0 is about to
 *	appear it's not practical - Read: It works, it's not clean but please
 *	don't consider it to be his standard of finished work.
 *		Alan.
 *
 *	All the real work was done by .....
 */

/*
 * Copyright (c) 1993 Daniel Boulet
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

/*
 * 	Format of an IP firewall descriptor
 *
 * 	src, dst, src_mask, dst_mask are always stored in network byte order.
 * 	flags and num_*_ports are stored in host byte order (of course).
 * 	Port numbers are stored in HOST byte order.
 */
 
#ifndef _IP_FW_H
#define _IP_FW_H

struct ip_fw 
{
	struct ip_fw  *fw_next;			/* Next firewall on chain */
	struct in_addr fw_src, fw_dst;		/* Source and destination IP addr */
	struct in_addr fw_smsk, fw_dmsk;	/* Mask for src and dest IP addr */
	struct in_addr fw_via;			/* IP address of interface "via" */
	unsigned short fw_flg;			/* Flags word */
	unsigned short fw_nsp, fw_ndp;          /* N'of src ports and # of dst ports */
						/* in ports array (dst ports follow */
    						/* src ports; max of 10 ports in all; */
    						/* count of 0 means match all ports) */
#define IP_FW_MAX_PORTS	10      		/* A reasonable maximum */
	unsigned short fw_pts[IP_FW_MAX_PORTS]; /* Array of port numbers to match */
	unsigned long  fw_pcnt,fw_bcnt;		/* Packet and byte counters */
};

/*
 *	Values for "flags" field .
 */

#define IP_FW_F_ALL	0x000	/* This is a universal packet firewall*/
#define IP_FW_F_TCP	0x001	/* This is a TCP packet firewall      */
#define IP_FW_F_UDP	0x002	/* This is a UDP packet firewall      */
#define IP_FW_F_ICMP	0x003	/* This is a ICMP packet firewall     */
#define IP_FW_F_KIND	0x003	/* Mask to isolate firewall kind      */
#define IP_FW_F_ACCEPT	0x004	/* This is an accept firewall (as     *
				 *         opposed to a deny firewall)*
				 *                                    */
#define IP_FW_F_SRNG	0x008	/* The first two src ports are a min  *
				 * and max range (stored in host byte *
				 * order).                            *
				 *                                    */
#define IP_FW_F_DRNG	0x010	/* The first two dst ports are a min  *
				 * and max range (stored in host byte *
				 * order).                            *
				 * (ports[0] <= port <= ports[1])     *
				 *                                    */
#define IP_FW_F_PRN	0x020	/* In verbose mode print this firewall*/
#define IP_FW_F_BIDIR	0x040	/* For bidirectional firewalls        */
#define IP_FW_F_TCPSYN	0x080	/* For tcp packets-check SYN only     */
#define IP_FW_F_ICMPRPL 0x100	/* Send back icmp unreachable packet  */
#define IP_FW_F_MASK	0x1FF	/* All possible flag bits mask        */

/*    
 *	New IP firewall options for [gs]etsockopt at the RAW IP level.
 *	Unlike BSD Linux inherits IP options so you don't have to use
 *	a raw socket for this. Instead we check rights in the calls.
 */     

#define IP_FW_BASE_CTL   64

#define IP_FW_ADD_BLK    (IP_FW_BASE_CTL)
#define IP_FW_ADD_FWD    (IP_FW_BASE_CTL+1)   
#define IP_FW_CHK_BLK    (IP_FW_BASE_CTL+2)
#define IP_FW_CHK_FWD    (IP_FW_BASE_CTL+3)
#define IP_FW_DEL_BLK    (IP_FW_BASE_CTL+4)
#define IP_FW_DEL_FWD    (IP_FW_BASE_CTL+5)
#define IP_FW_FLUSH_BLK  (IP_FW_BASE_CTL+6)
#define IP_FW_FLUSH_FWD  (IP_FW_BASE_CTL+7)
#define IP_FW_ZERO_BLK   (IP_FW_BASE_CTL+8)
#define IP_FW_ZERO_FWD   (IP_FW_BASE_CTL+9)
#define IP_FW_POLICY_BLK (IP_FW_BASE_CTL+10)
#define IP_FW_POLICY_FWD (IP_FW_BASE_CTL+11)

#define IP_ACCT_ADD      (IP_FW_BASE_CTL+16)
#define IP_ACCT_DEL      (IP_FW_BASE_CTL+17)
#define IP_ACCT_FLUSH    (IP_FW_BASE_CTL+18)
#define IP_ACCT_ZERO     (IP_FW_BASE_CTL+19)

struct ip_fwpkt
{
	struct iphdr fwp_iph;			/* IP header */
	union {
		struct tcphdr fwp_tcph;		/* TCP header or */
		struct udphdr fwp_udph;		/* UDP header */
	} fwp_protoh;
	struct in_addr fwp_via;			/* interface address */
};

/*
 *	Main firewall chains definitions and global var's definitions.
 */

#ifdef __KERNEL__

#include <linux/config.h>

#ifdef CONFIG_IP_FIREWALL
extern struct ip_fw *ip_fw_blk_chain;
extern struct ip_fw *ip_fw_fwd_chain;
extern int ip_fw_blk_policy;
extern int ip_fw_fwd_policy;
extern int ip_fw_ctl(int, void *, int);
#endif
#ifdef CONFIG_IP_ACCT
extern struct ip_fw *ip_acct_chain;
extern void ip_acct_cnt(struct iphdr *, struct device *, struct ip_fw *);
extern int ip_acct_ctl(int, void *, int);
#endif
extern int ip_fw_chk(struct iphdr *, struct device *rif,struct ip_fw *, int, int);
#endif /* KERNEL */

#endif /* _IP_FW_H */
