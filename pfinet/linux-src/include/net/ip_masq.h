/*
 * 	IP masquerading functionality definitions
 */

#include <linux/config.h> /* for CONFIG_IP_MASQ_DEBUG */
#ifndef _IP_MASQ_H
#define _IP_MASQ_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/skbuff.h>
#include <linux/list.h>
#endif /* __KERNEL__ */

/*
 * This define affects the number of ports that can be handled
 * by each of the protocol helper modules.
 */
#define MAX_MASQ_APP_PORTS 12

/*
 *	Linux ports don't normally get allocated above 32K.
 *	I used an extra 4K port-space
 */

#define PORT_MASQ_BEGIN	61000
#define PORT_MASQ_END	(PORT_MASQ_BEGIN+4096)

#define MASQUERADE_EXPIRE_TCP     15*60*HZ
#define MASQUERADE_EXPIRE_TCP_FIN  2*60*HZ
#define MASQUERADE_EXPIRE_UDP      5*60*HZ
/* 
 * ICMP can no longer be modified on the fly using an ioctl - this
 * define is the only way to change the timeouts 
 */
#define MASQUERADE_EXPIRE_ICMP      125*HZ

#define IP_MASQ_MOD_CTL			0x00
#define IP_MASQ_USER_CTL		0x01

#ifdef __KERNEL__

#define IP_MASQ_TAB_SIZE	256

#define IP_MASQ_F_NO_DADDR	      0x0001 	/* no daddr yet */
#define IP_MASQ_F_NO_DPORT     	      0x0002	/* no dport set yet */
#define IP_MASQ_F_NO_SADDR	      0x0004	/* no sport set yet */
#define IP_MASQ_F_NO_SPORT	      0x0008	/* no sport set yet */

#define IP_MASQ_F_DLOOSE	      0x0010	/* loose dest binding */
#define IP_MASQ_F_NO_REPLY	      0x0080	/* no reply yet from outside */

#define IP_MASQ_F_HASHED	      0x0100 	/* hashed entry */
#define IP_MASQ_F_OUT_SEQ             0x0200	/* must do output seq adjust */
#define IP_MASQ_F_IN_SEQ              0x0400	/* must do input seq adjust */

#define IP_MASQ_F_MPORT		      0x1000 	/* own mport specified */
#define IP_MASQ_F_USER		      0x2000	/* from uspace */
#define IP_MASQ_F_SIMPLE_HASH	      0x8000	/* prevent s+d and m+d hashing */

/*
 *	Delta seq. info structure
 *	Each MASQ struct has 2 (output AND input seq. changes).
 */

struct ip_masq_seq {
        __u32		init_seq;	/* Add delta from this seq */
        short		delta;		/* Delta in sequence numbers */
        short		previous_delta;	/* Delta in sequence numbers before last resized pkt */
};

/*
 *	MASQ structure allocated for each masqueraded association
 */
struct ip_masq {
	struct list_head m_list, s_list, d_list;
			/* hashed d-linked list heads */
	atomic_t refcnt;		/* reference count */
	struct timer_list timer;	/* Expiration timer */
	__u16 		protocol;	/* Which protocol are we talking? */
	__u16		sport, dport, mport;	/* src, dst & masq ports */
	__u32 		saddr, daddr, maddr;	/* src, dst & masq addresses */
        struct ip_masq_seq out_seq, in_seq;
	struct ip_masq_app *app;	/* bound ip_masq_app object */
	void		*app_data;	/* Application private data */
	struct ip_masq	*control;	/* Master control connection */
	atomic_t        n_control;	/* Number of "controlled" masqs */
	unsigned  	flags;        	/* status flags */
	unsigned	timeout;	/* timeout */
	unsigned	state;		/* state info */
	struct ip_masq_timeout_table *timeout_table;
};

/*
 *	Timeout values
 *	ipchains holds a copy of this definition
 */

struct ip_fw_masq {
        int tcp_timeout;
        int tcp_fin_timeout;
        int udp_timeout;
};

union ip_masq_tphdr {
		unsigned char *raw;
		struct udphdr *uh;
		struct tcphdr *th;
		struct icmphdr *icmph;
		__u16 *portp;
};
/*
 *	[0]: UDP free_ports
 *	[1]: TCP free_ports
 *	[2]: ICMP free_ports
 */

extern atomic_t ip_masq_free_ports[3];

/*
 *	ip_masq initializer (registers symbols and /proc/net entries)
 */
extern int ip_masq_init(void);

/*
 *	functions called from ip layer
 */
extern int ip_fw_masquerade(struct sk_buff **, __u32 maddr);
extern int ip_fw_masq_icmp(struct sk_buff **, __u32 maddr);
extern int ip_fw_unmasq_icmp(struct sk_buff *);
extern int ip_fw_demasquerade(struct sk_buff **);

/*
 *	ip_masq obj creation/deletion functions.
 */
extern struct ip_masq *ip_masq_new(int proto, __u32 maddr, __u16 mport, __u32 saddr, __u16 sport, __u32 daddr, __u16 dport, unsigned flags);

extern void ip_masq_control_add(struct ip_masq *ms, struct ip_masq* ctl_ms);
extern void ip_masq_control_del(struct ip_masq *ms);
extern struct ip_masq * ip_masq_control_get(struct ip_masq *ms);

struct ip_masq_ctl;
 
struct ip_masq_hook {
	int (*ctl)(int, struct ip_masq_ctl *, int);
	int (*info)(char *, char **, off_t, int, int);
};

extern struct list_head ip_masq_m_table[IP_MASQ_TAB_SIZE];
extern struct list_head ip_masq_s_table[IP_MASQ_TAB_SIZE];
extern struct list_head ip_masq_d_table[IP_MASQ_TAB_SIZE];
extern const char * ip_masq_state_name(int state);
extern struct ip_masq_hook *ip_masq_user_hook;
extern u32 ip_masq_select_addr(struct device *dev, u32 dst, int scope);
/*
 * 	
 *	IP_MASQ_APP: IP application masquerading definitions 
 *
 */

struct ip_masq_app
{
        struct ip_masq_app *next;
	char *name;		/* name of application proxy */
        unsigned type;          /* type = proto<<16 | port (host byte order)*/
        int n_attach;
        int (*masq_init_1)      /* ip_masq initializer */
                (struct ip_masq_app *, struct ip_masq *);
        int (*masq_done_1)      /* ip_masq fin. */
                (struct ip_masq_app *, struct ip_masq *);
        int (*pkt_out)          /* output (masquerading) hook */
                (struct ip_masq_app *, struct ip_masq *, struct sk_buff **, __u32);
        int (*pkt_in)           /* input (demasq) hook */
                (struct ip_masq_app *, struct ip_masq *, struct sk_buff **, __u32);
};

/*
 *	ip_masq_app initializer
 */
extern int ip_masq_app_init(void);

/*
 * 	ip_masq_app object registration functions (port: host byte order)
 */
extern int register_ip_masq_app(struct ip_masq_app *mapp, unsigned short proto, __u16 port);
extern int unregister_ip_masq_app(struct ip_masq_app *mapp);

/*
 *	get ip_masq_app obj by proto,port(net_byte_order)
 */
extern struct ip_masq_app * ip_masq_app_get(unsigned short proto, __u16 port);

/*
 *	ip_masq TO ip_masq_app (un)binding functions.
 */
extern struct ip_masq_app * ip_masq_bind_app(struct ip_masq *ms);
extern int ip_masq_unbind_app(struct ip_masq *ms);

/*
 *	output and input app. masquerading hooks.
 *	
 */
extern int ip_masq_app_pkt_out(struct ip_masq *, struct sk_buff **skb_p, __u32 maddr);
extern int ip_masq_app_pkt_in(struct ip_masq *, struct sk_buff **skb_p, __u32 maddr);

/*
 *	service routine(s).
 */

extern struct ip_masq * ip_masq_out_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);
extern struct ip_masq * ip_masq_in_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port);

extern int ip_masq_listen(struct ip_masq *);

static __inline__ struct ip_masq * ip_masq_in_get_iph(const struct iphdr *iph)
{
 	const __u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);
        return ip_masq_in_get(iph->protocol, 
				iph->saddr, portp[0], 
				iph->daddr, portp[1]);
}

static __inline__ struct ip_masq * ip_masq_out_get_iph(const struct iphdr *iph)
{
 	const __u16 *portp  = (__u16 *)&(((char *)iph)[iph->ihl*4]);
        return ip_masq_out_get(iph->protocol, 
				iph->saddr, portp[0], 
				iph->daddr, portp[1]);
}

extern void ip_masq_put(struct ip_masq *ms);


extern rwlock_t __ip_masq_lock;

#ifdef __SMP__
#define read_lock_bh(lock) 	do { start_bh_atomic(); read_lock(lock); \
					} while (0)
#define read_unlock_bh(lock)	do { read_unlock(lock); end_bh_atomic(); \
					} while (0)
#define write_lock_bh(lock)	do { start_bh_atomic(); write_lock(lock); \
					} while (0)
#define write_unlock_bh(lock)	do { write_unlock(lock); end_bh_atomic(); \
					} while (0)
#else
#define read_lock_bh(lock)	start_bh_atomic()
#define read_unlock_bh(lock)	end_bh_atomic()
#define write_lock_bh(lock)	start_bh_atomic()
#define write_unlock_bh(lock)	end_bh_atomic()
#endif 
/*
 *
 */

/*
 *	Debugging stuff
 */

extern int ip_masq_get_debug_level(void);

#ifdef CONFIG_IP_MASQ_DEBUG
#define IP_MASQ_DEBUG(level, msg...) do { \
	if (level <= ip_masq_get_debug_level()) \
		printk(KERN_DEBUG "IP_MASQ:" ## msg); \
	} while (0)
#else	/* NO DEBUGGING at ALL */
#define IP_MASQ_DEBUG(level, msg...) do { } while (0)
#endif

#define IP_MASQ_INFO(msg...) \
	printk(KERN_INFO "IP_MASQ:" ## msg)

#define IP_MASQ_ERR(msg...) \
	printk(KERN_ERR "IP_MASQ:" ## msg)

#define IP_MASQ_WARNING(msg...) \
	printk(KERN_WARNING "IP_MASQ:" ## msg)


/*
 *	/proc/net entry
 */
extern int ip_masq_proc_register(struct proc_dir_entry *);
extern void ip_masq_proc_unregister(struct proc_dir_entry *);
extern int ip_masq_app_getinfo(char *buffer, char **start, off_t offset, int length, int dummy);

/*
 *	skb_replace function used by "client" modules to replace
 *	a segment of skb.
 */
extern struct sk_buff * ip_masq_skb_replace(struct sk_buff *skb, int pri, char *o_buf, int o_len, char *n_buf, int n_len);

/*
 * masq_proto_num returns 0 for UDP, 1 for TCP, 2 for ICMP
 */

static __inline__ int masq_proto_num(unsigned proto)
{
   switch (proto)
   {
      case IPPROTO_UDP:  return (0); break;
      case IPPROTO_TCP:  return (1); break;
      case IPPROTO_ICMP: return (2); break;
      default:           return (-1); break;
   }
}

static __inline__ const char *masq_proto_name(unsigned proto)
{
	static char buf[20];
	static const char *strProt[] = {"UDP","TCP","ICMP"};
	int msproto = masq_proto_num(proto);

	if (msproto<0||msproto>2)  {
		sprintf(buf, "IP_%d", proto);
		return buf;
	}
        return strProt[msproto];
}

enum {
	IP_MASQ_S_NONE = 0,
	IP_MASQ_S_ESTABLISHED,
	IP_MASQ_S_SYN_SENT,
	IP_MASQ_S_SYN_RECV,
	IP_MASQ_S_FIN_WAIT,
	IP_MASQ_S_TIME_WAIT,
	IP_MASQ_S_CLOSE,
	IP_MASQ_S_CLOSE_WAIT,
	IP_MASQ_S_LAST_ACK,
	IP_MASQ_S_LISTEN,
	IP_MASQ_S_UDP,
	IP_MASQ_S_ICMP,
	IP_MASQ_S_LAST
};

struct ip_masq_timeout_table {
	atomic_t refcnt;
	int scale;
	int timeout[IP_MASQ_S_LAST+1];
};

static __inline__ void ip_masq_timeout_attach(struct ip_masq *ms, struct ip_masq_timeout_table *mstim)
{
	atomic_inc (&mstim->refcnt);
	ms->timeout_table=mstim;
}

static __inline__ void ip_masq_timeout_detach(struct ip_masq *ms)
{
	struct ip_masq_timeout_table *mstim = ms->timeout_table;

	if (!mstim)
		return;
	atomic_dec(&mstim->refcnt);
}

#endif /* __KERNEL__ */

#endif /* _IP_MASQ_H */
