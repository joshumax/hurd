/*
 *		IP_MASQ_PORTFW masquerading module
 *
 *
 *	$Id: ip_masq_portfw.c,v 1.3.2.2 1999/08/13 18:26:29 davem Exp $
 *
 * Author:	Steven Clarke <steven.clarke@monmouth.demon.co.uk>
 *
 * Fixes:	
 *	Juan Jose Ciarlante	: created this new file from ip_masq.c and ip_fw.c
 *	Juan Jose Ciarlante	: modularized 
 *	Juan Jose Ciarlante 	: use GFP_KERNEL
 *	Juan Jose Ciarlante 	: locking
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <net/ip.h>
#include <linux/ip_fw.h>
#include <linux/ip_masq.h>
#include <net/ip_masq.h>
#include <net/ip_masq_mod.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

#define IP_PORTFW_PORT_MIN 1
#define IP_PORTFW_PORT_MAX 60999

struct ip_portfw {
	struct 		list_head list;
	__u32           laddr, raddr;
	__u16           lport, rport;
	atomic_t	pref_cnt;	/* pref "counter" down to 0 */
	int 		pref;		/* user set pref */
};

static struct ip_masq_mod *mmod_self = NULL;
/*
 *	Debug level
 */
#ifdef CONFIG_IP_MASQ_DEBUG
static int debug=0;
MODULE_PARM(debug, "i");
#endif

/*
 *	Lock
 */
static rwlock_t portfw_lock = RW_LOCK_UNLOCKED;

static struct list_head portfw_list[2];
static __inline__ int portfw_idx(int protocol)
{
        return (protocol==IPPROTO_TCP);
}

/*
 *
 *	Delete forwarding entry(s):
 *	called from _DEL, u-space.
 *	. "relaxed" match, except for lport
 *
 */

static __inline__ int ip_portfw_del(__u16 protocol, __u16 lport, __u32 laddr, __u16 rport, __u32 raddr)
{
        int prot = portfw_idx(protocol);
        struct ip_portfw *n;
	struct list_head *entry;
	struct list_head *list = &portfw_list[prot];
	int nent;

	nent = atomic_read(&mmod_self->mmod_nent);

	write_lock_bh(&portfw_lock);

	for (entry=list->next;entry != list;entry = entry->next)  {
		n = list_entry(entry, struct ip_portfw, list);
		if (n->lport == lport && 
				(!laddr || n->laddr == laddr) &&
				(!raddr || n->raddr == raddr) && 
				(!rport || n->rport == rport)) {
			entry = n->list.prev;
			list_del(&n->list);
			ip_masq_mod_dec_nent(mmod_self);
			kfree_s(n, sizeof(struct ip_portfw));
			MOD_DEC_USE_COUNT;
		}
	}
	write_unlock_bh(&portfw_lock);
	
	return nent==atomic_read(&mmod_self->mmod_nent)? ESRCH : 0;
}

/*
 *	Flush tables
 *	called from _FLUSH, u-space.
 */
static __inline__ void ip_portfw_flush(void)
{
        int prot;
	struct list_head *l;
	struct list_head *e;
	struct ip_portfw *n;

	write_lock_bh(&portfw_lock);

	for (prot = 0; prot < 2;prot++) {
		l = &portfw_list[prot];
		while((e=l->next) != l) {
			ip_masq_mod_dec_nent(mmod_self);
			n = list_entry (e, struct ip_portfw, list);
			list_del(e);
			kfree_s(n, sizeof (*n));
			MOD_DEC_USE_COUNT;
		}
	}

	write_unlock_bh(&portfw_lock);
}

/*
 *	Lookup routine for lport,laddr match
 *	must be called with locked tables
 */
static __inline__ struct ip_portfw *ip_portfw_lookup(__u16 protocol, __u16 lport, __u32 laddr, __u32 *daddr_p, __u16 *dport_p)
{
	int prot = portfw_idx(protocol);
	
	struct ip_portfw *n = NULL;
	struct list_head *l, *e;

	l = &portfw_list[prot];

	for (e=l->next;e!=l;e=e->next) {
		n = list_entry(e, struct ip_portfw, list);
		if (lport == n->lport && laddr == n->laddr) {
			/* Please be nice, don't pass only a NULL dport */
			if (daddr_p) {
				*daddr_p = n->raddr;
				*dport_p = n->rport;
			}
			
			goto out;
		}
	}
	n = NULL;
out:
	return n;
}

/*
 *	Edit routine for lport,[laddr], [raddr], [rport] match
 *	By now, only called from u-space
 */
static __inline__ int ip_portfw_edit(__u16 protocol, __u16 lport, __u32 laddr, __u16 rport, __u32 raddr, int pref)
{
	int prot = portfw_idx(protocol);
	
	struct ip_portfw *n = NULL;
	struct list_head *l, *e;
	int count = 0;


	read_lock_bh(&portfw_lock);

	l = &portfw_list[prot];

	for (e=l->next;e!=l;e=e->next) {
		n = list_entry(e, struct ip_portfw, list);
		if (lport == n->lport && 
				(!laddr || laddr == n->laddr) &&
				(!rport || rport == n->rport) && 
				(!raddr || raddr == n->raddr)) {
			n->pref = pref;
			atomic_set(&n->pref_cnt, pref);
			count++;
		}
	}

	read_unlock_bh(&portfw_lock);

	return count;
}

/*
 *	Add/edit en entry
 *	called from _ADD, u-space.
 *	must return 0 or +errno
 */
static __inline__ int ip_portfw_add(__u16 protocol, __u16 lport, __u32 laddr, __u16 rport, __u32 raddr, int pref)
{
        struct ip_portfw  *npf;
        int prot = portfw_idx(protocol);
         
	if (pref <= 0)
		return EINVAL;

	if (ip_portfw_edit(protocol, lport, laddr, rport, raddr, pref)) {
		/*
		 *	Edit ok ...
		 */
		return 0;
	}

	/* may block ... */
	npf = (struct ip_portfw*) kmalloc(sizeof(struct ip_portfw), GFP_KERNEL);

	if (!npf)
		return ENOMEM;

	MOD_INC_USE_COUNT;
        memset(npf, 0, sizeof(*npf));

        npf->laddr = laddr;
        npf->lport = lport;
        npf->rport = rport;
        npf->raddr = raddr;
	npf->pref  = pref;

	atomic_set(&npf->pref_cnt, npf->pref);
	INIT_LIST_HEAD(&npf->list);

	write_lock_bh(&portfw_lock);

	/*
	 *	Add at head
	 */
	list_add(&npf->list, &portfw_list[prot]);

	write_unlock_bh(&portfw_lock);

	ip_masq_mod_inc_nent(mmod_self);
        return 0;
}



static __inline__ int portfw_ctl(int optname, struct ip_masq_ctl *mctl, int optlen)
{
        struct ip_portfw_user *mm =  &mctl->u.portfw_user;
	int ret = EINVAL;
	int arglen = optlen - IP_MASQ_CTL_BSIZE;
	int cmd;


	IP_MASQ_DEBUG(1-debug, "ip_masq_user_ctl(len=%d/%d|%d/%d)\n",
		arglen,
		sizeof (*mm),
		optlen,
		sizeof (*mctl));

	/*
	 *	Yes, I'm a bad guy ...
	 */
	if (arglen != sizeof(*mm) && optlen != sizeof(*mctl)) 
		return EINVAL;
 
	/* 
	 *	Don't trust the lusers - plenty of error checking! 
	 */
	cmd = mctl->m_cmd;
	IP_MASQ_DEBUG(1-debug, "ip_masq_portfw_ctl(cmd=%d)\n", cmd);


	switch (cmd) {
		case IP_MASQ_CMD_NONE:
			return 0;
		case IP_MASQ_CMD_FLUSH:
			break;
		default:
			if (htons(mm->lport) < IP_PORTFW_PORT_MIN || htons(mm->lport) > IP_PORTFW_PORT_MAX)
				return EINVAL;

			if (mm->protocol!=IPPROTO_TCP && mm->protocol!=IPPROTO_UDP)
				return EINVAL;
	}

	switch(cmd) {
	case IP_MASQ_CMD_ADD:
		ret = ip_portfw_add(mm->protocol,
				mm->lport, mm->laddr,
				mm->rport, mm->raddr,
				mm->pref);
		break;

	case IP_MASQ_CMD_DEL:
		ret = ip_portfw_del(mm->protocol, 
				mm->lport, mm->laddr,
				mm->rport, mm->raddr);
		break;
	case IP_MASQ_CMD_FLUSH:
		ip_portfw_flush();
		ret = 0;
		break;
	}
				

	return ret;
}




#ifdef CONFIG_PROC_FS

static int portfw_procinfo(char *buffer, char **start, off_t offset,
                            int length, int unused)
{
        off_t pos=0, begin;
        struct ip_portfw *pf;
	struct list_head *l, *e;
        char temp[65];
        int ind;
        int len=0;


        if (offset < 64) 
        {
                sprintf(temp, "Prot LAddr    LPort > RAddr    RPort PrCnt  Pref");
                len = sprintf(buffer, "%-63s\n", temp);
        }
        pos = 64;

	read_lock_bh(&portfw_lock);

        for(ind = 0; ind < 2; ind++)
        {
		l = &portfw_list[ind];
		for (e=l->next; e!=l; e=e->next)
                {
			pf = list_entry(e, struct ip_portfw, list);
                        pos += 64;
                        if (pos <= offset) {
				len = 0;
                                continue;
			}

                        sprintf(temp,"%s  %08lX %5u > %08lX %5u %5d %5d",
                                ind ? "TCP" : "UDP",
				ntohl(pf->laddr), ntohs(pf->lport),
				ntohl(pf->raddr), ntohs(pf->rport),
				atomic_read(&pf->pref_cnt), pf->pref);
                        len += sprintf(buffer+len, "%-63s\n", temp);

                        if (len >= length)
                                goto done;
		}
        }
done:
	read_unlock_bh(&portfw_lock);

        begin = len - (pos - offset);
        *start = buffer + begin;
        len -= begin;
        if(len>length)
                len = length;
        return len;
}

static struct proc_dir_entry portfw_proc_entry = {
/* 		0, 0, NULL", */
		0, 6, "portfw",   /* Just for compatibility, for now ... */
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		portfw_procinfo
};

#define proc_ent &portfw_proc_entry
#else /* !CONFIG_PROC_FS */

#define proc_ent NULL
#endif

static int portfw_in_rule(const struct sk_buff *skb, const struct iphdr *iph)
{
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
#ifdef CONFIG_IP_MASQ_DEBUG
	struct rtable *rt = (struct rtable *)skb->dst;
#endif
	struct ip_portfw *pfw;

	IP_MASQ_DEBUG(2, "portfw_in_rule(): skb:= dev=%s (index=%d), rt_iif=%d, rt_flags=0x%x rt_dev___=%s daddr=%d.%d.%d.%d dport=%d\n",
		skb->dev->name, skb->dev->ifindex, rt->rt_iif, rt->rt_flags,
		rt->u.dst.dev->name,
		NIPQUAD(iph->daddr), ntohs(portp[1]));

	read_lock(&portfw_lock);
	pfw = ip_portfw_lookup(iph->protocol, portp[1], iph->daddr, NULL, NULL);
	read_unlock(&portfw_lock);
	return (pfw!=0);
}

static struct ip_masq * portfw_in_create(const struct sk_buff *skb, const struct iphdr *iph, __u32 maddr)
{
	/* 
	 *	If no entry exists in the masquerading table
 	 * 	and the port is involved
	 *  	in port forwarding, create a new masq entry 
	 */

	__u32 raddr;
	__u16 rport;
	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
	struct ip_masq *ms = NULL;
	struct ip_portfw *pf;

	/*
	 *	Lock for writing.
	 */
	write_lock(&portfw_lock);

	if ((pf=ip_portfw_lookup(iph->protocol, 
			portp[1], iph->daddr, 
			&raddr, &rport))) {
		ms = ip_masq_new(iph->protocol,
				iph->daddr, portp[1],	
				raddr, rport,
				iph->saddr, portp[0],
				0);
		if (!ms || atomic_read(&mmod_self->mmod_nent) <= 1 
			/* || ip_masq_nlocks(&portfw_lock) != 1 */ )
				/*
				 *	Maybe later...
				 */
				goto out;

		ip_masq_listen(ms);

		/*
		 *	Entry created, lock==1.
		 *	if pref_cnt == 0, move
		 *	entry at _tail_.
		 *	This is a simple load balance scheduling
		 */
	
		if (atomic_dec_and_test(&pf->pref_cnt)) {

			atomic_set(&pf->pref_cnt, pf->pref);
			list_del(&pf->list);
			list_add(&pf->list, 
				portfw_list[portfw_idx(iph->protocol)].prev);

		}
	}
out:
	write_unlock(&portfw_lock);
	return ms;
}

#define portfw_in_update	NULL
#define portfw_out_rule		NULL
#define portfw_out_create	NULL
#define portfw_out_update	NULL

static struct ip_masq_mod portfw_mod = {
	NULL,			/* next */
	NULL,			/* next_reg */
	"portfw",		/* name */
	ATOMIC_INIT(0),		/* nent */
	ATOMIC_INIT(0),		/* refcnt */
	proc_ent,
	portfw_ctl,
	NULL,			/* masq_mod_init */
	NULL,			/* masq_mod_done */
	portfw_in_rule,
	portfw_in_update,
	portfw_in_create,
	portfw_out_rule,
	portfw_out_update,
	portfw_out_create,
};



__initfunc(int ip_portfw_init(void))
{
	INIT_LIST_HEAD(&portfw_list[0]);
	INIT_LIST_HEAD(&portfw_list[1]);
	return register_ip_masq_mod ((mmod_self=&portfw_mod));
}

int ip_portfw_done(void)
{
	return unregister_ip_masq_mod(&portfw_mod);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	if (ip_portfw_init() != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	if (ip_portfw_done() != 0)
		printk(KERN_INFO "ip_portfw_done(): can't remove module");
}

#endif /* MODULE */
