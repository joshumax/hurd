#ifdef __KERNEL__
/* Compatibility for various Linux kernel versions */

#ifndef _LINUX_ISDN_COMPAT_H
#define _LINUX_ISDN_COMPAT_H

#ifndef LINUX_VERSION_CODE
#include <linux/version.h>
#endif

#ifndef KERNEL_VERSION
#define KERNEL_VERSION(x,y,z) (((x)<<16)+((y)<<8)+(z))
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
#include <linux/mm.h>

#define ioremap vremap
#define ioremap_nocache vremap
#define iounmap vfree

static inline unsigned long copy_from_user(void *to, const void *from, unsigned long n)
{
	int i;
	if ((i = verify_area(VERIFY_READ, from, n)) != 0)
		return i;
	memcpy_fromfs(to, from, n);
	return 0;
}

static inline unsigned long copy_to_user(void *to, const void *from, unsigned long n)
{
	int i;
	if ((i = verify_area(VERIFY_WRITE, to, n)) != 0)
		return i;
	memcpy_tofs(to, from, n);
	return 0;
}

#define GET_USER(x, addr) ( x = get_user(addr) )
#ifdef __alpha__ /* needed for 2.0.x with alpha-patches */
#define RWTYPE long
#define LSTYPE long
#define RWARG unsigned long
#else
#define RWTYPE int
#define LSTYPE int
#define RWARG int
#endif
#define LSARG off_t
#else
#include <asm/uaccess.h>
#define GET_USER get_user
#define PUT_USER put_user
#define RWTYPE long
#define LSTYPE long long
#define RWARG unsigned long
#define LSARG long long
#endif /* LINUX_VERSION_CODE */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,15)
#define SET_SKB_FREE(x) ( x->free = 1 )
#define idev_kfree_skb(a,b) dev_kfree_skb(a,b)
#else
#define SET_SKB_FREE(x)
#define idev_kfree_skb(a,b) dev_kfree_skb(a)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,1,18)
#define COMPAT_HAS_NEW_SYMTAB
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,31)
#define CLOSETYPE void
#define CLOSEVAL
#else
#define CLOSETYPE int
#define CLOSEVAL (0)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,37)
#define test_and_clear_bit clear_bit
#define test_and_set_bit set_bit
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,81)
#define kstat_irqs( PAR ) kstat.interrupts[PAR]
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,1,91)
#define COMPAT_HAS_NEW_PCI
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,13)
#define get_pcibase(ps, nr) ps->base_address[nr]
#else
#define get_pcibase(ps, nr) ps->resource[nr].start
#endif
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,127)
#define schedule_timeout(a) current->timeout = jiffies + (a); schedule ();
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,3,0)
#define COMPAT_HAS_NEW_WAITQ
#endif

#endif /* __KERNEL__ */
#endif /* _LINUX_ISDN_COMPAT_H */
