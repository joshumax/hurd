/*
 * linux/include/asm-arm/checksum.h
 *
 * IP checksum routines
 *
 * Copyright (C) Original authors of ../asm-i386/checksum.h
 * Copyright (C) 1996,1997,1998 Russell King
 */
#ifndef __ASM_ARM_CHECKSUM_H
#define __ASM_ARM_CHECKSUM_H

#ifndef __ASM_ARM_SEGMENT_H
#include <asm/segment.h>
#endif

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic
 *
 * this function must be called with even lengths, except
 * for the last fragment, which may be odd
 *
 * it's best to have buff aligned on a 32-bit boundary
 */
unsigned int csum_partial(const unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums, and handles user-space pointer exceptions correctly, when needed.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

unsigned int
csum_partial_copy_nocheck(const char *src, char *dst, int len, int sum);

unsigned int
csum_partial_copy_from_user(const char *src, char *dst, int len, int sum, int *err_ptr);

#if 0
/*
 * This combination is currently not used, but possible:
 */
unsigned int
csum_partial_copy_to_user(const char *src, char *dst, int len, int sum, int *err_ptr);
#endif

/*
 * These are the old (and unsafe) way of doing checksums, a warning message will be
 * printed if they are used and an exception occurs.
 *
 * these functions should go away after some time.
 */
#define csum_partial_copy_fromuser csum_partial_copy
unsigned int
csum_partial_copy(const char *src, char *dst, int len, int sum);

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	Converted and optimised for ARM by R. M. King.
 *
 *	Note: the order that the LDM registers are loaded with respect to
 *	the adc's doesn't matter.
 */
static inline unsigned short
ip_fast_csum(unsigned char * iph, unsigned int ihl)
{
	unsigned int sum, tmp1;

    __asm__ __volatile__("
	sub	%2, %2, #5
	ldr	%0, [%1], #4
	ldr	%3, [%1], #4
	adds	%0, %0, %3
	ldr	%3, [%1], #4
	adcs	%0, %0, %3
	ldr	%3, [%1], #4
	adcs	%0, %0, %3
1:	ldr	%3, [%1], #4
	adcs	%0, %0, %3
	tst	%2, #15
	subne	%2, %2, #1
	bne	1b
	adc	%0, %0, #0
	adds	%0, %0, %0, lsl #16
	addcs	%0, %0, #0x10000
	mvn	%0, %0
	mov	%0, %0, lsr #16
	"
	: "=&r" (sum), "=&r" (iph), "=&r" (ihl), "=&r" (tmp1)
	: "1" (iph), "2" (ihl));
	return(sum);
}

/*
 * 	Fold a partial checksum without adding pseudo headers
 */
static inline unsigned int
csum_fold(unsigned int sum)
{
	__asm__("
	adds	%0, %0, %0, lsl #16
	addcs	%0, %0, #0x10000"
	: "=r" (sum)
	: "0" (sum));
	return (~sum) >> 16;
}

static inline unsigned long
csum_tcpudp_nofold(unsigned long saddr, unsigned long daddr, unsigned short len,
		   unsigned short proto, unsigned int sum)
{
	__asm__("
	adds	%0, %0, %1
	adcs	%0, %0, %2
	adcs	%0, %0, %3
	adc	%0, %0, #0"
	: "=&r"(sum)
	: "r" (daddr), "r" (saddr), "r" ((ntohs(len)<<16)+proto*256), "0" (sum));
	return sum;
}	
/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
static inline unsigned short int
csum_tcpudp_magic(unsigned long saddr, unsigned long daddr, unsigned short len,
		  unsigned short proto, unsigned int sum)
{
	return csum_fold(csum_tcpudp_nofold(saddr, daddr, len, proto, sum));
}


/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */
static inline unsigned short
ip_compute_csum(unsigned char * buff, int len)
{
	return csum_fold(csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
extern unsigned long
__csum_ipv6_magic(struct in6_addr *saddr, struct in6_addr *daddr, __u32 len,
		__u32 proto, unsigned int sum);

extern __inline__ unsigned short int
csum_ipv6_magic(struct in6_addr *saddr, struct in6_addr *daddr, __u16 len,
		unsigned short proto, unsigned int sum)
{
	return csum_fold(__csum_ipv6_magic(saddr, daddr, htonl((__u32)len),
					   htonl(proto), sum));
}
#endif
