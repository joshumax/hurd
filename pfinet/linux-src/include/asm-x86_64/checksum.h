#ifndef _ASM_X86_CHECKSUM_64_H
#define _ASM_X86_CHECKSUM_64_H

/*
 * Checksums for x86-64
 * Copyright 2002 by Andi Kleen, SuSE Labs
 * with some code from asm-x86/checksum.h
 */

#include <asm/uaccess.h>
#include <asm/byteorder.h>

/*
 *	Fold a partial checksum without adding pseudo headers
 */

static inline unsigned short csum_fold(unsigned int sum)
{
	sum = (sum & 0xffff) + (sum >> 16);
	sum = (sum & 0xffff) + (sum >> 16);
	return ~sum;
}

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 */
extern unsigned short ip_fast_csum(unsigned char * iph, unsigned int ihl);

/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */
extern unsigned short int csum_tcpudp_magic(unsigned long saddr,
					   unsigned long daddr,
					   unsigned short len,
					   unsigned short proto,
					   unsigned int sum);

unsigned int csum_tcpudp_nofold(unsigned long saddr, unsigned long daddr,
				unsigned short len, unsigned short proto,
				unsigned int sum);

/**
 * csum_partial - Compute an internet checksum.
 * @buff: buffer to be checksummed
 * @len: length of buffer.
 * @sum: initial sum to be added in (32bit unfolded)
 *
 * Returns the 32bit unfolded internet checksum of the buffer.
 * Before filling it in it needs to be csum_fold()'ed.
 * buff should be aligned to a 64bit boundary if possible.
 */
extern unsigned int csum_partial(const void *buff, int len, unsigned int sum);

#define  _HAVE_ARCH_COPY_AND_CSUM_FROM_USER 1
#define HAVE_CSUM_COPY_USER 1

static inline unsigned int 
csum_partial_copy(const char *src, char *dst, int len,unsigned int sum)
{
	memcpy(dst,src,len);
        return csum_partial(dst, len, sum);
}

/* Do not call this directly. Use the wrappers below */
static inline unsigned int
csum_partial_copy_generic(const void *src, void *dst,
			  int len, unsigned int sum,
			  int *src_err_ptr, int *dst_err_ptr)
{
	return csum_partial_copy(src, dst, len, sum);
}

static __inline__
unsigned int csum_partial_copy_to_user ( const void *src, void *dst,
					 int len, unsigned int sum, int *err_ptr)
{
	return csum_partial_copy_generic ( src, dst, len, sum, NULL, err_ptr);
}

static __inline__
unsigned int csum_partial_copy_from_user ( const void *src, void *dst,
					   int len, unsigned int sum, int *err_ptr)
{
	return csum_partial_copy_generic ( src, dst, len, sum, err_ptr, NULL);
}

extern unsigned int csum_partial_copy_nocheck(const void *src, void *dst,
					      int len, unsigned int sum);

#define csum_partial_copy_nocheck(src, dst, len, sum)	\
	csum_partial_copy((src), (dst), (len), (sum))

/* Old names. To be removed. */
#define csum_and_copy_to_user csum_partial_copy_to_user
#define csum_and_copy_from_user csum_partial_copy_from_user

/**
 * ip_compute_csum - Compute an 16bit IP checksum.
 * @buff: buffer address.
 * @len: length of buffer.
 *
 * Returns the 16bit folded/inverted checksum of the passed buffer.
 * Ready to fill in.
 */
extern unsigned short ip_compute_csum(const void *buff, int len);

#endif /* _ASM_X86_CHECKSUM_64_H */
