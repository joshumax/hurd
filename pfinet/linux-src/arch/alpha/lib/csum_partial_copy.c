/*
 * csum_partial_copy - do IP checksumming and copy
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * Don't look at this too closely - you'll go mad. The things
 * we do for performance..
 */

#include <linux/types.h>
#include <linux/string.h>
#include <asm/uaccess.h>


#define ldq_u(x,y) \
__asm__ __volatile__("ldq_u %0,%1":"=r" (x):"m" (*(const unsigned long *)(y)))

#define stq_u(x,y) \
__asm__ __volatile__("stq_u %1,%0":"=m" (*(unsigned long *)(y)):"r" (x))

#define extql(x,y,z) \
__asm__ __volatile__("extql %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define extqh(x,y,z) \
__asm__ __volatile__("extqh %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define mskql(x,y,z) \
__asm__ __volatile__("mskql %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define mskqh(x,y,z) \
__asm__ __volatile__("mskqh %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define insql(x,y,z) \
__asm__ __volatile__("insql %1,%2,%0":"=r" (z):"r" (x),"r" (y))

#define insqh(x,y,z) \
__asm__ __volatile__("insqh %1,%2,%0":"=r" (z):"r" (x),"r" (y))


#define __get_user_u(x,ptr)				\
({							\
	long __guu_err;					\
	__asm__ __volatile__(				\
	"1:	ldq_u %0,%2\n"				\
	"2:\n"						\
	".section __ex_table,\"a\"\n"			\
	"	.gprel32 1b\n"				\
	"	lda %0,2b-1b(%1)\n"			\
	".previous"					\
		: "=r"(x), "=r"(__guu_err)		\
		: "m"(__m(ptr)), "1"(0));		\
	__guu_err;					\
})

#define __put_user_u(x,ptr)				\
({							\
	long __puu_err;					\
	__asm__ __volatile__(				\
	"1:	stq_u %2,%1\n"				\
	"2:\n"						\
	".section __ex_table,\"a\"\n"			\
	"	.gprel32 1b"				\
	"	lda $31,2b-1b(%0)\n"			\
	".previous"					\
		: "=r"(__puu_err)			\
		: "m"(__m(addr)), "rJ"(x), "0"(0));	\
	__puu_err;					\
})


/*
 * Ok. This isn't fun, but this is the EASY case.
 */
static inline unsigned long
csum_partial_cfu_aligned(const unsigned long *src, unsigned long *dst,
			 long len, unsigned long checksum,
			 int *errp)
{
	unsigned long carry = 0;
	int err = 0;

	while (len >= 0) {
		unsigned long word;
		err |= __get_user(word, src);
		checksum += carry;
		src++;
		checksum += word;
		len -= 8;
		carry = checksum < word;
		*dst = word;
		dst++;
	}
	len += 8;
	checksum += carry;
	if (len) {
		unsigned long word, tmp;
		err |= __get_user(word, src);
		tmp = *dst;
		mskql(word, len, word);
		checksum += word;
		mskqh(tmp, len, tmp);
		carry = checksum < word;
		*dst = word | tmp;
		checksum += carry;
	}
	if (err) *errp = err;
	return checksum;
}

/*
 * This is even less fun, but this is still reasonably
 * easy.
 */
static inline unsigned long
csum_partial_cfu_dest_aligned(const unsigned long *src, unsigned long *dst,
			      unsigned long soff,
			      long len, unsigned long checksum,
			      int *errp)
{
	unsigned long first;
	unsigned long word, carry;
	unsigned long lastsrc = 7+len+(unsigned long)src;
	int err = 0;

	err |= __get_user_u(first,src);
	carry = 0;
	while (len >= 0) {
		unsigned long second;

		err |= __get_user_u(second, src+1);
		extql(first, soff, word);
		len -= 8;
		src++;
		extqh(second, soff, first);
		checksum += carry;
		word |= first;
		first = second;
		checksum += word;
		*dst = word;
		dst++;
		carry = checksum < word;
	}
	len += 8;
	checksum += carry;
	if (len) {
		unsigned long tmp;
		unsigned long second;
		err |= __get_user_u(second, lastsrc);
		tmp = *dst;
		extql(first, soff, word);
		extqh(second, soff, first);
		word |= first;
		mskql(word, len, word);
		checksum += word;
		mskqh(tmp, len, tmp);
		carry = checksum < word;
		*dst = word | tmp;
		checksum += carry;
	}
	if (err) *errp = err;
	return checksum;
}

/*
 * This is slightly less fun than the above..
 */
static inline unsigned long
csum_partial_cfu_src_aligned(const unsigned long *src, unsigned long *dst,
			     unsigned long doff,
			     long len, unsigned long checksum,
			     unsigned long partial_dest,
			     int *errp)
{
	unsigned long carry = 0;
	unsigned long word;
	int err = 0;

	mskql(partial_dest, doff, partial_dest);
	while (len >= 0) {
		unsigned long second_dest;
		err |= __get_user(word, src);
		len -= 8;
		insql(word, doff, second_dest);
		checksum += carry;
		stq_u(partial_dest | second_dest, dst);
		src++;
		checksum += word;
		insqh(word, doff, partial_dest);
		carry = checksum < word;
		dst++;
	}
	len += doff;
	checksum += carry;
	if (len >= 0) {
		unsigned long second_dest;
		err |= __get_user(word, src);
		mskql(word, len-doff, word);
		checksum += word;
		insql(word, doff, second_dest);
		stq_u(partial_dest | second_dest, dst);
		carry = checksum < word;
		if (len) {
			ldq_u(second_dest, dst+1);
			insqh(word, doff, partial_dest);
			mskqh(second_dest, len, second_dest);
			stq_u(partial_dest | second_dest, dst+1);
		}
		checksum += carry;
	} else if (len & 7) {
		unsigned long second_dest;
		err |= __get_user(word, src);
		ldq_u(second_dest, dst);
		mskql(word, len-doff, word);
		checksum += word;
		mskqh(second_dest, len, second_dest);
		carry = checksum < word;
		insql(word, doff, word);
		stq_u(partial_dest | word | second_dest, dst);
		checksum += carry;
	}
	if (err) *errp = err;
	return checksum;
}

/*
 * This is so totally un-fun that it's frightening. Don't
 * look at this too closely, you'll go blind.
 */
static inline unsigned long
csum_partial_cfu_unaligned(const unsigned long * src, unsigned long * dst,
			   unsigned long soff, unsigned long doff,
			   long len, unsigned long checksum,
			   unsigned long partial_dest,
			   int *errp)
{
	unsigned long carry = 0;
	unsigned long first;
	unsigned long lastsrc;
	int err = 0;

	err |= __get_user_u(first, src);
	lastsrc = 7+len+(unsigned long)src;
	mskql(partial_dest, doff, partial_dest);
	while (len >= 0) {
		unsigned long second, word;
		unsigned long second_dest;

		err |= __get_user_u(second, src+1);
		extql(first, soff, word);
		checksum += carry;
		len -= 8;
		extqh(second, soff, first);
		src++;
		word |= first;
		first = second;
		insql(word, doff, second_dest);
		checksum += word;
		stq_u(partial_dest | second_dest, dst);
		carry = checksum < word;
		insqh(word, doff, partial_dest);
		dst++;
	}
	len += doff;
	checksum += carry;
	if (len >= 0) {
		unsigned long second, word;
		unsigned long second_dest;

		err |= __get_user_u(second, lastsrc);
		extql(first, soff, word);
		extqh(second, soff, first);
		word |= first;
		first = second;
		mskql(word, len-doff, word);
		checksum += word;
		insql(word, doff, second_dest);
		carry = checksum < word;
		stq_u(partial_dest | second_dest, dst);
		if (len) {
			ldq_u(second_dest, dst+1);
			insqh(word, doff, partial_dest);
			mskqh(second_dest, len, second_dest);
			stq_u(partial_dest | second_dest, dst+1);
		}
		checksum += carry;
	} else if (len & 7) {
		unsigned long second, word;
		unsigned long second_dest;

		err |= __get_user_u(second, lastsrc);
		extql(first, soff, word);
		extqh(second, soff, first);
		word |= first;
		ldq_u(second_dest, dst);
		mskql(word, len-doff, word);
		checksum += word;
		mskqh(second_dest, len, second_dest);
		carry = checksum < word;
		insql(word, doff, word);
		stq_u(partial_dest | word | second_dest, dst);
		checksum += carry;
	}
	if (err) *errp = err;
	return checksum;
}

static unsigned int
do_csum_partial_copy_from_user(const char *src, char *dst, int len,
			       unsigned int sum, int *errp)
{
	unsigned long checksum = (unsigned) sum;
	unsigned long soff = 7 & (unsigned long) src;
	unsigned long doff = 7 & (unsigned long) dst;

	if (len) {
		if (!doff) {
			if (!soff)
				checksum = csum_partial_cfu_aligned(
					(const unsigned long *) src,
					(unsigned long *) dst,
					len-8, checksum, errp);
			else
				checksum = csum_partial_cfu_dest_aligned(
					(const unsigned long *) src,
					(unsigned long *) dst,
					soff, len-8, checksum, errp);
		} else {
			unsigned long partial_dest;
			ldq_u(partial_dest, dst);
			if (!soff)
				checksum = csum_partial_cfu_src_aligned(
					(const unsigned long *) src,
					(unsigned long *) dst,
					doff, len-8, checksum,
					partial_dest, errp);
			else
				checksum = csum_partial_cfu_unaligned(
					(const unsigned long *) src,
					(unsigned long *) dst,
					soff, doff, len-8, checksum,
					partial_dest, errp);
		}
		/* 64 -> 33 bits */
		checksum = (checksum & 0xffffffff) + (checksum >> 32);
		/* 33 -> < 32 bits */
		checksum = (checksum & 0xffff) + (checksum >> 16);
		/* 32 -> 16 bits */
		checksum = (checksum & 0xffff) + (checksum >> 16);
		checksum = (checksum & 0xffff) + (checksum >> 16);
	}
	return checksum;
}

unsigned int
csum_partial_copy_from_user(const char *src, char *dst, int len,
			    unsigned int sum, int *errp)
{
	if (!access_ok(src, len, VERIFY_READ)) {
		*errp = -EFAULT;
		memset(dst, 0, len);
		return sum;
	}

	return do_csum_partial_copy_from_user(src, dst, len, sum, errp);
}

unsigned int
csum_partial_copy_nocheck(const char *src, char *dst, int len, unsigned int sum)
{
	return do_csum_partial_copy_from_user(src, dst, len, sum, NULL);
}

unsigned int
csum_partial_copy (const char *src, char *dst, int len, unsigned int sum)
{
	unsigned int ret;
	int error = 0;

	ret = do_csum_partial_copy_from_user(src, dst, len, sum, &error);
	if (error)
		printk("csum_partial_copy_old(): tell mingo to convert me!\n");

	return ret;
}
