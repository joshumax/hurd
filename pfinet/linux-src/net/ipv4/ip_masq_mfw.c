/*
 *		IP_MASQ_MARKFW masquerading module
 *
 *	Does (reverse-masq) forwarding based on skb->fwmark value
 *
 *	$Id: ip_masq_mfw.c,v 1.3.2.3 1999/09/22 16:33:26 davem Exp $
 *
 * Author:	Juan Jose Ciarlante   <jjciarla@raiz.uncu.edu.ar>
 *		  based on Steven Clarke's portfw
 *
 * Fixes:	
 *	JuanJo Ciarlante:	added u-space sched support
 *	JuanJo Ciarlante:	if rport==0, use packet dest port *grin*
 *	JuanJo Ciarlante:	fixed tcp syn&&!ack creation
 *
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
#include <asm/softirq.h>
#include <asm/spinlock.h>
#include <asm/atomic.h>

static struct ip_masq_mod *mmod_self = NULL;
#ifdef CONFIG_IP_MASQ_DEBUG
static int debug=0;
MODULE_PARM(debug, "i");
#endif

/*
 *  Lists structure:
 *	There is a "main" linked list with entries hashed
 *	by fwmark value (struct ip_masq_mfw, the "m-entries").
 *
 *	Each of this m-entry holds a double linked list
 *	of "forward-to" hosts (struct ip_masq_mfw_host, the "m.host"),
 *	the round-robin scheduling takes place by rotating m.host entries
 *	"inside" its m-entry.
 */

/*
 *	Each forwarded host (addr:port) is stored here
 */
struct ip_masq_mfw_host {
	struct 	list_head list;
	__u32 	addr;
	__u16	port;
	__u16	pad0;
	__u32 	fwmark;
	int 	pref;
	atomic_t	pref_cnt;
};

#define IP_MASQ_MFW_HSIZE	16
/*
 *	This entries are indexed by fwmark, 
 *	they hold a list of forwarded addr:port
 */	

struct ip_masq_mfw {
	struct ip_masq_mfw *next;	/* linked list */
	__u32 fwmark;			/* key: firewall mark */
	struct list_head hosts;		/* list of forward-to hosts */
	atomic_t nhosts;		/* number of "" */
	rwlock_t lock;
};


static struct semaphore mfw_sema = MUTEX;
static rwlock_t mfw_lock = RW_LOCK_UNLOCKED;

static struct ip_masq_mfw *ip_masq_mfw_table[IP_MASQ_MFW_HSIZE];

static __inline__ int mfw_hash_val(int fwmark)
{
	return fwmark & 0x0f;
}

/*
 *	Get m-entry by "fwmark"
 *	Caller must lock tables.
 */

static struct ip_masq_mfw *__mfw_get(int fwmark)
{
	struct ip_masq_mfw* mfw;
	int hash = mfw_hash_val(fwmark);

	for (mfw=ip_masq_mfw_table[hash];mfw;mfw=mfw->next) {
		if (mfw->fwmark==fwmark) {
			goto out;
		}
	}
out:
	return mfw;
}

/*
 *	Links m-entry.
 *	Caller should have checked if already present for same fwmark
 *
 *	Caller must lock tables.
 */
static int __mfw_add(struct ip_masq_mfw *mfw)
{
	int fwmark = mfw->fwmark;
	int hash = mfw_hash_val(fwmark);

	mfw->next = ip_masq_mfw_table[hash];
	ip_masq_mfw_table[hash] = mfw;
	ip_masq_mod_inc_nent(mmod_self);

	return 0;
}

/*
 *	Creates a m-entry (doesn't link it)
 */

static struct ip_masq_mfw * mfw_new(int fwmark)
{
	struct ip_masq_mfw *mfw;

	mfw = kmalloc(sizeof(*mfw), GFP_KERNEL);
	if (mfw == NULL) 
		goto out;

	MOD_INC_USE_COUNT;
	memset(mfw, 0, sizeof(*mfw));
	mfw->fwmark = fwmark;
	mfw->lock = RW_LOCK_UNLOCKED;

	INIT_LIST_HEAD(&mfw->hosts);
out:
	return mfw;
}

static void mfw_host_to_user(struct ip_masq_mfw_host *h, struct ip_mfw_user *mu)
{
	mu->raddr = h->addr;
	mu->rport = h->port;
	mu->fwmark = h->fwmark;
	mu->pref = h->pref;
}

/*
 *	Creates a m.host (doesn't link it in a m-entry)
 */
static struct ip_masq_mfw_host * mfw_host_new(struct ip_mfw_user *mu)
{
	struct ip_masq_mfw_host * mfw_host;
	mfw_host = kmalloc(sizeof (*mfw_host), GFP_KERNEL);
	if (!mfw_host)
		return NULL;

	MOD_INC_USE_COUNT;
	memset(mfw_host, 0, sizeof(*mfw_host));
	mfw_host->addr = mu->raddr;
	mfw_host->port = mu->rport;
	mfw_host->fwmark = mu->fwmark;
	mfw_host->pref = mu->pref;
	atomic_set(&mfw_host->pref_cnt, mu->pref);

	return mfw_host;
}

/*
 *	Create AND link m.host to m-entry.
 *	It locks m.lock.
 */
static int mfw_addhost(struct ip_masq_mfw *mfw, struct ip_mfw_user *mu, int attail)
{
	struct ip_masq_mfw_host *mfw_host;

	mfw_host = mfw_host_new(mu);
	if (!mfw_host) 
		return -ENOMEM;

	write_lock_bh(&mfw->lock);
	list_add(&mfw_host->list, attail? mfw->hosts.prev : &mfw->hosts);
	atomic_inc(&mfw->nhosts);
	write_unlock_bh(&mfw->lock);

	return 0;
}

/*
 *	Unlink AND destroy m.host(s) from m-entry.
 *	Wildcard (nul host or addr) ok.
 *	It uses m.lock.
 */
static int mfw_delhost(struct ip_masq_mfw *mfw, struct ip_mfw_user *mu)
{

	struct list_head *l,*e;
	struct ip_masq_mfw_host *h;
	int n_del = 0;
	l = &mfw->hosts;

	write_lock_bh(&mfw->lock);
	for (e=l->next; e!=l; e=e->next)
	{
		h = list_entry(e, struct ip_masq_mfw_host, list);
		if ((!mu->raddr || h->addr == mu->raddr) && 
			(!mu->rport || h->port == mu->rport)) {
			/* HIT */
			atomic_dec(&mfw->nhosts);
			e = h->list.prev;
			list_del(&h->list);
			kfree_s(h, sizeof(*h));
			MOD_DEC_USE_COUNT;
			n_del++;
		}
				
	}
	write_unlock_bh(&mfw->lock);
	return n_del? 0 : -ESRCH;
}

/*
 *	Changes m.host parameters
 *	Wildcards ok
 *
 *	Caller must lock tables.
 */
static int __mfw_edithost(struct ip_masq_mfw *mfw, struct ip_mfw_user *mu)
{

	struct list_head *l,*e;
	struct ip_masq_mfw_host *h;
	int n_edit = 0;
	l = &mfw->hosts;

	for (e=l->next; e!=l; e=e->next)
	{
		h = list_entry(e, struct ip_masq_mfw_host, list);
		if ((!mu->raddr || h->addr == mu->raddr) && 
			(!mu->rport || h->port == mu->rport)) {
			/* HIT */
			h->pref = mu->pref;
			atomic_set(&h->pref_cnt, mu->pref);
			n_edit++;
		}
				
	}
	return n_edit? 0 : -ESRCH;
}

/*
 *	Destroys m-entry.
 *	Caller must have checked that it doesn't hold any m.host(s)
 */
static void mfw_destroy(struct ip_masq_mfw *mfw)
{
	kfree_s(mfw, sizeof(*mfw));
	MOD_DEC_USE_COUNT;
}

/* 
 *	Unlink m-entry.
 *
 *	Caller must lock tables.
 */
static int __mfw_del(struct ip_masq_mfw *mfw)
{
	struct ip_masq_mfw **mfw_p;
	int ret = -EINVAL;


	for(mfw_p=&ip_masq_mfw_table[mfw_hash_val(mfw->fwmark)]; 
			*mfw_p; 
			mfw_p = &((*mfw_p)->next)) 
	{
		if (mfw==(*mfw_p)) {
			*mfw_p = mfw->next;
			ip_masq_mod_dec_nent(mmod_self);
			ret = 0;
			goto out;
		}
	}
out:
	return ret;
}

/*
 *	Crude m.host scheduler
 *	This interface could be exported to allow playing with 
 *	other sched policies.
 *
 *	Caller must lock m-entry.
 */
static struct ip_masq_mfw_host * __mfw_sched(struct ip_masq_mfw *mfw, int force)
{
	struct ip_masq_mfw_host *h = NULL;

	if (atomic_read(&mfw->nhosts) == 0)
		goto out;

	/*
	 *	Here resides actual sched policy: 
	 *	When pref_cnt touches 0, entry gets shifted to tail and
	 *	its pref_cnt reloaded from h->pref (actual value
	 *	passed from u-space).
	 *
	 *	Exception is pref==0: avoid scheduling.
	 */

	h = list_entry(mfw->hosts.next, struct ip_masq_mfw_host, list);

	if (atomic_read(&mfw->nhosts) <= 1)
		goto out;

	if ((h->pref && atomic_dec_and_test(&h->pref_cnt)) || force) {
		atomic_set(&h->pref_cnt, h->pref);
		list_del(&h->list);
		list_add(&h->list, mfw->hosts.prev);
	}
out:
	return h;
}

/*
 *	Main lookup routine.
 *	HITs fwmark and schedules m.host entries if required
 */
static struct ip_masq_mfw_host * mfw_lookup(int fwmark)
{
	struct ip_masq_mfw *mfw;
	struct ip_masq_mfw_host *h = NULL;

	read_lock(&mfw_lock);
	mfw = __mfw_get(fwmark);

	if (mfw) {
		write_lock(&mfw->lock);
		h = __mfw_sched(mfw, 0);
		write_unlock(&mfw->lock);
	}

	read_unlock(&mfw_lock);
	return h;
}

#ifdef CONFIG_PROC_FS
static int mfw_procinfo(char *buffer, char **start, off_t offset,
			      int length, int dummy)
{
	struct ip_masq_mfw *mfw;
	struct ip_masq_mfw_host *h;
	struct list_head *l,*e;
	off_t pos=0, begin;
	char temp[129];
        int idx = 0;
	int len=0;

	MOD_INC_USE_COUNT;

	IP_MASQ_DEBUG(1-debug, "Entered mfw_info\n");

	if (offset < 64)
	{
                sprintf(temp, "FwMark > RAddr    RPort PrCnt  Pref");
		len = sprintf(buffer, "%-63s\n", temp);
	}
	pos = 64;

        for(idx = 0; idx < IP_MASQ_MFW_HSIZE; idx++)
	{
		read_lock(&mfw_lock);
		for(mfw = ip_masq_mfw_table[idx]; mfw ; mfw = mfw->next)
		{
			read_lock_bh(&mfw->lock);
			l=&mfw->hosts;

			for(e=l->next;l!=e;e=e->next) {
				h = list_entry(e, struct ip_masq_mfw_host, list);
				pos += 64;
				if (pos <= offset) {
					len = 0;
					continue;
				}

				sprintf(temp,"0x%x > %08lX %5u %5d %5d",
						h->fwmark,
						ntohl(h->addr), ntohs(h->port),
						atomic_read(&h->pref_cnt), h->pref);
				len += sprintf(buffer+len, "%-63s\n", temp);

				if(len >= length) {
					read_unlock_bh(&mfw->lock);
					read_unlock(&mfw_lock);
					goto done;
				}
			}
			read_unlock_bh(&mfw->lock);
		}
		read_unlock(&mfw_lock);
	}

done:

	if (len) {
		begin = len - (pos - offset);
		*start = buffer + begin;
		len -= begin;
	}
	if(len>length)
		len = length;
	MOD_DEC_USE_COUNT;
	return len;
}
static struct proc_dir_entry mfw_proc_entry = {
/* 		0, 0, NULL", */
		0, 3, "mfw",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		mfw_procinfo
};

#define proc_ent &mfw_proc_entry
#else /* !CONFIG_PROC_FS */

#define proc_ent NULL
#endif


static void mfw_flush(void)
{
	struct ip_masq_mfw *mfw, *local_table[IP_MASQ_MFW_HSIZE];
	struct ip_masq_mfw_host *h;
	struct ip_masq_mfw *mfw_next;
	int idx;
	struct list_head *l,*e;

	write_lock_bh(&mfw_lock);
	memcpy(local_table, ip_masq_mfw_table, sizeof ip_masq_mfw_table);
	memset(ip_masq_mfw_table, 0, sizeof ip_masq_mfw_table);
	write_unlock_bh(&mfw_lock);

	/*
	 *	For every hash table row ...
	 */
	for(idx=0;idx<IP_MASQ_MFW_HSIZE;idx++) {

		/*
		 *	For every m-entry in row ...
		 */
		for(mfw=local_table[idx];mfw;mfw=mfw_next) {
			/*
			 *	For every m.host in m-entry ...
			 */
			l=&mfw->hosts;
			while((e=l->next) != l) {
				h = list_entry(e, struct ip_masq_mfw_host, list);
				atomic_dec(&mfw->nhosts);
				list_del(&h->list);
				kfree_s(h, sizeof(*h));
				MOD_DEC_USE_COUNT;
			}

			if (atomic_read(&mfw->nhosts)) {
				IP_MASQ_ERR("mfw_flush(): after flushing row nhosts=%d\n",
						atomic_read(&mfw->nhosts));
			}
			mfw_next = mfw->next;
			kfree_s(mfw, sizeof(*mfw));	
			MOD_DEC_USE_COUNT;
			ip_masq_mod_dec_nent(mmod_self);
		}
	}
}

/*
 *	User space control entry point
 */
static int mfw_ctl(int optname, struct ip_masq_ctl *mctl, int optlen)
{
        struct ip_mfw_user *mu =  &mctl->u.mfw_user;
	struct ip_masq_mfw *mfw;
	int ret = EINVAL;
	int arglen = optlen - IP_MASQ_CTL_BSIZE;
	int cmd;


	IP_MASQ_DEBUG(1-debug, "ip_masq_user_ctl(len=%d/%d|%d/%d)\n",
		arglen,
		sizeof (*mu),
		optlen,
		sizeof (*mctl));

	/*
	 *	checks ...
	 */
	if (arglen != sizeof(*mu) && optlen != sizeof(*mctl)) 
		return -EINVAL;
 
	/* 
	 *	Don't trust the lusers - plenty of error checking! 
	 */
	cmd = mctl->m_cmd;
	IP_MASQ_DEBUG(1-debug, "ip_masq_mfw_ctl(cmd=%d, fwmark=%d)\n",
			cmd, mu->fwmark);


	switch(cmd) {
		case IP_MASQ_CMD_NONE:
			return 0;
		case IP_MASQ_CMD_FLUSH:
			break;
		case IP_MASQ_CMD_ADD:
		case IP_MASQ_CMD_INSERT:
		case IP_MASQ_CMD_SET:
			if (mu->fwmark == 0) {
				IP_MASQ_DEBUG(1-debug, "invalid fwmark==0\n");
				return -EINVAL;
			}
			if (mu->pref < 0) {
				IP_MASQ_DEBUG(1-debug, "invalid pref==%d\n",
					mu->pref);
				return -EINVAL;
			}
			break;
	}


	ret = -EINVAL;

	switch(cmd) {
	case IP_MASQ_CMD_ADD:
	case IP_MASQ_CMD_INSERT:
		if (!mu->raddr) {
			IP_MASQ_DEBUG(0-debug, "ip_masq_mfw_ctl(ADD): invalid redirect 0x%x:%d\n",
					mu->raddr, mu->rport);
			goto out;
		}

		/*
		 *	Cannot just use mfw_lock because below
		 *	are allocations that can sleep; so
		 *	to assure "new entry" atomic creation
		 *	I use a semaphore.
		 *
		 */
		down(&mfw_sema);

		read_lock(&mfw_lock);
		mfw = __mfw_get(mu->fwmark);
		read_unlock(&mfw_lock);
		
		/*
		 *	If first host, create m-entry
		 */
		if (mfw == NULL) {
			mfw = mfw_new(mu->fwmark);
			if (mfw == NULL) 
				ret = -ENOMEM;
		} 

		if (mfw) {
			/*
			 *	Put m.host in m-entry.
			 */
			ret = mfw_addhost(mfw, mu, cmd == IP_MASQ_CMD_ADD);

			/*
			 *	If first host, link m-entry to hash table.
			 *	Already protected by global lock.
			 */
			if (ret == 0 && atomic_read(&mfw->nhosts) == 1)  {
				write_lock_bh(&mfw_lock);
				__mfw_add(mfw);
				write_unlock_bh(&mfw_lock);
			} 
			if (atomic_read(&mfw->nhosts) == 0) {
				mfw_destroy(mfw);
			}
		}

		up(&mfw_sema);

		break;

	case IP_MASQ_CMD_DEL:
		down(&mfw_sema);

		read_lock(&mfw_lock);
		mfw = __mfw_get(mu->fwmark);
		read_unlock(&mfw_lock);

		if (mfw) {
			ret = mfw_delhost(mfw, mu);

			/*
			 *	Last lease will free
			 *	XXX check logic XXX
			 */
			if (atomic_read(&mfw->nhosts) == 0) {
				write_lock_bh(&mfw_lock);
				__mfw_del(mfw);
				write_unlock_bh(&mfw_lock);
				mfw_destroy(mfw);
			}
		} else 
			ret = -ESRCH;

		up(&mfw_sema);
		break;
	case IP_MASQ_CMD_FLUSH:

		down(&mfw_sema);
		mfw_flush();
		up(&mfw_sema);
		ret = 0;
		break;
	case IP_MASQ_CMD_SET:
		/*
		 *	No need to semaphorize here, main list is not 
		 *	modified.
		 */
		read_lock(&mfw_lock);
		
		mfw = __mfw_get(mu->fwmark);
		if (mfw) {
			write_lock_bh(&mfw->lock);
			
			if (mu->flags & IP_MASQ_MFW_SCHED) {
				struct ip_masq_mfw_host *h;
				if ((h=__mfw_sched(mfw, 1))) {
					mfw_host_to_user(h, mu);
					ret = 0;
				} 
			} else {
				ret = __mfw_edithost(mfw, mu);
			}
				
			write_unlock_bh(&mfw->lock);
		}

		read_unlock(&mfw_lock);
		break;
	}
out:
	
	return ret;
}

/*
 *	Module stubs called from ip_masq core module
 */
 
/*
 *	Input rule stub, called very early for each incoming packet, 
 *	to see if this module has "interest" in packet.
 */
static int mfw_in_rule(const struct sk_buff *skb, const struct iphdr *iph)
{
	int val;
	read_lock(&mfw_lock);
	val = ( __mfw_get(skb->fwmark) != 0);
	read_unlock(&mfw_lock);
	return val;
}

/*
 *	Input-create stub, called to allow "custom" masq creation
 */
static struct ip_masq * mfw_in_create(const struct sk_buff *skb, const struct iphdr *iph, __u32 maddr)
{
	union ip_masq_tphdr tph;
	struct ip_masq *ms = NULL;
	struct ip_masq_mfw_host *h = NULL;

	tph.raw = (char*) iph + iph->ihl * 4;

	switch (iph->protocol) {
		case IPPROTO_TCP:
			/* 	
			 *	Only open TCP tunnel if SYN+!ACK packet
			 */
			if (!tph.th->syn || tph.th->ack)
				return NULL;
		case IPPROTO_UDP:
			break;
		default:
			return NULL;
	}

	/* 
	 *	If no entry exists in the masquerading table
 	 * 	and the port is involved
	 *  	in port forwarding, create a new masq entry 
	 */

	if ((h=mfw_lookup(skb->fwmark))) {
		ms = ip_masq_new(iph->protocol,
				iph->daddr, tph.portp[1],	
				/* if no redir-port, use packet dest port */
				h->addr, h->port? h->port : tph.portp[1],
				iph->saddr, tph.portp[0],
				0);

		if (ms != NULL)
			ip_masq_listen(ms);
	}
	return ms;
}


#define mfw_in_update	NULL
#define mfw_out_rule	NULL
#define mfw_out_create	NULL
#define mfw_out_update	NULL

static struct ip_masq_mod mfw_mod = {
	NULL,			/* next */
	NULL,			/* next_reg */
	"mfw",		/* name */
	ATOMIC_INIT(0),		/* nent */
	ATOMIC_INIT(0),		/* refcnt */
	proc_ent,
	mfw_ctl,
	NULL,			/* masq_mod_init */
	NULL,			/* masq_mod_done */
	mfw_in_rule,
	mfw_in_update,
	mfw_in_create,
	mfw_out_rule,
	mfw_out_update,
	mfw_out_create,
};


__initfunc(int ip_mfw_init(void))
{
	return register_ip_masq_mod ((mmod_self=&mfw_mod));
}

int ip_mfw_done(void)
{
	return unregister_ip_masq_mod(&mfw_mod);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
	if (ip_mfw_init() != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	if (ip_mfw_done() != 0)
		printk(KERN_INFO "can't remove module");
}

#endif /* MODULE */
