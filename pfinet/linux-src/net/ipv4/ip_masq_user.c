/*
 *	IP_MASQ_USER user space control module
 *
 *
 *	$Id: ip_masq_user.c,v 1.1.2.3 1999/11/16 06:33:51 davem Exp $
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <asm/system.h>
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/inet.h>
#include <linux/init.h>
#include <net/protocol.h>
#include <net/icmp.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/checksum.h>
#include <net/ip_masq.h>
#include <net/ip_masq_mod.h>
#include <linux/sysctl.h>
#include <linux/ip_fw.h>

#include <linux/ip_masq.h>

/*
 *	Debug level
 */
static int debug=0;

MODULE_PARM(ports, "1-" __MODULE_STRING(MAX_MASQ_APP_PORTS) "i");
MODULE_PARM(debug, "i");

/*
static int check_5uple (struct ip_masq_user *ums) {
	return 0;
}
*/
static void masq_user_k2u(const struct ip_masq *ms, struct ip_masq_user *ums)
{
	ums->protocol = ms->protocol;
	ums->daddr = ms->daddr;
	ums->dport = ms->dport;
	ums->maddr = ms->maddr;
	ums->mport = ms->mport;
	ums->saddr = ms->saddr;
	ums->sport = ms->sport;
	ums->timeout = ms->timeout;
}


static int ip_masq_user_maddr(struct ip_masq_user *ums)
{
	struct device *dev;
	struct rtable *rt;
	int ret = -EINVAL;
	u32 rt_daddr, rt_saddr;
	u32 tos;

	/*
	 *	Did specify masq address.
	 */
	if (ums->maddr)
		return 0;

	/*
	 *	Select address to use for routing query
	 */

	rt_daddr = ums->rt_daddr? ums->rt_daddr : ums->daddr;
	rt_saddr = ums->rt_saddr? ums->rt_saddr : ums->saddr;


	/*
	 *	No address for routing, cannot continue
	 */
	if (rt_daddr == 0) {
		IP_MASQ_DEBUG(1-debug, "cannot setup maddr with daddr=%lX, rt_addr=%lX\n",
			     ntohl(ums->daddr), ntohl(ums->rt_daddr));
		return -EINVAL;
	}

	/*
	 *	Find out rt device 
	 */

	rt_saddr = 0; 
	tos = RT_TOS(ums->ip_tos) | RTO_CONN;

	if ((ret=ip_route_output(&rt, rt_daddr, rt_saddr, tos, 0 /* dev */))) {
		IP_MASQ_DEBUG(0-debug, "could not setup maddr for routing daddr=%lX, saddr=%lX\n",
			     ntohl(rt_daddr), ntohl(rt_saddr));
		return ret;
	}
	dev = rt->u.dst.dev;
	ums->maddr = rt->rt_src;  /* Per Alexey */

	IP_MASQ_DEBUG(1-debug, "did setup maddr=%lX\n", ntohl(ums->maddr));
	ip_rt_put(rt);
	return 0;
}

/*
 *	Create new entry (from uspace)
 */
static int ip_masq_user_new(struct ip_masq_user *ums)
{
	struct ip_masq *ms = NULL;
	unsigned mflags = 0;
	int ret;

	if (masq_proto_num (ums->protocol) == -1) {
		return EPROTONOSUPPORT;
	}

	if (ums->dport == 0) {
		ums->flags |= IP_MASQ_USER_F_LISTEN;
	}

	if (ums->flags | IP_MASQ_USER_F_LISTEN) {
		if ((ums->saddr == 0) || (ums->sport == 0)) {
			return EINVAL;
		}
		mflags |= (IP_MASQ_F_NO_DPORT|IP_MASQ_F_NO_DADDR);

	}

	if ((ret = ip_masq_user_maddr(ums)) < 0) {
		return -ret;
	}

	mflags |= IP_MASQ_F_USER;
	ms = ip_masq_new(ums->protocol, 
			ums->maddr, ums->mport, 
			ums->saddr, ums->sport,
			ums->daddr, ums->dport,
			mflags);
	
	if (ms == NULL) {
		/*
		 *	FIXME: ip_masq_new() should return errno
		 */
		return EBUSY;
	}

	/*
	 *	Setup timeouts for this new entry
	 */

	if (ums->timeout) {
		ms->timeout = ums->timeout;
	} else if (ums->flags | IP_MASQ_USER_F_LISTEN) {
		ip_masq_listen(ms);
	}

	masq_user_k2u(ms, ums);
	ip_masq_put(ms);
	return 0;
}

/* 
 *	Delete existing entry
 */
static int ip_masq_user_del(struct ip_masq_user *ums)
{
	struct ip_masq *ms=NULL;

	if (masq_proto_num (ums->protocol) == -1) {
		return EPROTONOSUPPORT;
	}
	start_bh_atomic();
	if (ums->mport && ums->maddr) {
		ms = ip_masq_in_get(ums->protocol, 
				ums->daddr, ums->dport, 
				ums->maddr, ums->mport);
		end_bh_atomic();
	} else if (ums->sport && ums->saddr) {
		ms = ip_masq_out_get(ums->protocol,
				ums->saddr, ums->sport,
				ums->daddr, ums->dport);
		end_bh_atomic();
	} else
		return EINVAL;	
	
	if (ms == NULL) {
		return ESRCH;
	}

	/*
	 *	got (locked) entry, setup almost tiny timeout :) and  
	 *	give away
	 *
	 *	FIXME: should use something better than S_CLOSE
	 */
	ms->timeout = IP_MASQ_S_CLOSE;

	masq_user_k2u(ms, ums);
	ip_masq_put(ms);
	return 0;
}

static struct ip_masq * ip_masq_user_locked_get (struct ip_masq_user *ums, int *err)
{
	struct ip_masq *ms=NULL;
	if (masq_proto_num (ums->protocol) == -1) {
		*err = EPROTONOSUPPORT;
	}

	start_bh_atomic();
	if (ums->mport && ums->maddr) {
		ms = ip_masq_in_get(ums->protocol, 
				ums->daddr, ums->dport, 
				ums->maddr, ums->mport);
		end_bh_atomic();
	} else if (ums->sport && ums->saddr) {
		ms = ip_masq_out_get(ums->protocol,
				ums->saddr, ums->sport,
				ums->daddr, ums->dport);
		end_bh_atomic();
	} else
		*err = EINVAL;	
	
	if (ms == NULL) *err = ESRCH;
	return ms;
}

/*
 * 	Get existing entry (complete full tunnel info)
 */
static int ip_masq_user_get(struct ip_masq_user *ums)
{
	struct ip_masq *ms=NULL;
	int err;

	ms = ip_masq_user_locked_get(ums, &err);
	if (ms == NULL)
		return err;

	masq_user_k2u(ms, ums);

	ip_masq_put(ms);
	return 0;
}

/* 
 *	Set (some, valid) entry parameters
 */
static int ip_masq_user_set(struct ip_masq_user *ums)
{
	struct ip_masq *ms = NULL;
	int err;

	ms = ip_masq_user_locked_get(ums, &err);
	if (ms == NULL)
		return err;
	
	/*
	 *	FIXME: must allow selecting what you want to set
	 */
	ms->timeout = ums->timeout;

	masq_user_k2u(ms, ums);
	
	ip_masq_put(ms);
	return 0;
}


/*
 *	Entry point
 *	ret value:
 *		<0   err
 *		==0  ok
 *		>0   ok, copy to user
 */
static int ip_masq_user_ctl(int optname, struct ip_masq_ctl *mctl, int optlen)
{
	struct ip_masq_user *ums = &mctl->u.user;
	int ret = EINVAL;
	int arglen = optlen - IP_MASQ_CTL_BSIZE;
	int cmd;

	IP_MASQ_DEBUG(1-debug, "ip_masq_user_ctl(len=%d/%d|%d/%d)\n",
		arglen,
		sizeof (*ums),
		optlen,
		sizeof (*mctl));

	/*
	 *	Yes, I'm a bad guy ...
	 */
	if (arglen != sizeof(*ums) && optlen != sizeof(*mctl)) 
		return EINVAL;

	MOD_INC_USE_COUNT;

	/* 
	 *	Don't trust the lusers - plenty of error checking! 
	 */
	cmd = mctl->m_cmd;
	IP_MASQ_DEBUG(1-debug, "ip_masq_user_ctl(cmd=%d)\n", cmd);

	switch (mctl->m_cmd) {
		case IP_MASQ_CMD_ADD:
		case IP_MASQ_CMD_INSERT:
			ret = ip_masq_user_new(ums);
			break;
		case IP_MASQ_CMD_DEL:
			ret = ip_masq_user_del(ums);
			break;
		case IP_MASQ_CMD_SET:
			ret = ip_masq_user_set(ums);
			break;
		case IP_MASQ_CMD_GET:
			ret = ip_masq_user_get(ums);
			break;
	}

	/*
	 *	For all of the above, return masq tunnel info
	 */

	ret = -ret;

	if (ret == 0) {
		ret = sizeof (*ums) + IP_MASQ_CTL_BSIZE;
		IP_MASQ_DEBUG(1-debug, "will return %d bytes to user\n", ret);
	}

	MOD_DEC_USE_COUNT;
	return ret;
}


#ifdef CONFIG_PROC_FS
static int ip_masq_user_info(char *buffer, char **start, off_t offset,
			      int length, int proto)
{
	off_t pos=0, begin;
	struct ip_masq *ms;
	char temp[129];
        int idx = 0;
	int col;
	int len=0;
	int magic_control;
	struct list_head *l,*e;

	MOD_INC_USE_COUNT;

	IP_MASQ_DEBUG(1-debug, "Entered user_info with proto=%d\n", proto);

	if (offset < 128)
	{
		sprintf(temp,
			"Prot SrcIP    SPrt DstIP    DPrt MAddr    MPrt State        Flgs Ref Ctl Expires HRow HCol (free=%d,%d,%d)",
			atomic_read(ip_masq_free_ports), 
			atomic_read(ip_masq_free_ports+1), 
			atomic_read(ip_masq_free_ports+2));
		len = sprintf(buffer, "%-127s\n", temp);
	}
	pos = 128;

        for(idx = 0; idx < IP_MASQ_TAB_SIZE; idx++)
	{
	/*
	 *	Lock is actually only need in next loop 
	 *	we are called from uspace: must stop bh.
	 */
	col=0;
	read_lock_bh(&__ip_masq_lock);
	l = &ip_masq_m_table[idx];
	for (e=l->next; e!=l; e=e->next) {
		col++;
		ms = list_entry(e, struct ip_masq, m_list);
		if (ms->protocol != proto) {
			continue;
		}

		pos += 128;
		if (pos <= offset) {
			len = 0;
			continue;
		}

		/*
		 *	We have locked the tables, no need to del/add timers
		 *	nor cli()  8)
		 */
		

		magic_control = atomic_read(&ms->n_control);
		if (!magic_control && ms->control) magic_control = -1;
		sprintf(temp,"%-4s %08lX:%04X %08lX:%04X %08lX:%04X %-12s %3X %4d %3d %7lu %4d %4d",
			masq_proto_name(ms->protocol),
			ntohl(ms->saddr), ntohs(ms->sport),
			ntohl(ms->daddr), ntohs(ms->dport),
			ntohl(ms->maddr), ntohs(ms->mport),
			ip_masq_state_name(ms->state),
			ms->flags,
			atomic_read(&ms->refcnt),
			magic_control,
			(ms->timer.expires-jiffies)/HZ,
			idx, col);
		len += sprintf(buffer+len, "%-127s\n", temp);

		if(len >= length) {
			read_unlock_bh(&__ip_masq_lock);
			goto done;
		}
	}
	read_unlock_bh(&__ip_masq_lock);
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
#else
#define ip_masq_user_info	NULL
#endif

static struct ip_masq_hook ip_masq_user = {
	ip_masq_user_ctl,
	ip_masq_user_info
};

int ip_masq_user_init(void)
{
	if (ip_masq_user_hook != NULL) 
		return -EEXIST;
	ip_masq_user_hook = &ip_masq_user;
	return 0;
}

int ip_masq_user_done(void)
{
	if (ip_masq_user_hook == NULL) 
		return ENOENT;
	ip_masq_user_hook = NULL;
	return 0;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;
int init_module(void)
{
	if (ip_masq_user_init() != 0)
		return -EIO;
	return 0;
}

void cleanup_module(void)
{
	if (ip_masq_user_done() != 0)
		printk(KERN_INFO "ip_masq_user_done(): can't remove module");
}

#endif /* MODULE */
