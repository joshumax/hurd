/*
 *		IP_MASQ_AUTOFW auto forwarding module
 *
 *
 * 	$Id: ip_masq_autofw.c,v 1.3.2.1 1999/08/13 18:26:20 davem Exp $
 *
 * Author:	Richard Lynch
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *
 * Fixes:
 *	Juan Jose Ciarlante	: created this new file from ip_masq.c and ip_fw.c
 *	Juan Jose Ciarlante	: modularized 
 *	Juan Jose Ciarlante	: use GFP_KERNEL when creating entries
 *	Juan Jose Ciarlante	: call del_timer() when freeing entries (!)
 *  FIXME:
 *	- implement refcnt
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <asm/system.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/if.h>
#include <linux/init.h>
#include <linux/ip_fw.h>
#include <net/ip_masq.h>
#include <net/ip_masq_mod.h>
#include <linux/ip_masq.h>

#define IP_AUTOFW_EXPIRE	     15*HZ

/* WARNING: bitwise equal to ip_autofw_user  in linux/ip_masq.h */
struct ip_autofw {
	struct ip_autofw * next;
	__u16 type;
	__u16 low;
	__u16 hidden;
	__u16 high;
	__u16 visible;
	__u16 protocol;
	__u32 lastcontact;
	__u32 where;
	__u16 ctlproto;
	__u16 ctlport;
	__u16 flags;
	struct timer_list timer;
};

/*
 *	Debug level
 */
#ifdef CONFIG_IP_MASQ_DEBUG
static int debug=0;
MODULE_PARM(debug, "i");
#endif

/*
 *	Auto-forwarding table
 */

static struct ip_autofw * ip_autofw_hosts = NULL;
static struct ip_masq_mod * mmod_self = NULL;

/*
 *	Check if a masq entry should be created for a packet
 */

static __inline__ struct ip_autofw * ip_autofw_check_range (__u32 where, __u16 port, __u16 protocol, int reqact)
{
	struct ip_autofw *af;
	af=ip_autofw_hosts;
	port=ntohs(port);
	while (af) {
		if (af->type==IP_FWD_RANGE && 
		     port>=af->low && 
		     port<=af->high && 
		     protocol==af->protocol && 

		     /*
		      *		It's ok to create masq entries after 
		      *		the timeout if we're in insecure mode 
		      */
		     (af->flags & IP_AUTOFW_ACTIVE || !reqact || !(af->flags & IP_AUTOFW_SECURE)) &&  
		     (!(af->flags & IP_AUTOFW_SECURE) || af->lastcontact==where || !reqact))
			return(af);
		af=af->next;
	}
	return(NULL);
}

static __inline__ struct ip_autofw * ip_autofw_check_port (__u16 port, __u16 protocol)
{
	struct ip_autofw *af;
	af=ip_autofw_hosts;
	port=ntohs(port);
	while (af)
	{
		if (af->type==IP_FWD_PORT && port==af->visible && protocol==af->protocol)
			return(af);
		af=af->next;
	}
	return(NULL);
}

static __inline__ struct ip_autofw * ip_autofw_check_direct (__u16 port, __u16 protocol)
{
	struct ip_autofw *af;
	af=ip_autofw_hosts;
	port=ntohs(port);
	while (af)
	{
		if (af->type==IP_FWD_DIRECT && af->low<=port && af->high>=port)
			return(af);
		af=af->next;
	}
	return(NULL);
}

static __inline__ void ip_autofw_update_out (__u32 who, __u32 where, __u16 port, __u16 protocol)
{
	struct ip_autofw *af;
	af=ip_autofw_hosts;
	port=ntohs(port);
	while (af)
	{
		if (af->type==IP_FWD_RANGE && af->ctlport==port && af->ctlproto==protocol)
		{
			if (af->flags & IP_AUTOFW_USETIME)
			{
				mod_timer(&af->timer,
					  jiffies+IP_AUTOFW_EXPIRE);
			}
			af->flags|=IP_AUTOFW_ACTIVE;
			af->lastcontact=where;
			af->where=who;
		}
		af=af->next;
	}
}

#if 0
static __inline__ void ip_autofw_update_in (__u32 where, __u16 port, __u16 protocol)
{
	struct ip_autofw *af;
	af=ip_autofw_check_range(where, port,protocol);
	if (af)
	{
		mod_timer(&af->timer, jiffies+IP_AUTOFW_EXPIRE);
	}
}
#endif


static __inline__ void ip_autofw_expire(unsigned long data)
{
	struct ip_autofw * af;
	af=(struct ip_autofw *) data;
	af->flags &= ~IP_AUTOFW_ACTIVE;
	af->timer.expires=0;
	af->lastcontact=0;
	if (af->flags & IP_AUTOFW_SECURE)
		af->where=0;
}



static __inline__ int ip_autofw_add(struct ip_autofw_user * af)
{
	struct ip_autofw * newaf;
	newaf = kmalloc( sizeof(struct ip_autofw), GFP_KERNEL );
	if ( newaf == NULL ) 
	{
		printk("ip_autofw_add:  malloc said no\n");
		return( ENOMEM );
	}

	init_timer(&newaf->timer);
	MOD_INC_USE_COUNT;

	memcpy(newaf, af, sizeof(struct ip_autofw_user));
	newaf->timer.data = (unsigned long) newaf;
	newaf->timer.function = ip_autofw_expire;
	newaf->timer.expires = 0;
	newaf->lastcontact=0;
	newaf->next=ip_autofw_hosts;
	ip_autofw_hosts=newaf;
	ip_masq_mod_inc_nent(mmod_self);
	return(0);
}

static __inline__ int ip_autofw_del(struct ip_autofw_user * af)
{
	struct ip_autofw ** af_p, *curr;

	for (af_p=&ip_autofw_hosts, curr=*af_p; (curr=*af_p); af_p = &(*af_p)->next) {
		if (af->type     == curr->type &&
		    af->low      == curr->low &&
		    af->high     == curr->high &&
		    af->hidden   == curr->hidden &&
		    af->visible  == curr->visible &&
		    af->protocol == curr->protocol &&
		    af->where    == curr->where &&
		    af->ctlproto == curr->ctlproto &&
		    af->ctlport  == curr->ctlport)
		{
			ip_masq_mod_dec_nent(mmod_self);
			*af_p = curr->next;
			if (af->flags&IP_AUTOFW_ACTIVE)
				del_timer(&curr->timer);
			kfree_s(curr,sizeof(struct ip_autofw));
			MOD_DEC_USE_COUNT;
			return 0;
		}
		curr=curr->next;
	}
	return EINVAL;
}

static __inline__ int ip_autofw_flush(void)
{
	struct ip_autofw * af;

	while (ip_autofw_hosts)
	{
		af=ip_autofw_hosts;
		ip_masq_mod_dec_nent(mmod_self);
		ip_autofw_hosts=ip_autofw_hosts->next;
		if (af->flags&IP_AUTOFW_ACTIVE)
			del_timer(&af->timer);
		kfree_s(af,sizeof(struct ip_autofw));
		MOD_DEC_USE_COUNT;
	}
	return(0);
}

/*
 *	Methods for registered object
 */

static int autofw_ctl(int optname, struct ip_masq_ctl *mctl, int optlen)
{
	struct ip_autofw_user *af = &mctl->u.autofw_user;

	switch (mctl->m_cmd) {
		case IP_MASQ_CMD_ADD:
		case IP_MASQ_CMD_INSERT:
			if (optlen<sizeof(*af))
				return EINVAL;
			return ip_autofw_add(af);
		case IP_MASQ_CMD_DEL:
			if (optlen<sizeof(*af))
				return EINVAL;
			return ip_autofw_del(af);
		case IP_MASQ_CMD_FLUSH:
			return ip_autofw_flush();

	}
	return EINVAL;
}


static int autofw_out_update(const struct sk_buff *skb, const struct iphdr *iph, struct ip_masq *ms)
{
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
	/* 
	 *	Update any ipautofw entries ...
	 */

	ip_autofw_update_out(iph->saddr, iph->daddr, portp[1], iph->protocol);
	return IP_MASQ_MOD_NOP;
}

static struct ip_masq * autofw_out_create(const struct sk_buff *skb, const struct iphdr *iph, __u32 maddr)
{
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
	/*
	 *	If the source port is supposed to match the masq port, then
	 *  	make it so 
	 */

	if (ip_autofw_check_direct(portp[1],iph->protocol)) {
		return ip_masq_new(iph->protocol,
					maddr, portp[0],
					iph->saddr, portp[0],
					iph->daddr, portp[1],
					0);
	}
	return NULL;
}

#if 0
static int autofw_in_update(const struct sk_buff *skb, const struct iphdr *iph, __u16 *portp, struct ip_masq *ms)
{
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
	ip_autofw_update_in(iph->saddr, portp[1], iph->protocol);
	return IP_MASQ_MOD_NOP;
}
#endif

static int autofw_in_rule(const struct sk_buff *skb, const struct iphdr *iph)
{
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
	return (ip_autofw_check_range(iph->saddr, portp[1], iph->protocol, 0)
		|| ip_autofw_check_direct(portp[1], iph->protocol)
		|| ip_autofw_check_port(portp[1], iph->protocol));
}

static struct ip_masq * autofw_in_create(const struct sk_buff *skb, const struct iphdr *iph, __u32 maddr)
{
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
	struct ip_autofw *af;

        if ((af=ip_autofw_check_range(iph->saddr, portp[1], iph->protocol, 0))) {
		IP_MASQ_DEBUG(1-debug, "autofw_check_range HIT\n");
		return ip_masq_new(iph->protocol,
					maddr, portp[1],
					af->where, portp[1],
					iph->saddr, portp[0],
					0);
        } 
        if ((af=ip_autofw_check_port(portp[1], iph->protocol)) ) {
		IP_MASQ_DEBUG(1-debug, "autofw_check_port HIT\n");
		return ip_masq_new(iph->protocol,
					maddr, htons(af->visible),
					af->where, htons(af->hidden),
					iph->saddr, portp[0],
					0);
        }
	return NULL;
}

#ifdef CONFIG_PROC_FS
static int autofw_procinfo(char *buffer, char **start, off_t offset,
			      int length, int unused)
{
	off_t pos=0, begin=0;
	struct ip_autofw * af;
	int len=0;
	
	len=sprintf(buffer,"Type Prot Low  High Vis  Hid  Where    Last     CPto CPrt Timer Flags\n"); 
        
        for(af = ip_autofw_hosts; af ; af = af->next)
	{
		len+=sprintf(buffer+len,"%4X %4X %04X-%04X/%04X %04X %08lX %08lX %04X %04X %6lu %4X\n",
					af->type,
					af->protocol,
					af->low,
					af->high,
					af->visible,
					af->hidden,
					ntohl(af->where),
					ntohl(af->lastcontact),
					af->ctlproto,
					af->ctlport,
					(af->timer.expires<jiffies ? 0 : af->timer.expires-jiffies), 
					af->flags);

		pos=begin+len;
		if(pos<offset) 
		{
 			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
        }
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	return len;
}

static struct proc_dir_entry autofw_proc_entry = {
		0, 0, NULL,
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		autofw_procinfo
};

#define proc_ent &autofw_proc_entry
#else /* !CONFIG_PROC_FS */

#define proc_ent NULL
#endif


#define	autofw_in_update NULL
#define autofw_out_rule NULL
#define autofw_mod_init NULL
#define autofw_mod_done NULL

static struct ip_masq_mod autofw_mod = {
	NULL,			/* next */
	NULL,			/* next_reg */
	"autofw",		/* name */
	ATOMIC_INIT(0),		/* nent */
	ATOMIC_INIT(0),		/* refcnt */
	proc_ent,
	autofw_ctl,
	autofw_mod_init,
	autofw_mod_done,
	autofw_in_rule,
	autofw_in_update,
	autofw_in_create,
	autofw_out_rule,
	autofw_out_update,
	autofw_out_create,
};

__initfunc(int ip_autofw_init(void))
{
	return register_ip_masq_mod ((mmod_self=&autofw_mod));
}

int ip_autofw_done(void)
{
	return unregister_ip_masq_mod(&autofw_mod);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	if (ip_autofw_init() != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	if (ip_autofw_done() != 0)
		printk(KERN_INFO "ip_autofw_done(): can't remove module");
}

#endif /* MODULE */
