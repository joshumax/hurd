#ifndef __LINUX_FIREWALL_H
#define __LINUX_FIREWALL_H

#include <linux/config.h>

/*
 *	Definitions for loadable firewall modules
 */

#define FW_QUEUE	0
#define FW_BLOCK	1
#define FW_ACCEPT	2
#define FW_REJECT	(-1)
#define FW_REDIRECT	3
#define FW_MASQUERADE	4
#define FW_SKIP		5

struct firewall_ops
{
	struct firewall_ops *next;
	int (*fw_forward)(struct firewall_ops *this, int pf, 
			struct device *dev, void *phdr, void *arg, struct sk_buff **pskb);
	int (*fw_input)(struct firewall_ops *this, int pf, 
			struct device *dev, void *phdr, void *arg, struct sk_buff **pskb);
	int (*fw_output)(struct firewall_ops *this, int pf, 
			struct device *dev, void *phdr, void *arg, struct sk_buff **pskb);
	/* Data falling in the second 486 cache line isn't used directly
	   during a firewall call and scan, only by insert/delete and other
	   unusual cases
	 */
	int fw_pf;		/* Protocol family 			*/	
	int fw_priority;	/* Priority of chosen firewalls 	*/
};

#ifdef __KERNEL__
extern int register_firewall(int pf, struct firewall_ops *fw);
extern int unregister_firewall(int pf, struct firewall_ops *fw);
extern void fwchain_init(void);
#ifdef CONFIG_FIREWALL
extern int call_fw_firewall(int pf, struct device *dev, void *phdr, void *arg, struct sk_buff **pskb);
extern int call_in_firewall(int pf, struct device *dev, void *phdr, void *arg, struct sk_buff **pskb);
extern int call_out_firewall(int pf, struct device *dev, void *phdr, void *arg, struct sk_buff **pskb);
#else
static __inline__ int call_fw_firewall(int pf, struct device *dev, void *phdr, void *arg, struct sk_buff **skb)
{
	return FW_ACCEPT;
}

static __inline__ int call_in_firewall(int pf, struct device *dev, void *phdr, void *arg, struct sk_buff **skb)
{
	return FW_ACCEPT;
}

static __inline__ int call_out_firewall(int pf, struct device *dev, void *phdr, void *arg, struct sk_buff **skb)
{
	return FW_ACCEPT;
}

#endif
#endif
#endif
