/* linux/net/inet/arp.h */
#ifndef _ARP_H
#define _ARP_H

extern void	arp_init(void);
extern void	arp_destroy(unsigned long paddr, int force);
extern void	arp_device_down(struct device *dev);
extern int	arp_rcv(struct sk_buff *skb, struct device *dev,
			struct packet_type *pt);
extern int	arp_find(unsigned char *haddr, unsigned long paddr,
		struct device *dev, unsigned long saddr, struct sk_buff *skb);
extern int	arp_get_info(char *buffer, char **start, off_t origin, int length);
extern int	arp_ioctl(unsigned int cmd, void *arg);
extern void     arp_send(int type, int ptype, unsigned long dest_ip, 
			 struct device *dev, unsigned long src_ip, 
			 unsigned char *dest_hw, unsigned char *src_hw);

#endif	/* _ARP_H */
