#ifndef _ADDRCONF_H
#define _ADDRCONF_H

#include "ipv6.h"

#define RETRANS_TIMER	HZ

#define MAX_RTR_SOLICITATIONS		3
#define RTR_SOLICITATION_INTERVAL	(4*HZ)

#define ADDR_CHECK_FREQUENCY		(120*HZ)

struct prefix_info {
	__u8			type;
	__u8			length;
	__u8			prefix_len;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8			onlink : 1,
			 	autoconf : 1,
				reserved : 6;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8			reserved : 6,
				autoconf : 1,
				onlink : 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	__u32			valid;
	__u32			prefered;
	__u32			reserved2;

	struct in6_addr		prefix;
};


#ifdef __KERNEL__

#include <linux/in6.h>
#include <linux/netdevice.h>
#include <net/if_inet6.h>

#define IN6_ADDR_HSIZE		16

extern void			addrconf_init(void);
extern void			addrconf_cleanup(void);

extern int		        addrconf_notify(struct notifier_block *this, 
						unsigned long event, 
						void * data);

extern int			addrconf_add_ifaddr(void *arg);
extern int			addrconf_del_ifaddr(void *arg);
extern int			addrconf_set_dstaddr(void *arg);

extern struct inet6_ifaddr *	ipv6_chk_addr(struct in6_addr *addr,
					      struct device *dev, int nd);
extern int			ipv6_get_saddr(struct dst_entry *dst, 
					       struct in6_addr *daddr,
					       struct in6_addr *saddr);
extern struct inet6_ifaddr *	ipv6_get_lladdr(struct device *dev);

/*
 *	multicast prototypes (mcast.c)
 */
extern int			ipv6_sock_mc_join(struct sock *sk, 
						  int ifindex, 
						  struct in6_addr *addr);
extern int			ipv6_sock_mc_drop(struct sock *sk,
						  int ifindex, 
						  struct in6_addr *addr);
extern void			ipv6_sock_mc_close(struct sock *sk);

extern int			ipv6_dev_mc_inc(struct device *dev,
						struct in6_addr *addr);
extern int			ipv6_dev_mc_dec(struct device *dev,
						struct in6_addr *addr);
extern void			ipv6_mc_up(struct inet6_dev *idev);
extern void			ipv6_mc_down(struct inet6_dev *idev);
extern void			ipv6_mc_destroy_dev(struct inet6_dev *idev);
extern void			addrconf_dad_failure(struct inet6_ifaddr *ifp);

extern int			ipv6_chk_mcast_addr(struct device *dev,
						    struct in6_addr *addr);

extern void			addrconf_prefix_rcv(struct device *dev,
						    u8 *opt, int len);

extern struct inet6_dev *	ipv6_get_idev(struct device *dev);

extern void			addrconf_forwarding_on(void);
/*
 *	Hash function taken from net_alias.c
 */

static __inline__ u8 ipv6_addr_hash(struct in6_addr *addr)
{	
	__u32 word;
	unsigned tmp;

	/* 
	 * We perform the hash function over the last 64 bits of the address
	 * This will include the IEEE address token on links that support it.
	 */

	word = addr->s6_addr[2] ^ addr->s6_addr32[3];
	tmp  = word ^ (word>>16);
	tmp ^= (tmp >> 8);

	return ((tmp ^ (tmp >> 4)) & 0x0f);
}

static __inline__ int ipv6_devindex_hash(int ifindex)
{
	return ifindex & (IN6_ADDR_HSIZE - 1);
}

/*
 *	compute link-local solicited-node multicast address
 */

static __inline__ void addrconf_addr_solict_mult_old(struct in6_addr *addr,
						     struct in6_addr *solicited)
{
	ipv6_addr_set(solicited,
		      __constant_htonl(0xFF020000), 0,
		      __constant_htonl(0x1), addr->s6_addr32[3]);
}

static __inline__ void addrconf_addr_solict_mult_new(struct in6_addr *addr,
						     struct in6_addr *solicited)
{
	ipv6_addr_set(solicited,
		      __constant_htonl(0xFF020000), 0,
		      __constant_htonl(0x1),
		      __constant_htonl(0xFF000000) | addr->s6_addr32[3]);
}


static __inline__ void ipv6_addr_all_nodes(struct in6_addr *addr)
{
	ipv6_addr_set(addr,
		      __constant_htonl(0xFF020000), 0, 0,
		      __constant_htonl(0x1));
}

static __inline__ void ipv6_addr_all_routers(struct in6_addr *addr)
{
	ipv6_addr_set(addr,
		      __constant_htonl(0xFF020000), 0, 0,
		      __constant_htonl(0x2));
}

static __inline__ int ipv6_addr_is_multicast(struct in6_addr *addr)
{
	return (addr->s6_addr32[0] & __constant_htonl(0xFF000000)) == __constant_htonl(0xFF000000);
}

#endif
#endif
