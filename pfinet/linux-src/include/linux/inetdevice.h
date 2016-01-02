#ifndef _LINUX_INETDEVICE_H
#define _LINUX_INETDEVICE_H

#ifdef __KERNEL__

struct ipv4_devconf
{
	int	accept_redirects;
	int	send_redirects;
	int	secure_redirects;
	int	shared_media;
	int	accept_source_route;
	int	rp_filter;
	int	proxy_arp;
	int	bootp_relay;
	int	log_martians;
	int	forwarding;
	int	mc_forwarding;
	int	hidden;
	void	*sysctl;
};

extern struct ipv4_devconf ipv4_devconf;

struct in_device
{
	struct device		*dev;
	struct in_ifaddr	*ifa_list;	/* IP ifaddr chain		*/
	struct ip_mc_list	*mc_list;	/* IP multicast filter chain    */
	unsigned long		mr_v1_seen;
	unsigned		flags;
	struct neigh_parms	*arp_parms;
	struct ipv4_devconf	cnf;
};

#define IN_DEV_FORWARD(in_dev)		((in_dev)->cnf.forwarding)
#define IN_DEV_MFORWARD(in_dev)		(ipv4_devconf.mc_forwarding && (in_dev)->cnf.mc_forwarding)
#define IN_DEV_RPFILTER(in_dev)		(ipv4_devconf.rp_filter && (in_dev)->cnf.rp_filter)
#define IN_DEV_SOURCE_ROUTE(in_dev)	(ipv4_devconf.accept_source_route && (in_dev)->cnf.accept_source_route)
#define IN_DEV_BOOTP_RELAY(in_dev)	(ipv4_devconf.bootp_relay && (in_dev)->cnf.bootp_relay)

#define IN_DEV_LOG_MARTIANS(in_dev)	(ipv4_devconf.log_martians || (in_dev)->cnf.log_martians)
#define IN_DEV_PROXY_ARP(in_dev)	(ipv4_devconf.proxy_arp || (in_dev)->cnf.proxy_arp)
#define IN_DEV_HIDDEN(in_dev)		((in_dev)->cnf.hidden && ipv4_devconf.hidden)
#define IN_DEV_SHARED_MEDIA(in_dev)	(ipv4_devconf.shared_media || (in_dev)->cnf.shared_media)
#define IN_DEV_TX_REDIRECTS(in_dev)	(ipv4_devconf.send_redirects || (in_dev)->cnf.send_redirects)
#define IN_DEV_SEC_REDIRECTS(in_dev)	(ipv4_devconf.secure_redirects || (in_dev)->cnf.secure_redirects)

#define IN_DEV_RX_REDIRECTS(in_dev) \
	((IN_DEV_FORWARD(in_dev) && \
	  (ipv4_devconf.accept_redirects && (in_dev)->cnf.accept_redirects)) \
	 || (!IN_DEV_FORWARD(in_dev) && \
	  (ipv4_devconf.accept_redirects || (in_dev)->cnf.accept_redirects)))

struct in_ifaddr
{
	struct in_ifaddr	*ifa_next;
	struct in_device	*ifa_dev;
	u32			ifa_local;
	u32			ifa_address;
	u32			ifa_mask;
	u32			ifa_broadcast;
	u32			ifa_anycast;
	unsigned char		ifa_scope;
	unsigned char		ifa_flags;
	unsigned char		ifa_prefixlen;
	char			ifa_label[IFNAMSIZ];
};

extern int register_inetaddr_notifier(struct notifier_block *nb);
extern int unregister_inetaddr_notifier(struct notifier_block *nb);

extern struct device 	*ip_dev_find(u32 addr);
extern struct in_ifaddr	*inet_addr_onlink(struct in_device *in_dev, u32 a, u32 b);
extern int		devinet_ioctl(unsigned int cmd, void *);
extern void		devinet_init(void);
extern struct in_device *inetdev_init(struct device *dev);
extern struct in_device	*inetdev_by_index(int);
extern u32		inet_select_addr(struct device *dev, u32 dst, int scope);
extern struct in_ifaddr *inet_ifa_byprefix(struct in_device *in_dev, u32 prefix, u32 mask);
extern void		inet_forward_change(void);

static __inline__ int inet_ifa_match(u32 addr, struct in_ifaddr *ifa)
{
	return !((addr^ifa->ifa_address)&ifa->ifa_mask);
}

/*
 *	Check if a mask is acceptable.
 */
 
static __inline__ int bad_mask(u32 mask, u32 addr)
{
	if (addr & (mask = ~mask))
		return 1;
	mask = ntohl(mask);
	if (mask & (mask+1))
		return 1;
	return 0;
}

#define for_primary_ifa(in_dev)	{ struct in_ifaddr *ifa; \
  for (ifa = (in_dev)->ifa_list; ifa && !(ifa->ifa_flags&IFA_F_SECONDARY); ifa = ifa->ifa_next)

#define for_ifa(in_dev)	{ struct in_ifaddr *ifa; \
  for (ifa = (in_dev)->ifa_list; ifa; ifa = ifa->ifa_next)


#define endfor_ifa(in_dev) }

#endif /* __KERNEL__ */

static __inline__ __u32 inet_make_mask(int logmask)
{
	if (logmask)
		return htonl(~((1<<(32-logmask))-1));
	return 0;
}

static __inline__ int inet_mask_len(__u32 mask)
{
	if (!(mask = ntohl(mask)))
		return 0;
	return 32 - ffz(~mask);
}


#endif /* _LINUX_INETDEVICE_H */
