/*
 *
 * 	Masquerading functionality
 *
 * 	Copyright (c) 1994 Pauline Middelink
 *
 *	$Id: ip_masq.c,v 1.34.2.2 1999/08/07 10:56:28 davem Exp $
 *
 *
 *	See ip_fw.c for original log
 *
 * Fixes:
 *	Joseph Gooch		:	Modified ip_fw_masquerade() to do a ip_route_output()
 *	 (help by Dan Drown)	:	to choose the proper local address.
 *	 (and Alexey)		:
 *	Juan Jose Ciarlante	:	Modularized application masquerading (see ip_masq_app.c)
 *	Juan Jose Ciarlante	:	New struct ip_masq_seq that holds output/input delta seq.
 *	Juan Jose Ciarlante	:	Added hashed lookup by proto,maddr,mport and proto,saddr,sport
 *	Juan Jose Ciarlante	:	Fixed deadlock if free ports get exhausted
 *	Juan Jose Ciarlante	:	Added NO_ADDR status flag.
 *	Richard Lynch		:	Added IP Autoforward
 *	Nigel Metheringham	:	Added ICMP handling for demasquerade
 *	Nigel Metheringham	:	Checksum checking of masqueraded data
 *	Nigel Metheringham	:	Better handling of timeouts of TCP conns
 *	Delian Delchev		:	Added support for ICMP requests and replys
 *	Nigel Metheringham	:	ICMP in ICMP handling, tidy ups, bug fixes, made ICMP optional
 *	Juan Jose Ciarlante	:	re-assign maddr if no packet received from outside
 *	Juan Jose Ciarlante	:	ported to 2.1 tree
 *	Juan Jose Ciarlante	:	reworked control connections
 *	Steven Clarke		:	Added Port Forwarding
 *	Juan Jose Ciarlante	:	Just ONE ip_masq_new (!)
 *	Juan Jose Ciarlante	:	IP masq modules support
 *	Juan Jose Ciarlante	:	don't go into search loop if mport specified
 *	Juan Jose Ciarlante	:	locking
 *	Steven Clarke		:	IP_MASQ_S_xx state design
 *	Juan Jose Ciarlante	:	IP_MASQ_S state implementation 
 *	Juan Jose Ciarlante	: 	xx_get() clears timer, _put() inserts it
 *	Juan Jose Ciarlante	: 	create /proc/net/ip_masq/ 
 *	Juan Jose Ciarlante	: 	reworked checksums (save payload csum if possible)
 *	Juan Jose Ciarlante	: 	added missing ip_fw_masquerade checksum
 *	Juan Jose Ciarlante	: 	csum savings
 *	Juan Jose Ciarlante	: 	added user-space tunnel creation/del, etc
 *	Juan Jose Ciarlante	: 	(last) moved to ip_masq_user runtime module
 *	Juan Jose Ciarlante	: 	user timeout handling again
 *	Juan Jose Ciarlante	: 	make new modules support optional
 *	Juan Jose Ciarlante	: 	u-space context => locks reworked
 *	Juan Jose Ciarlante	: 	fixed stupid SMP locking bug
 *	Juan Jose Ciarlante	: 	fixed "tap"ing in demasq path by copy-on-w
 *	Juan Jose Ciarlante	: 	make masq_proto_doff() robust against fake sized/corrupted packets
 *	Kai Bankett		:	do not toss other IP protos in proto_doff()
 *	Dan Kegel		:	pointed correct NAT behavior for UDP streams
 *	Julian Anastasov	:	use daddr and dport as hash keys
 *	
 */

#include <linux/config.h>
#include <linux/module.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
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

#ifdef CONFIG_IP_MASQUERADE_MOD
#include <net/ip_masq_mod.h>
#endif

#include <linux/sysctl.h>
#include <linux/ip_fw.h>
#include <linux/ip_masq.h>

int sysctl_ip_masq_debug = 0;

/*
 *	Exported wrapper 
 */
int ip_masq_get_debug_level(void)
{
	return sysctl_ip_masq_debug;
}

struct ip_masq_hook *ip_masq_user_hook = NULL;

/*
 *	Timeout table[state]
 */
/* static int masq_timeout_table[IP_MASQ_S_LAST+1] = { */
static struct ip_masq_timeout_table masq_timeout_table = {
	ATOMIC_INIT(0),	/* refcnt */
	0,		/* scale  */
	{
		30*60*HZ,	/*	IP_MASQ_S_NONE,	*/
		15*60*HZ,	/*	IP_MASQ_S_ESTABLISHED,	*/
		2*60*HZ,	/*	IP_MASQ_S_SYN_SENT,	*/
		1*60*HZ,	/*	IP_MASQ_S_SYN_RECV,	*/
		2*60*HZ,	/*	IP_MASQ_S_FIN_WAIT,	*/
		2*60*HZ,	/*	IP_MASQ_S_TIME_WAIT,	*/
		10*HZ,		/*	IP_MASQ_S_CLOSE,	*/
		60*HZ,		/*	IP_MASQ_S_CLOSE_WAIT,	*/
		30*HZ,		/*	IP_MASQ_S_LAST_ACK,	*/
		2*60*HZ,	/*	IP_MASQ_S_LISTEN,	*/
		5*60*HZ,	/*	IP_MASQ_S_UDP,	*/
		1*60*HZ,	/*	IP_MASQ_S_ICMP,	*/
		2*HZ,/*	IP_MASQ_S_LAST	*/
	},	/* timeout */
};

#define MASQUERADE_EXPIRE_RETRY      masq_timeout_table.timeout[IP_MASQ_S_TIME_WAIT]

static const char * state_name_table[IP_MASQ_S_LAST+1] = {
	"NONE",		/*	IP_MASQ_S_NONE,	*/
	"ESTABLISHED",	/*	IP_MASQ_S_ESTABLISHED,	*/
	"SYN_SENT",	/*	IP_MASQ_S_SYN_SENT,	*/
	"SYN_RECV",	/*	IP_MASQ_S_SYN_RECV,	*/
	"FIN_WAIT",	/*	IP_MASQ_S_FIN_WAIT,	*/
	"TIME_WAIT",	/*	IP_MASQ_S_TIME_WAIT,	*/
	"CLOSE",	/*	IP_MASQ_S_CLOSE,	*/
	"CLOSE_WAIT",	/*	IP_MASQ_S_CLOSE_WAIT,	*/
	"LAST_ACK",	/*	IP_MASQ_S_LAST_ACK,	*/
	"LISTEN",	/*	IP_MASQ_S_LISTEN,	*/
	"UDP",		/*	IP_MASQ_S_UDP,	*/
	"ICMP",		/*	IP_MASQ_S_ICMP,	*/
	"BUG!",		/*	IP_MASQ_S_LAST	*/
};

#define mNO IP_MASQ_S_NONE
#define mES IP_MASQ_S_ESTABLISHED
#define mSS IP_MASQ_S_SYN_SENT
#define mSR IP_MASQ_S_SYN_RECV
#define mFW IP_MASQ_S_FIN_WAIT
#define mTW IP_MASQ_S_TIME_WAIT
#define mCL IP_MASQ_S_CLOSE
#define mCW IP_MASQ_S_CLOSE_WAIT
#define mLA IP_MASQ_S_LAST_ACK
#define mLI IP_MASQ_S_LISTEN

struct masq_tcp_states_t {
	int next_state[IP_MASQ_S_LAST];	/* should be _LAST_TCP */
};

const char * ip_masq_state_name(int state)
{
	if (state >= IP_MASQ_S_LAST)
		return "ERR!";
	return state_name_table[state];
}

struct masq_tcp_states_t masq_tcp_states [] = {
/*	INPUT */
/* 	  mNO, mES, mSS, mSR, mFW, mTW, mCL, mCW, mLA, mLI 	*/
/*syn*/	{{mSR, mES, mES, mSR, mSR, mSR, mSR, mSR, mSR, mSR }},
/*fin*/	{{mCL, mCW, mSS, mTW, mTW, mTW, mCL, mCW, mLA, mLI }},
/*ack*/	{{mCL, mES, mSS, mSR, mFW, mTW, mCL, mCW, mCL, mLI }},
/*rst*/ {{mCL, mCL, mCL, mSR, mCL, mCL, mCL, mCL, mLA, mLI }},

/*	OUTPUT */
/* 	  mNO, mES, mSS, mSR, mFW, mTW, mCL, mCW, mLA, mLI 	*/
/*syn*/	{{mSS, mES, mSS, mES, mSS, mSS, mSS, mSS, mSS, mLI }},
/*fin*/	{{mTW, mFW, mSS, mTW, mFW, mTW, mCL, mTW, mLA, mLI }},
/*ack*/	{{mES, mES, mSS, mSR, mFW, mTW, mCL, mCW, mLA, mES }},
/*rst*/ {{mCL, mCL, mSS, mCL, mCL, mTW, mCL, mCL, mCL, mCL }},
};

static __inline__ int masq_tcp_state_idx(struct tcphdr *th, int output) 
{
	/*
	 *	[0-3]: input states, [4-7]: output.
	 */
	if (output) 
		output=4;

	if (th->rst)
		return output+3;
	if (th->syn)
		return output+0;
	if (th->fin)
		return output+1;
	if (th->ack)
		return output+2;
	return -1;
}



static int masq_set_state_timeout(struct ip_masq *ms, int state)
{
	struct ip_masq_timeout_table *mstim = ms->timeout_table;
	int scale;

	/*
	 *	Use default timeout table if no specific for this entry
	 */
	if (!mstim) 
		mstim = &masq_timeout_table;

	ms->timeout = mstim->timeout[ms->state=state];
	scale = mstim->scale;

	if (scale<0)
		ms->timeout >>= -scale;
	else if (scale > 0)
		ms->timeout <<= scale;

	return state;
}

static int masq_tcp_state(struct ip_masq *ms, int output, struct tcphdr *th)
{
	int state_idx;
	int new_state = IP_MASQ_S_CLOSE;

	if ((state_idx = masq_tcp_state_idx(th, output)) < 0) {
		IP_MASQ_DEBUG(1, "masq_state_idx(%d)=%d!!!\n", 
			output, state_idx);
		goto tcp_state_out;
	}

	new_state = masq_tcp_states[state_idx].next_state[ms->state];
	
tcp_state_out:
	if (new_state!=ms->state)
		IP_MASQ_DEBUG(1, "%s %s [%c%c%c%c] %08lX:%04X-%08lX:%04X state: %s->%s\n",
				masq_proto_name(ms->protocol),
				output? "output" : "input ",
				th->syn? 'S' : '.',
				th->fin? 'F' : '.',
				th->ack? 'A' : '.',
				th->rst? 'R' : '.',
				ntohl(ms->saddr), ntohs(ms->sport),
				ntohl(ms->daddr), ntohs(ms->dport),
				ip_masq_state_name(ms->state),
				ip_masq_state_name(new_state));
	return masq_set_state_timeout(ms, new_state);
}


/*
 *	Handle state transitions
 */
static int masq_set_state(struct ip_masq *ms, int output, struct iphdr *iph, void *tp)
{
	switch (iph->protocol) {
		case IPPROTO_ICMP:
			return masq_set_state_timeout(ms, IP_MASQ_S_ICMP);
		case IPPROTO_UDP:
			return masq_set_state_timeout(ms, IP_MASQ_S_UDP);
		case IPPROTO_TCP:
			return masq_tcp_state(ms, output, tp);
	}
	return -1;
}

/*
 *	Set LISTEN timeout. (ip_masq_put will setup timer)
 */
int ip_masq_listen(struct ip_masq *ms)
{
	masq_set_state_timeout(ms, IP_MASQ_S_LISTEN);
	return ms->timeout;
}

/* 
 *	Dynamic address rewriting 
 */
extern int sysctl_ip_dynaddr;

/*
 *	Lookup lock
 */
rwlock_t __ip_masq_lock = RW_LOCK_UNLOCKED;

/*
 *	Implement IP packet masquerading
 */

/*
 * Converts an ICMP reply code into the equivalent request code
 */
static __inline__ const __u8 icmp_type_request(__u8 type)
{
   switch (type)
   {
      case ICMP_ECHOREPLY: return ICMP_ECHO; break;
      case ICMP_TIMESTAMPREPLY: return ICMP_TIMESTAMP; break;
      case ICMP_INFO_REPLY: return ICMP_INFO_REQUEST; break;
      case ICMP_ADDRESSREPLY: return ICMP_ADDRESS; break;
      default: return (255); break;
   }
}

/*
 * Helper macros - attempt to make code clearer! 
 */

/* ID used in ICMP lookups */
#define icmp_id(icmph)		((icmph->un).echo.id)
/* (port) hash value using in ICMP lookups for requests */
#define icmp_hv_req(icmph)	((__u16)(icmph->code+(__u16)(icmph->type<<8)))
/* (port) hash value using in ICMP lookups for replies */
#define icmp_hv_rep(icmph)	((__u16)(icmph->code+(__u16)(icmp_type_request(icmph->type)<<8)))

/*
 *	Last masq_port number in use.
 *	Will cycle in MASQ_PORT boundaries.
 */
static __u16 masq_port = PORT_MASQ_BEGIN;
static spinlock_t masq_port_lock = SPIN_LOCK_UNLOCKED;

/*
 *	free ports counters (UDP & TCP)
 *
 *	Their value is _less_ or _equal_ to actual free ports:
 *	same masq port, diff masq addr (firewall iface address) allocated
 *	entries are accounted but their actually don't eat a more than 1 port.
 *
 *	Greater values could lower MASQ_EXPIRATION setting as a way to
 *	manage 'masq_entries resource'.
 *
 *	By default we will reuse masq.port iff (output) connection
 *	(5-upla) if not duplicated. 
 *	This may break midentd and others ...
 */

#ifdef CONFIG_IP_MASQ_NREUSE
#define PORT_MASQ_MUL 1
#else
#define PORT_MASQ_MUL 10
#endif

/*
 *	At the moment, hardcore in sync with masq_proto_num
 */
atomic_t ip_masq_free_ports[3] = {
        ATOMIC_INIT((PORT_MASQ_END-PORT_MASQ_BEGIN) * PORT_MASQ_MUL),/* UDP */
        ATOMIC_INIT((PORT_MASQ_END-PORT_MASQ_BEGIN) * PORT_MASQ_MUL),/* TCP */
        ATOMIC_INIT((PORT_MASQ_END-PORT_MASQ_BEGIN) * PORT_MASQ_MUL),/* ICMP */
};

/*
 *	Counts entries that have been requested with specific mport.
 *	Used for incoming packets to "relax" input rule (port in MASQ range).
 */
atomic_t mport_count = ATOMIC_INIT(0);

EXPORT_SYMBOL(ip_masq_get_debug_level);
EXPORT_SYMBOL(ip_masq_new);
EXPORT_SYMBOL(ip_masq_listen);
EXPORT_SYMBOL(ip_masq_free_ports);
EXPORT_SYMBOL(ip_masq_out_get);
EXPORT_SYMBOL(ip_masq_in_get);
EXPORT_SYMBOL(ip_masq_put);
EXPORT_SYMBOL(ip_masq_control_add);
EXPORT_SYMBOL(ip_masq_control_del);
EXPORT_SYMBOL(ip_masq_control_get);
EXPORT_SYMBOL(ip_masq_user_hook);
EXPORT_SYMBOL(ip_masq_state_name);
EXPORT_SYMBOL(ip_masq_select_addr);
EXPORT_SYMBOL(__ip_masq_lock);
EXPORT_SYMBOL(ip_masq_m_table);
EXPORT_SYMBOL(ip_masq_s_table);
EXPORT_SYMBOL(ip_masq_d_table);

/*
 *	3 ip_masq hash double linked tables: 
 *	  2 for input  m{addr,port}  and output s{addr,port} pkts lookups.
 *	  1 for extra modules support (daddr)
 */
  
#define IP_MASQ_NTABLES 3

struct list_head ip_masq_m_table[IP_MASQ_TAB_SIZE];
struct list_head ip_masq_s_table[IP_MASQ_TAB_SIZE];
struct list_head ip_masq_d_table[IP_MASQ_TAB_SIZE];

/*
 * timeouts
 */

#if 000 /* FIXED timeout handling */
static struct ip_fw_masq ip_masq_dummy = {
	MASQUERADE_EXPIRE_TCP,
	MASQUERADE_EXPIRE_TCP_FIN,
	MASQUERADE_EXPIRE_UDP
};

EXPORT_SYMBOL(ip_masq_expire);
struct ip_fw_masq *ip_masq_expire = &ip_masq_dummy;
#endif

/*
 *	These flags enable non-strict d{addr,port} checks
 *	Given that both (in/out) lookup tables are hashed
 *	by m{addr,port} and s{addr,port} this is quite easy 
 */

#define MASQ_DADDR_PASS	(IP_MASQ_F_NO_DADDR|IP_MASQ_F_DLOOSE)
#define MASQ_DPORT_PASS	(IP_MASQ_F_NO_DPORT|IP_MASQ_F_DLOOSE)

/*
 *	By default enable dest loose semantics
 */
#define CONFIG_IP_MASQ_LOOSE_DEFAULT 1


/*
 * 	Set masq expiration (deletion) and adds timer,
 *	if timeout==0 cancel expiration.
 *	Warning: it does not check/delete previous timer!
 */

static void __ip_masq_set_expire(struct ip_masq *ms, unsigned long tout)
{
        if (tout) {
                ms->timer.expires = jiffies+tout;
                add_timer(&ms->timer);
        } else {
                del_timer(&ms->timer);
        }
}


/*
 *	Returns hash value
 */

static __inline__ unsigned 
ip_masq_hash_key(unsigned proto, __u32 addr, __u16 port)
{
        return (proto^ntohl(addr)^ntohs(port)) & (IP_MASQ_TAB_SIZE-1);
}

/*
 *	Hashes ip_masq by its proto,addrs,ports.
 *	should be called with locked tables.
 *	returns bool success.
 */

static int ip_masq_hash(struct ip_masq *ms)
{
        unsigned hash;

        if (ms->flags & IP_MASQ_F_HASHED) {
                IP_MASQ_ERR( "ip_masq_hash(): request for already hashed, called from %p\n",
			__builtin_return_address(0));
                return 0;
        }
	atomic_add(IP_MASQ_NTABLES, &ms->refcnt);

	if ((ms->flags & (MASQ_DADDR_PASS | MASQ_DPORT_PASS |
		IP_MASQ_F_SIMPLE_HASH)) == 0)
		/*
		 *	Hash by proto,m{addr,port},d{addr,port}
		 */
		hash = ip_masq_hash_key(ms->protocol,
			ms->maddr^ms->daddr, ms->mport^ms->dport);
	else
		/*
		 *	Hash by proto,m{addr,port}
		 */
		hash = ip_masq_hash_key(ms->protocol, ms->maddr, ms->mport);

	list_add(&ms->m_list, &ip_masq_m_table[hash]);

	if ((ms->flags & (MASQ_DADDR_PASS | MASQ_DPORT_PASS |
		IP_MASQ_F_NO_SADDR | IP_MASQ_F_NO_SPORT |
		IP_MASQ_F_SIMPLE_HASH)) == 0)
		/*
		 *	Hash by proto,s{addr,port},d{addr,port}
		 */
		hash = ip_masq_hash_key(ms->protocol,
			ms->saddr^ms->daddr, ms->sport^ms->dport);
	else
		/*
		 *	Hash by proto,s{addr,port}
		 */
		hash = ip_masq_hash_key(ms->protocol, ms->saddr, ms->sport);

	list_add(&ms->s_list, &ip_masq_s_table[hash]);

        /*
         *	Hash by proto,d{addr,port}
         */
        hash = ip_masq_hash_key(ms->protocol, ms->daddr, ms->dport);
	list_add(&ms->d_list, &ip_masq_d_table[hash]);


        ms->flags |= IP_MASQ_F_HASHED;
        return 1;
}

/*
 *	UNhashes ip_masq from ip_masq_[ms]_tables.
 *	should be called with locked tables.
 *	returns bool success.
 */

static int ip_masq_unhash(struct ip_masq *ms)
{
        if (!(ms->flags & IP_MASQ_F_HASHED)) {
                IP_MASQ_ERR( "ip_masq_unhash(): request for unhash flagged, called from %p\n",
			__builtin_return_address(0));
                return 0;
        }
	list_del(&ms->m_list);
	list_del(&ms->s_list);
	list_del(&ms->d_list);

	atomic_sub(IP_MASQ_NTABLES, &ms->refcnt);

        ms->flags &= ~IP_MASQ_F_HASHED;
        return 1;
}

/*
 *	Returns ip_masq associated with supplied parameters, either
 *	broken out of the ip/tcp headers or directly supplied for those
 *	pathological protocols with address/port in the data stream
 *	(ftp, irc).  addresses and ports are in network order.
 *	called for pkts coming from OUTside-to-INside the firewall.
 *
 *	s_addr, s_port: pkt source address (foreign host)
 *	d_addr, d_port: pkt dest address (firewall)
 *
 * 	NB. Cannot check destination address, just for the incoming port.
 * 	reason: archie.doc.ac.uk has 6 interfaces, you send to
 * 	phoenix and get a reply from any other interface(==dst)!
 *
 * 	[Only for UDP] - AC
 *	
 *	Caller must lock tables
 */

static struct ip_masq * __ip_masq_in_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
        unsigned hash;
        struct ip_masq *ms = NULL;
	struct list_head *l,*e;

	hash = ip_masq_hash_key(protocol, d_addr^s_addr, d_port^s_port);

	l = &ip_masq_m_table[hash];
	for (e=l->next; e!=l; e=e->next) {
		ms = list_entry(e, struct ip_masq, m_list);
		if (s_port==ms->dport && s_addr==ms->daddr &&
		    d_port==ms->mport && protocol==ms->protocol &&
		    d_addr==ms->maddr &&
		    ((ms->flags & (MASQ_DADDR_PASS | MASQ_DPORT_PASS)) == 0)
		    ) {
			IP_MASQ_DEBUG(2, "look/in %d %08X:%04hX->%08X:%04hX OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port);
			atomic_inc(&ms->refcnt);
                        goto out;
		}
        }

        hash = ip_masq_hash_key(protocol, d_addr, d_port);

	l = &ip_masq_m_table[hash];
	for (e=l->next; e!=l; e=e->next) {
		ms = list_entry(e, struct ip_masq, m_list);
		if (protocol==ms->protocol && 
		    (d_addr==ms->maddr && d_port==ms->mport) &&
		    (s_addr==ms->daddr || ms->flags & MASQ_DADDR_PASS) &&
		    (s_port==ms->dport || ms->flags & MASQ_DPORT_PASS)
		    ) {
			IP_MASQ_DEBUG(2, "look/in %d %08X:%04hX->%08X:%04hX OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port);
			atomic_inc(&ms->refcnt);
                        goto out;
		}
        }
	IP_MASQ_DEBUG(2, "look/in %d %08X:%04hX->%08X:%04hX fail\n",
	       protocol,
	       s_addr,
	       s_port,
	       d_addr,
	       d_port);

	ms = NULL;
out:
        return ms;
}

/*
 *	Returns ip_masq associated with supplied parameters, either
 *	broken out of the ip/tcp headers or directly supplied for those
 *	pathological protocols with address/port in the data stream
 *	(ftp, irc).  addresses and ports are in network order.
 *	called for pkts coming from inside-to-OUTside the firewall.
 *
 *	Normally we know the source address and port but for some protocols
 *	(e.g. ftp PASV) we do not know the source port initially.  Alas the
 *	hash is keyed on source port so if the first lookup fails then try again
 *	with a zero port, this time only looking at entries marked "no source
 *	port".
 *	
 *	Caller must lock tables
 */

static struct ip_masq * __ip_masq_out_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
        unsigned hash;
        struct ip_masq *ms = NULL;
	struct list_head *l,*e;

	/*	
	 *	Check for "full" addressed entries
	 */
	hash = ip_masq_hash_key(protocol, s_addr^d_addr, s_port^d_port);

	l = &ip_masq_s_table[hash];
	for (e=l->next; e!=l; e=e->next) {
		ms = list_entry(e, struct ip_masq, s_list);
		if (d_addr==ms->daddr && d_port==ms->dport &&
		   s_addr==ms->saddr && s_port==ms->sport &&
		   protocol==ms->protocol &&
		   ((ms->flags & (MASQ_DADDR_PASS | MASQ_DPORT_PASS |
		   IP_MASQ_F_NO_SADDR | IP_MASQ_F_NO_SPORT)) == 0)
                   ) {
			IP_MASQ_DEBUG(2, "lk/out0 %d %08X:%04hX->%08X:%04hX OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port);

			atomic_inc(&ms->refcnt);
			goto out;
		}

        }

        hash = ip_masq_hash_key(protocol, s_addr, s_port);
	
	l = &ip_masq_s_table[hash];
	for (e=l->next; e!=l; e=e->next) {
		ms = list_entry(e, struct ip_masq, s_list);
		if (protocol == ms->protocol &&
		    s_addr == ms->saddr && s_port == ms->sport &&
		    (d_addr==ms->daddr || ms->flags & MASQ_DADDR_PASS) &&
		    (d_port==ms->dport || ms->flags & MASQ_DPORT_PASS)
                   ) {
			IP_MASQ_DEBUG(2, "lk/out1 %d %08X:%04hX->%08X:%04hX OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port);

			atomic_inc(&ms->refcnt);
			goto out;
		}

        }

	/*	
	 *	Check for NO_SPORT entries
	 */
        hash = ip_masq_hash_key(protocol, s_addr, 0);
	l = &ip_masq_s_table[hash];
	for (e=l->next; e!=l; e=e->next) {
		ms = list_entry(e, struct ip_masq, s_list);
		if (ms->flags & IP_MASQ_F_NO_SPORT &&
		    protocol == ms->protocol &&
		    s_addr == ms->saddr && 
		    (d_addr==ms->daddr || ms->flags & MASQ_DADDR_PASS) &&
		    (d_port==ms->dport || ms->flags & MASQ_DPORT_PASS)
                    ) {
			IP_MASQ_DEBUG(2, "lk/out2 %d %08X:%04hX->%08X:%04hX OK\n",
			       protocol,
			       s_addr,
			       s_port,
			       d_addr,
			       d_port);

			atomic_inc(&ms->refcnt);
                        goto out;
		}
        }
	IP_MASQ_DEBUG(2, "lk/out1 %d %08X:%04hX->%08X:%04hX fail\n",
	       protocol,
	       s_addr,
	       s_port,
	       d_addr,
	       d_port);

	ms = NULL;
out:
        return ms;
}

#ifdef CONFIG_IP_MASQ_NREUSE
/*
 *	Returns ip_masq for given proto,m_addr,m_port.
 *      called by allocation routine to find an unused m_port.
 *	
 *	Caller must lock tables
 */

static struct ip_masq * __ip_masq_getbym(int protocol, __u32 m_addr, __u16 m_port)
{
        unsigned hash;
        struct ip_masq *ms = NULL;

        hash = ip_masq_hash_key(protocol, m_addr, m_port);

        for(ms = ip_masq_m_tab[hash]; ms ; ms = ms->m_link) {
 		if ( protocol==ms->protocol &&
                    (m_addr==ms->maddr && m_port==ms->mport)) {
			atomic_inc(&ms->refcnt);
			goto out;
		}
        }

out:
        return ms;
}
#endif

struct ip_masq * ip_masq_out_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port) 
{
	struct ip_masq *ms;

	read_lock(&__ip_masq_lock);
	ms = __ip_masq_out_get(protocol, s_addr, s_port, d_addr, d_port);
	read_unlock(&__ip_masq_lock);

	if (ms)
		__ip_masq_set_expire(ms, 0);
	return ms;
}

struct ip_masq * ip_masq_in_get(int protocol, __u32 s_addr, __u16 s_port, __u32 d_addr, __u16 d_port)
{
	struct ip_masq *ms;

	read_lock(&__ip_masq_lock);
	ms =  __ip_masq_in_get(protocol, s_addr, s_port, d_addr, d_port);
	read_unlock(&__ip_masq_lock);

	if (ms)
		__ip_masq_set_expire(ms, 0);
	return ms;
}

static __inline__ void __ip_masq_put(struct ip_masq *ms) 
{
	atomic_dec(&ms->refcnt);
}

void ip_masq_put(struct ip_masq *ms)
{
	/*
	 *	Decrement refcnt
	 */
	__ip_masq_put(ms);

	/*
	 *	if refcnt==IP_MASQ_NTABLES
	 */
	if (atomic_read(&ms->refcnt)==IP_MASQ_NTABLES) {
		__ip_masq_set_expire(ms, ms->timeout);
	} else {
		IP_MASQ_DEBUG(0, "did not set timer with refcnt=%d, called from %p\n",
			atomic_read(&ms->refcnt),
			__builtin_return_address(0));
	}
}

extern int sysctl_ip_always_defrag;

static void masq_expire(unsigned long data)
{
	struct ip_masq *ms = (struct ip_masq *)data;
	ms->timeout = MASQUERADE_EXPIRE_RETRY;

	/*
	 *	hey, I'm using it
	 */
	atomic_inc(&ms->refcnt);

	IP_MASQ_DEBUG(1, "Masqueraded %s %08lX:%04X expired\n",
			masq_proto_name(ms->protocol),
			ntohl(ms->saddr),ntohs(ms->sport));

	write_lock(&__ip_masq_lock);

#if 0000
	/*
	 *	Already locked, do bounce ...
	 */
	if (ip_masq_nlocks(&__ip_masq_lock) != 1) {
		goto masq_expire_later;
	}

#endif
	/*
	 * 	do I control anybody?
	 */
	if (atomic_read(&ms->n_control)) 
		goto masq_expire_later;

	/* 	
	 *	does anybody controls me?
	 */

	if (ms->control) 
		ip_masq_control_del(ms);

        if (ip_masq_unhash(ms)) {
		if (ms->flags&IP_MASQ_F_MPORT) {
			atomic_dec(&mport_count);
		} else {
			atomic_inc(ip_masq_free_ports + masq_proto_num(ms->protocol));
		}
		ip_masq_unbind_app(ms);
        }

	/*
	 *	refcnt==1 implies I'm the only one referrer
	 */
	if (atomic_read(&ms->refcnt) == 1) {
		kfree_s(ms,sizeof(*ms));
		sysctl_ip_always_defrag--;
		MOD_DEC_USE_COUNT;
		goto masq_expire_out;
	}

masq_expire_later:
	IP_MASQ_DEBUG(0, "masq_expire delayed: %s %08lX:%04X->%08lX:%04X masq.refcnt-1=%d masq.n_control=%d\n",
		masq_proto_name(ms->protocol),
		ntohl(ms->saddr), ntohs(ms->sport),
		ntohl(ms->daddr), ntohs(ms->dport),
		atomic_read(&ms->refcnt)-1,
		atomic_read(&ms->n_control));

	ip_masq_put(ms);

masq_expire_out:
	write_unlock(&__ip_masq_lock);
}

static __u16 get_next_mport(void)
{
	__u16 mport;
	
	spin_lock_irq(&masq_port_lock);
	/*
	 *	Try the next available port number
	 */
	mport = htons(masq_port++);
	if (masq_port==PORT_MASQ_END) masq_port = PORT_MASQ_BEGIN;

	spin_unlock_irq(&masq_port_lock);
	return mport;
}

/*
 * 	Create a new masquerade list entry, also allocate an
 * 	unused mport, keeping the portnumber between the
 * 	given boundaries MASQ_BEGIN and MASQ_END.
 *
 * 	Be careful, it can be called from u-space
 */

struct ip_masq * ip_masq_new(int proto, __u32 maddr, __u16 mport, __u32 saddr, __u16 sport, __u32 daddr, __u16 dport, unsigned mflags)
{
        struct ip_masq *ms, *mst;
        int ports_tried;
	atomic_t *free_ports_p = NULL;
        static int n_fails = 0;
	int prio;


	if (masq_proto_num(proto)!=-1 && mport == 0) {
		free_ports_p = ip_masq_free_ports + masq_proto_num(proto);

		if (atomic_read(free_ports_p) == 0) {
			if (++n_fails < 5)
				IP_MASQ_ERR( "ip_masq_new(proto=%s): no free ports.\n",
				       masq_proto_name(proto));
			return NULL;
		}
	}

	prio = (mflags&IP_MASQ_F_USER) ? GFP_KERNEL : GFP_ATOMIC;

        ms = (struct ip_masq *) kmalloc(sizeof(struct ip_masq), prio);
        if (ms == NULL) {
                if (++n_fails < 5)
                        IP_MASQ_ERR("ip_masq_new(proto=%s): no memory available.\n",
                               masq_proto_name(proto));
                return NULL;
        }
	MOD_INC_USE_COUNT;
	sysctl_ip_always_defrag++;
        memset(ms, 0, sizeof(*ms));
	INIT_LIST_HEAD(&ms->s_list);
	INIT_LIST_HEAD(&ms->m_list);
	INIT_LIST_HEAD(&ms->d_list);
	init_timer(&ms->timer);
	ms->timer.data     = (unsigned long)ms;
	ms->timer.function = masq_expire;
        ms->protocol	   = proto;
        ms->saddr    	   = saddr;
        ms->sport	   = sport;
        ms->daddr	   = daddr;
        ms->dport	   = dport;
        ms->flags	   = mflags;
        ms->app_data	   = NULL;
        ms->control	   = NULL;
	
	atomic_set(&ms->n_control,0);
	atomic_set(&ms->refcnt,0);

        if (proto == IPPROTO_UDP && !mport)
#ifdef CONFIG_IP_MASQ_LOOSE_DEFAULT
		/*
		 *	Flag this tunnel as "dest loose"
		 *	
		 */
		ms->flags |= IP_MASQ_F_DLOOSE;
#else
                ms->flags |= IP_MASQ_F_NO_DADDR;
#endif

        
        /* get masq address from rif */
        ms->maddr	   = maddr;

        /*
         *	This flag will allow masq. addr (ms->maddr)
         *	to follow forwarding interface address.
         */
        ms->flags         |= IP_MASQ_F_NO_REPLY;
  
  	/*
	 * 	We want a specific mport. Be careful.
	 */
	if (masq_proto_num(proto) == -1 || mport) {
		ms->mport = mport;

		/* 
		 *	Check 5-upla uniqueness
		 */
		if (mflags & IP_MASQ_F_USER) 	
			write_lock_bh(&__ip_masq_lock);
		else 
			write_lock(&__ip_masq_lock);

                mst = __ip_masq_in_get(proto, daddr, dport, maddr, mport);
		if (mst==NULL) {
			ms->flags |= IP_MASQ_F_MPORT;

			atomic_inc(&mport_count);
                        ip_masq_hash(ms);

			if (mflags & IP_MASQ_F_USER) 	
				write_unlock_bh(&__ip_masq_lock);
			else 
				write_unlock(&__ip_masq_lock);

			ip_masq_bind_app(ms);
			atomic_inc(&ms->refcnt);
			masq_set_state_timeout(ms, IP_MASQ_S_NONE);
			return ms;
		}
		if (mflags & IP_MASQ_F_USER) 	
			write_unlock_bh(&__ip_masq_lock);
		else 
			write_unlock(&__ip_masq_lock);

		__ip_masq_put(mst);

		IP_MASQ_ERR( "Already used connection: %s, %d.%d.%d.%d:%d => %d.%d.%d.%d:%d, called from %p\n",
			masq_proto_name(proto),
			NIPQUAD(maddr), ntohs(mport),
			NIPQUAD(daddr), ntohs(dport),
			__builtin_return_address(0));


		goto mport_nono;
	}
	

        for (ports_tried = 0; 
	     (atomic_read(free_ports_p) && (ports_tried <= (PORT_MASQ_END - PORT_MASQ_BEGIN)));
	     ports_tried++){

		mport = ms->mport = get_next_mport();
		/*
		 *	lookup to find out if this connection is used.
		 */

		if (mflags & IP_MASQ_F_USER) 
			write_lock_bh(&__ip_masq_lock);
		else
			write_lock(&__ip_masq_lock);

#ifdef CONFIG_IP_MASQ_NREUSE
		mst = __ip_masq_getbym(proto, maddr, mport);
#else
		mst = __ip_masq_in_get(proto, daddr, dport, maddr, mport);
#endif
		if (mst == NULL) {

			if (atomic_read(free_ports_p) == 0) {
				if (mflags & IP_MASQ_F_USER) 
					write_unlock_bh(&__ip_masq_lock);
				else
					write_unlock(&__ip_masq_lock);

				break;
			}
			atomic_dec(free_ports_p);
			ip_masq_hash(ms);

			if (mflags & IP_MASQ_F_USER) 
				write_unlock_bh(&__ip_masq_lock);
			else
				write_unlock(&__ip_masq_lock);

			ip_masq_bind_app(ms);
			n_fails = 0;
			atomic_inc(&ms->refcnt);
			masq_set_state_timeout(ms, IP_MASQ_S_NONE);
			return ms;
		}
		if (mflags & IP_MASQ_F_USER) 
			write_unlock_bh(&__ip_masq_lock);
		else
			write_unlock(&__ip_masq_lock);

		__ip_masq_put(mst);
        }

        if (++n_fails < 5)
                IP_MASQ_ERR( "ip_masq_new(proto=%s): could not get free masq entry (free=%d).\n",
                       masq_proto_name(ms->protocol), 
		       atomic_read(free_ports_p));
mport_nono:
        kfree_s(ms, sizeof(*ms));

	sysctl_ip_always_defrag--;
	MOD_DEC_USE_COUNT;
        return NULL;
}

/*
 *	Get transport protocol data offset, check against size
 *	return:
 *		0  if other IP proto
 *		-1 if error
 */
static __inline__ int proto_doff(unsigned proto, char *th, unsigned size)
{
	int ret = -1;
	switch (proto) {
		case IPPROTO_ICMP:
			if (size >= sizeof(struct icmphdr))
				ret = sizeof(struct icmphdr);
			break;
		case IPPROTO_UDP:
			if (size >= sizeof(struct udphdr))
				ret = sizeof(struct udphdr);
			break;
		case IPPROTO_TCP:
			/*
			*	Is this case, this check _also_ avoids
			*	touching an invalid pointer if 
			*	size is invalid
			*/
			if (size >= sizeof(struct tcphdr)) {
				ret = ((struct tcphdr*)th)->doff << 2;
				if (ret > size) {
					ret = -1 ;
				}
			}

			break;
		default:
			/* 	Other proto: nothing to say, by now :) */
			ret = 0;
	}
	if (ret < 0)
		IP_MASQ_DEBUG(0, "mess proto_doff for proto=%d, size =%d\n",
			proto, size);
	return ret;
}

int ip_fw_masquerade(struct sk_buff **skb_p, __u32 maddr)
{
	struct sk_buff  *skb = *skb_p;
	struct iphdr	*iph = skb->nh.iph;
	union ip_masq_tphdr h;
	struct ip_masq	*ms;
	int		size;

	/* 
	 * 	doff holds transport protocol data offset
	 *	csum holds its checksum
	 *	csum_ok says if csum is valid
	 */
	int doff = 0;
	int csum = 0;
	int csum_ok = 0;

	/*
	 * We can only masquerade protocols with ports... and hack some ICMPs
	 */

	h.raw = (char*) iph + iph->ihl * 4;
	size = ntohs(iph->tot_len) - (iph->ihl * 4);


	doff = proto_doff(iph->protocol, h.raw, size);
	if (doff <= 0) {
		/*	
		 *	Output path: do not pass other IP protos nor
		 *	invalid packets.
		 */
		return -1;
	}

	/* Lets determine our maddr now, shall we? */
	if (maddr == 0) {
		struct rtable *rt;
		struct rtable *skb_rt = (struct rtable*)skb->dst;
		struct device *skb_dev = skb_rt->u.dst.dev;

		if (ip_route_output(&rt, iph->daddr, 0, RT_TOS(iph->tos)|RTO_CONN, skb_dev?skb_dev->ifindex:0)) {
			/* Fallback on old method */
			/* This really shouldn't happen... */
			maddr = inet_select_addr(skb_dev, skb_rt->rt_gateway, RT_SCOPE_UNIVERSE);
		} else {
			/* Route lookup succeeded */
			maddr = rt->rt_src;
			ip_rt_put(rt);
		}
	}

	switch (iph->protocol) {
	case IPPROTO_ICMP:
		return(ip_fw_masq_icmp(skb_p, maddr));
	case IPPROTO_UDP:
		if (h.uh->check == 0)
			/* No UDP checksum */
			break;
	case IPPROTO_TCP:
		/* Make sure packet is in the masq range */
		IP_MASQ_DEBUG(3, "O-pkt: %s size=%d\n",
				masq_proto_name(iph->protocol),
				size);

#ifdef CONFIG_IP_MASQ_DEBUG
		if (ip_masq_get_debug_level() > 3) {
			skb->ip_summed = CHECKSUM_NONE;
		}
#endif
		/* Check that the checksum is OK */
		switch (skb->ip_summed)
		{
			case CHECKSUM_NONE:
			{
				csum = csum_partial(h.raw + doff, size - doff, 0);
				IP_MASQ_DEBUG(3, "O-pkt: %s I-datacsum=%d\n",
						masq_proto_name(iph->protocol),
						csum);

				skb->csum = csum_partial(h.raw , doff, csum);
			}
			case CHECKSUM_HW:
				if (csum_tcpudp_magic(iph->saddr, iph->daddr, 
						size, iph->protocol, skb->csum))
				{
					IP_MASQ_DEBUG(0, "Outgoing failed %s checksum from %d.%d.%d.%d (size=%d)!\n",
					       masq_proto_name(iph->protocol),
					       NIPQUAD(iph->saddr),
					       size);
					return -1;
				}
			default:
				/* CHECKSUM_UNNECESSARY */
		}
		break;
	default:
		return -1;
	}
	/*
	 *	Now hunt the list to see if we have an old entry
	 */

	/* h.raw = (char*) iph + iph->ihl * 4; */

 	IP_MASQ_DEBUG(2, "Outgoing %s %08lX:%04X -> %08lX:%04X\n",
  		masq_proto_name(iph->protocol),
  		ntohl(iph->saddr), ntohs(h.portp[0]),
  		ntohl(iph->daddr), ntohs(h.portp[1]));

        ms = ip_masq_out_get_iph(iph);
        if (ms!=NULL) {

                /*
                 *	If sysctl !=0 and no pkt has been received yet
                 *	in this tunnel and routing iface address has changed...
                 *	 "You are welcome, diald".
                 */
                if ( sysctl_ip_dynaddr && ms->flags & IP_MASQ_F_NO_REPLY && maddr != ms->maddr) {

                        if (sysctl_ip_dynaddr > 1) {
                                IP_MASQ_INFO( "ip_fw_masquerade(): change masq.addr from %d.%d.%d.%d to %d.%d.%d.%d\n",
                                       NIPQUAD(ms->maddr),NIPQUAD(maddr));
                        }

			write_lock(&__ip_masq_lock);

                        ip_masq_unhash(ms);
                        ms->maddr = maddr;
                        ip_masq_hash(ms);

			write_unlock(&__ip_masq_lock);
                }
                
		/*
		 *      Set sport if not defined yet (e.g. ftp PASV).  Because
		 *	masq entries are hashed on sport, unhash with old value
		 *	and hash with new.
		 */

		if ( ms->flags & IP_MASQ_F_NO_SPORT && ms->protocol == IPPROTO_TCP ) {

			write_lock(&__ip_masq_lock);
			
			ip_masq_unhash(ms);
			ms->flags &= ~IP_MASQ_F_NO_SPORT;
			ms->sport = h.portp[0];
			ip_masq_hash(ms);	/* hash on new sport */

			write_unlock(&__ip_masq_lock);
			
			IP_MASQ_DEBUG(1, "ip_fw_masquerade(): filled sport=%d\n",
			       ntohs(ms->sport));
		}
		if (ms->flags & IP_MASQ_F_DLOOSE) {
			/*
			 *	update dest loose values
			 */
			ms->dport = h.portp[1];
			ms->daddr = iph->daddr;
		}
        } else {
		/*
		 *	Nope, not found, create a new entry for it
		 */

#ifdef CONFIG_IP_MASQUERADE_MOD
		if (!(ms = ip_masq_mod_out_create(skb, iph, maddr))) 
#endif
			ms = ip_masq_new(iph->protocol,
					maddr, 0,
					iph->saddr, h.portp[0],
					iph->daddr, h.portp[1],
					0);
                if (ms == NULL)
			return -1;
 	}

	/*
 	 * 	Call module's output update hook
	 */

#ifdef CONFIG_IP_MASQUERADE_MOD
	ip_masq_mod_out_update(skb, iph, ms);
#endif

 	/*
 	 *	Change the fragments origin
 	 */

 	size = skb->len - (h.raw - skb->nh.raw);

        /*
         *	Set iph addr and port from ip_masq obj.
         */
 	iph->saddr = ms->maddr;
 	h.portp[0] = ms->mport;

	/*
	 *	Invalidate csum saving if tunnel has masq helper
	 */

	if (ms->app) 
		csum_ok = 0;

 	/*
 	 *	Attempt ip_masq_app call.
         *	will fix ip_masq and iph seq stuff
 	 */
        if (ip_masq_app_pkt_out(ms, skb_p, maddr) != 0)
	{
                /*
                 *	skb has possibly changed, update pointers.
                 */
                skb = *skb_p;
                iph = skb->nh.iph;
		h.raw = (char*) iph + iph->ihl *4;
                size = skb->len - (h.raw - skb->nh.raw);
		/* doff should have not changed */
        }

 	/*
 	 *	Adjust packet accordingly to protocol
 	 */

	/*
	 *	Transport's payload partial csum
	 */

	if (!csum_ok) {
		csum = csum_partial(h.raw + doff, size - doff, 0);
	}
	skb->csum = csum;

	IP_MASQ_DEBUG(3, "O-pkt: %s size=%d O-datacsum=%d\n",
			masq_proto_name(iph->protocol),
			size,
			csum);

	/*
	 * 	Protocol csum
	 */
	switch (iph->protocol) {
		case IPPROTO_TCP:
			h.th->check = 0;
			h.th->check=csum_tcpudp_magic(iph->saddr, iph->daddr, 
					size, iph->protocol, 
					csum_partial(h.raw , doff, csum));
			IP_MASQ_DEBUG(3, "O-pkt: %s O-csum=%d (+%d)\n",
					masq_proto_name(iph->protocol),
					h.th->check,
					(char*) & (h.th->check) - (char*) h.raw);

			break;
		case IPPROTO_UDP:
			h.uh->check = 0;
			h.uh->check=csum_tcpudp_magic(iph->saddr, iph->daddr, 
					size, iph->protocol, 
					csum_partial(h.raw , doff, csum));
			if (h.uh->check == 0) 
				h.uh->check = 0xFFFF;
			IP_MASQ_DEBUG(3, "O-pkt: %s O-csum=%d (+%d)\n",
					masq_proto_name(iph->protocol),
					h.uh->check,
					(char*) &(h.uh->check)- (char*) h.raw);
			break;
	}
	ip_send_check(iph);

  	IP_MASQ_DEBUG(2, "O-routed from %08lX:%04X with masq.addr %08lX\n",
		ntohl(ms->maddr),ntohs(ms->mport),ntohl(maddr));

	masq_set_state(ms, 1, iph, h.portp);
	ip_masq_put(ms);

	return 0;
 }

/*
 *	Restore original addresses and ports in the original IP
 *	datagram if the failing packet has been [de]masqueraded.
 *	This is ugly in the extreme.  We no longer have the original
 *	packet so we have to reconstruct it from the failing packet
 *	plus data in the masq tables.  The resulting "original data"
 *	should be good enough to tell the sender which session to
 *	throttle.  Relies on far too much knowledge of masq internals,
 *	there ought to be a better way - KAO 990303.
 *
 *	Moved here from icmp.c - JJC.
 *	Already known: type == ICMP_DEST_UNREACH, IPSKB_MASQUERADED
 *	skb->nh.iph points to original header.
 *
 *	Must try both OUT and IN tables; we could add a flag
 *	ala IPSKB_MASQUERADED to avoid 2nd tables lookup, but this is VERY
 *	unlike because routing makes mtu decision before reaching 
 *	ip_fw_masquerade().
 *	
 */
int ip_fw_unmasq_icmp(struct sk_buff *skb) {
	struct ip_masq *ms;
	struct iphdr *iph = skb->nh.iph;
	__u16 *portp = (__u16 *)&(((char *)iph)[iph->ihl*4]);

	/* 
	 *	Always called from _bh context: use read_[un]lock()
	 */

	/*
	 * 	Peek "out" table, this packet has bounced:
	 *	out->in(frag_needed!)->OUT[icmp]
	 *
	 *	iph->daddr is IN host
	 *	iph->saddr is OUT host
	 */
	read_lock(&__ip_masq_lock);
	ms = __ip_masq_out_get(iph->protocol,
			iph->daddr, portp[1],
			iph->saddr, portp[0]);
	read_unlock(&__ip_masq_lock);
	if (ms) {
		IP_MASQ_DEBUG(1, "Incoming frag_need rewrited from %d.%d.%d.%d to %d.%d.%d.%d\n",
			NIPQUAD(iph->daddr), NIPQUAD(ms->maddr));
		iph->daddr = ms->maddr;
		portp[1] = ms->mport;
		__ip_masq_put(ms);
		return 1;
	}
	/*
	 * 	Peek "in" table
	 *	in->out(frag_needed!)->IN[icmp]
	 *
	 *	iph->daddr is OUT host
	 *	iph->saddr is MASQ host
	 *
	 */
	read_lock(&__ip_masq_lock);
	ms = __ip_masq_in_get(iph->protocol,
			iph->daddr, portp[1],
			iph->saddr, portp[0]);
	read_unlock(&__ip_masq_lock);
	if (ms) {
		IP_MASQ_DEBUG(1, "Outgoing frag_need rewrited from %d.%d.%d.%d to %d.%d.%d.%d\n",
			NIPQUAD(iph->saddr), NIPQUAD(ms->saddr));
		iph->saddr = ms->saddr;
		portp[0] = ms->sport;
		__ip_masq_put(ms);
		return 1;
	}
	return 0;

}
/*
 *	Handle ICMP messages in forward direction.
 *	Find any that might be relevant, check against existing connections,
 *	forward to masqueraded host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded
 */

int ip_fw_masq_icmp(struct sk_buff **skb_p, __u32 maddr)
{
        struct sk_buff 	*skb   = *skb_p;
 	struct iphdr	*iph   = skb->nh.iph;
	struct icmphdr  *icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
	struct iphdr    *ciph;	/* The ip header contained within the ICMP */
	__u16	        *pptr;	/* port numbers from TCP/UDP contained header */
	struct ip_masq	*ms;
	unsigned short   len   = ntohs(iph->tot_len) - (iph->ihl * 4);

 	IP_MASQ_DEBUG(2, "Incoming forward ICMP (%d,%d) %lX -> %lX\n",
	        icmph->type, ntohs(icmp_id(icmph)),
 		ntohl(iph->saddr), ntohl(iph->daddr));

#ifdef CONFIG_IP_MASQUERADE_ICMP		
	if ((icmph->type == ICMP_ECHO ) ||
	    (icmph->type == ICMP_TIMESTAMP ) ||
	    (icmph->type == ICMP_INFO_REQUEST ) ||
	    (icmph->type == ICMP_ADDRESS )) {

		IP_MASQ_DEBUG(2, "icmp request rcv %lX->%lX  id %d type %d\n",
		       ntohl(iph->saddr),
		       ntohl(iph->daddr),
		       ntohs(icmp_id(icmph)),
		       icmph->type);

		ms = ip_masq_out_get(iph->protocol,
				       iph->saddr,
				       icmp_id(icmph),
				       iph->daddr,
				       icmp_hv_req(icmph));
		if (ms == NULL) {
			ms = ip_masq_new(iph->protocol,
					 maddr, 0,
					 iph->saddr, icmp_id(icmph),
					 iph->daddr, icmp_hv_req(icmph),
					 0);
			if (ms == NULL)
				return (-1);
			IP_MASQ_DEBUG(1, "Created new icmp entry\n");
		}
		/* Rewrite source address */
                
                /*
                 *	If sysctl !=0 and no pkt has been received yet
                 *	in this tunnel and routing iface address has changed...
                 *	 "You are welcome, diald".
                 */
                if ( sysctl_ip_dynaddr && ms->flags & IP_MASQ_F_NO_REPLY && maddr != ms->maddr) {

                        if (sysctl_ip_dynaddr > 1) {
				IP_MASQ_INFO( "ip_fw_masq_icmp(): change masq.addr %d.%d.%d.%d to %d.%d.%d.%d",
				       NIPQUAD(ms->maddr), NIPQUAD(maddr));
			}

			write_lock(&__ip_masq_lock);
			
                        ip_masq_unhash(ms);
                        ms->maddr = maddr;
                        ip_masq_hash(ms);

			write_unlock(&__ip_masq_lock);
                }
                
		iph->saddr = ms->maddr;
		ip_send_check(iph);
		/* Rewrite port (id) */
		(icmph->un).echo.id = ms->mport;
		icmph->checksum = 0;
		icmph->checksum = ip_compute_csum((unsigned char *)icmph, len);

		IP_MASQ_DEBUG(2, "icmp request rwt %lX->%lX id %d type %d\n",
		       ntohl(iph->saddr),
		       ntohl(iph->daddr),
		       ntohs(icmp_id(icmph)),
		       icmph->type);

		masq_set_state(ms, 1, iph, icmph);
		ip_masq_put(ms);

		return 1;
	}
#endif

	/*
	 * Work through seeing if this is for us.
	 * These checks are supposed to be in an order that
	 * means easy things are checked first to speed up
	 * processing.... however this means that some
	 * packets will manage to get a long way down this
	 * stack and then be rejected, but thats life
	 */
	if ((icmph->type != ICMP_DEST_UNREACH) &&
	    (icmph->type != ICMP_SOURCE_QUENCH) &&
	    (icmph->type != ICMP_TIME_EXCEEDED))
		return 0;

	/* Now find the contained IP header */
	ciph = (struct iphdr *) (icmph + 1);

#ifdef CONFIG_IP_MASQUERADE_ICMP
	if (ciph->protocol == IPPROTO_ICMP) {
		/*
		 * This section handles ICMP errors for ICMP packets
		 */
		struct icmphdr  *cicmph = (struct icmphdr *)((char *)ciph + 
							     (ciph->ihl<<2));


		IP_MASQ_DEBUG(2, "fw icmp/icmp rcv %lX->%lX id %d type %d\n",
		       ntohl(ciph->saddr),
		       ntohl(ciph->daddr),
		       ntohs(icmp_id(cicmph)),
		       cicmph->type);

		read_lock(&__ip_masq_lock);
		ms = __ip_masq_out_get(ciph->protocol, 
				      ciph->daddr,
				      icmp_id(cicmph),
				      ciph->saddr,
				      icmp_hv_rep(cicmph));
		read_unlock(&__ip_masq_lock);

		if (ms == NULL)
			return 0;

		/* Now we do real damage to this packet...! */
		/* First change the source IP address, and recalc checksum */
		iph->saddr = ms->maddr;
		ip_send_check(iph);
	
		/* Now change the *dest* address in the contained IP */
		ciph->daddr = ms->maddr;
		__ip_masq_put(ms);

		ip_send_check(ciph);

		/* Change the ID to the masqed one! */
		(cicmph->un).echo.id = ms->mport;
	
		/* And finally the ICMP checksum */
		icmph->checksum = 0;
		icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);


		IP_MASQ_DEBUG(2, "fw icmp/icmp rwt %lX->%lX id %d type %d\n",
		       ntohl(ciph->saddr),
		       ntohl(ciph->daddr),
		       ntohs(icmp_id(cicmph)),
		       cicmph->type);

		return 1;
	}
#endif /* CONFIG_IP_MASQUERADE_ICMP */

	/* We are only interested ICMPs generated from TCP or UDP packets */
	if ((ciph->protocol != IPPROTO_UDP) && (ciph->protocol != IPPROTO_TCP))
		return 0;

	/*
	 * Find the ports involved - this packet was
	 * incoming so the ports are right way round
	 * (but reversed relative to outer IP header!)
	 */
	pptr = (__u16 *)&(((char *)ciph)[ciph->ihl*4]);
#if 0
	if (ntohs(pptr[1]) < PORT_MASQ_BEGIN ||
 	    ntohs(pptr[1]) > PORT_MASQ_END)
 		return 0;
#endif

	/* Ensure the checksum is correct */
	if (ip_compute_csum((unsigned char *) icmph, len))
	{
		/* Failed checksum! */
		IP_MASQ_DEBUG(0, "forward ICMP: failed checksum from %d.%d.%d.%d!\n",
			      NIPQUAD(iph->saddr));
		return(-1);
	}


 	IP_MASQ_DEBUG(2, "Handling forward ICMP for %08lX:%04X -> %08lX:%04X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));


#if 0
	/* This is pretty much what __ip_masq_in_get_iph() does */
	ms = __ip_masq_in_get(ciph->protocol, ciph->saddr, pptr[0], ciph->daddr, pptr[1]);
#endif
	read_lock(&__ip_masq_lock);
	ms = __ip_masq_out_get(ciph->protocol,
			       ciph->daddr,
			       pptr[1],
			       ciph->saddr,
			       pptr[0]);
	read_unlock(&__ip_masq_lock);

	if (ms == NULL)
		return 0;

	/* Now we do real damage to this packet...! */
	/* First change the source IP address, and recalc checksum */
	iph->saddr = ms->maddr;
	ip_send_check(iph);

	/* Now change the *dest* address in the contained IP */
	ciph->daddr = ms->maddr;
	ip_send_check(ciph);

	/* the TCP/UDP dest port - cannot redo check */
	pptr[1] = ms->mport;
	__ip_masq_put(ms);

	/* And finally the ICMP checksum */
	icmph->checksum = 0;
	icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);


 	IP_MASQ_DEBUG(2, "Rewrote forward ICMP to %08lX:%04X -> %08lX:%04X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));


	return 1;
}


/*
 *	Own skb_cow() beast, tweaked for rewriting commonly
 *	used pointers in masq code
 */
static struct sk_buff * masq_skb_cow(struct sk_buff **skb_p, 
			struct iphdr **iph_p, unsigned char **t_p) {
	struct sk_buff *skb=(*skb_p);
	if (skb_cloned(skb)) {
		skb = skb_copy(skb, GFP_ATOMIC);
		if (skb) {
			/*
			 *	skb changed, update other pointers
			 */
			struct iphdr *iph = skb->nh.iph;
			kfree_skb(*skb_p);
			*skb_p = skb;
			*iph_p = iph;
			*t_p = (char*) iph + iph->ihl * 4;
		}
	}
	return skb;
}

/*
 *	Handle ICMP messages in reverse (demasquerade) direction.
 *	Find any that might be relevant, check against existing connections,
 *	forward to masqueraded host if relevant.
 *	Currently handles error types - unreachable, quench, ttl exceeded
 */

int ip_fw_demasq_icmp(struct sk_buff **skb_p)
{
        struct sk_buff 	*skb   = *skb_p;
 	struct iphdr	*iph   = skb->nh.iph;
	struct icmphdr  *icmph = (struct icmphdr *)((char *)iph + (iph->ihl<<2));
	struct iphdr    *ciph;	/* The ip header contained within the ICMP */
	__u16	        *pptr;	/* port numbers from TCP/UDP contained header */
	struct ip_masq	*ms;
	unsigned short   len   = ntohs(iph->tot_len) - (iph->ihl * 4);


 	IP_MASQ_DEBUG(2, "icmp in/rev (%d,%d) %lX -> %lX\n",
	        icmph->type, ntohs(icmp_id(icmph)),
 		ntohl(iph->saddr), ntohl(iph->daddr));


#ifdef CONFIG_IP_MASQUERADE_ICMP		
	if ((icmph->type == ICMP_ECHOREPLY) ||
	    (icmph->type == ICMP_TIMESTAMPREPLY) ||
	    (icmph->type == ICMP_INFO_REPLY) ||
	    (icmph->type == ICMP_ADDRESSREPLY))	{

		IP_MASQ_DEBUG(2, "icmp reply rcv %lX->%lX id %d type %d, req %d\n",
		       ntohl(iph->saddr),
		       ntohl(iph->daddr),
		       ntohs(icmp_id(icmph)),
		       icmph->type,
		       icmp_type_request(icmph->type));

		ms = ip_masq_in_get(iph->protocol,
				      iph->saddr,
				      icmp_hv_rep(icmph),
				      iph->daddr,
				      icmp_id(icmph));
		if (ms == NULL)
			return 0;

                /*
                 *	got reply, so clear flag
                 */
                ms->flags &= ~IP_MASQ_F_NO_REPLY;

		if ((skb=masq_skb_cow(skb_p, &iph, (unsigned char**)&icmph)) == NULL) {
			ip_masq_put(ms);
			return -1;
		}

		/* Reset source address */
		iph->daddr = ms->saddr;
		/* Redo IP header checksum */
		ip_send_check(iph);
		/* Set ID to fake port number */
		(icmph->un).echo.id = ms->sport;
		/* Reset ICMP checksum and set expiry */
		icmph->checksum=0;
		icmph->checksum=ip_compute_csum((unsigned char *)icmph,len);



		IP_MASQ_DEBUG(2, "icmp reply rwt %lX->%lX id %d type %d\n",
		       ntohl(iph->saddr),
		       ntohl(iph->daddr),
		       ntohs(icmp_id(icmph)),
		       icmph->type);

		masq_set_state(ms, 0, iph, icmph);
		ip_masq_put(ms);

		return 1;
	} else {
#endif
		if ((icmph->type != ICMP_DEST_UNREACH) &&
		    (icmph->type != ICMP_SOURCE_QUENCH) &&
		    (icmph->type != ICMP_TIME_EXCEEDED))
			return 0;
#ifdef CONFIG_IP_MASQUERADE_ICMP
	}
#endif
	/*
	 * If we get here we have an ICMP error of one of the above 3 types
	 * Now find the contained IP header
	 */

	ciph = (struct iphdr *) (icmph + 1);

#ifdef CONFIG_IP_MASQUERADE_ICMP
	if (ciph->protocol == IPPROTO_ICMP) {
		/*
		 * This section handles ICMP errors for ICMP packets
		 *
		 * First get a new ICMP header structure out of the IP packet
		 */
		struct icmphdr  *cicmph = (struct icmphdr *)((char *)ciph + 
							     (ciph->ihl<<2));


		IP_MASQ_DEBUG(2, "rv icmp/icmp rcv %lX->%lX id %d type %d\n",
		       ntohl(ciph->saddr),
		       ntohl(ciph->daddr),
		       ntohs(icmp_id(cicmph)),
		       cicmph->type);

		read_lock(&__ip_masq_lock);
		ms = __ip_masq_in_get(ciph->protocol, 
				      ciph->daddr, 
				      icmp_hv_req(cicmph),
				      ciph->saddr, 
				      icmp_id(cicmph));
		read_unlock(&__ip_masq_lock);

		if (ms == NULL)
			return 0;

		if ((skb=masq_skb_cow(skb_p, &iph, (unsigned char**)&icmph)) == NULL) {
			__ip_masq_put(ms);
			return -1;
		}
		ciph = (struct iphdr *) (icmph + 1);
		cicmph = (struct icmphdr *)((char *)ciph + 
					    (ciph->ihl<<2));
		/* Now we do real damage to this packet...! */
		/* First change the dest IP address, and recalc checksum */
		iph->daddr = ms->saddr;
		ip_send_check(iph);
	
		/* Now change the *source* address in the contained IP */
		ciph->saddr = ms->saddr;
		ip_send_check(ciph);

		/* Change the ID to the original one! */
		(cicmph->un).echo.id = ms->sport;
		__ip_masq_put(ms);

		/* And finally the ICMP checksum */
		icmph->checksum = 0;
		icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);


		IP_MASQ_DEBUG(2, "rv icmp/icmp rwt %lX->%lX id %d type %d\n",
		       ntohl(ciph->saddr),
		       ntohl(ciph->daddr),
		       ntohs(icmp_id(cicmph)),
		       cicmph->type);

		return 1;
	}
#endif /* CONFIG_IP_MASQUERADE_ICMP */

	/* We are only interested ICMPs generated from TCP or UDP packets */
	if ((ciph->protocol != IPPROTO_UDP) && 
	    (ciph->protocol != IPPROTO_TCP))
		return 0;

	/*
	 * Find the ports involved - remember this packet was
	 * *outgoing* so the ports are reversed (and addresses)
	 */
	pptr = (__u16 *)&(((char *)ciph)[ciph->ihl*4]);
	if (ntohs(pptr[0]) < PORT_MASQ_BEGIN ||
 	    ntohs(pptr[0]) > PORT_MASQ_END)
 		return 0;

	/* Ensure the checksum is correct */
	if (ip_compute_csum((unsigned char *) icmph, len))
	{
		/* Failed checksum! */
		IP_MASQ_ERR( "reverse ICMP: failed checksum from %d.%d.%d.%d!\n",
		       NIPQUAD(iph->saddr));
		return(-1);
	}


 	IP_MASQ_DEBUG(2, "Handling reverse ICMP for %08lX:%04X -> %08lX:%04X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));


	/* This is pretty much what __ip_masq_in_get_iph() does, except params are wrong way round */
	read_lock(&__ip_masq_lock);
	ms = __ip_masq_in_get(ciph->protocol,
			      ciph->daddr,
			      pptr[1],
			      ciph->saddr,
			      pptr[0]);
	read_unlock(&__ip_masq_lock);

	if (ms == NULL)
		return 0;

	if ((skb=masq_skb_cow(skb_p, &iph, (unsigned char**)&icmph)) == NULL) {
		__ip_masq_put(ms);
		return -1;
	}
	ciph = (struct iphdr *) (icmph + 1);
	pptr = (__u16 *)&(((char *)ciph)[ciph->ihl*4]);

	/* Now we do real damage to this packet...! */
	/* First change the dest IP address, and recalc checksum */
	iph->daddr = ms->saddr;
	ip_send_check(iph);

	/* Now change the *source* address in the contained IP */
	ciph->saddr = ms->saddr;
	ip_send_check(ciph);

	/* the TCP/UDP source port - cannot redo check */
	pptr[0] = ms->sport;
	__ip_masq_put(ms);

	/* And finally the ICMP checksum */
	icmph->checksum = 0;
	icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);


 	IP_MASQ_DEBUG(2, "Rewrote reverse ICMP to %08lX:%04X -> %08lX:%04X\n",
	       ntohl(ciph->saddr), ntohs(pptr[0]),
	       ntohl(ciph->daddr), ntohs(pptr[1]));


	return 1;
}

 /*
  *	Check if it's an masqueraded port, look it up,
  *	and send it on its way...
  *
  *	Better not have many hosts using the designated portrange
  *	as 'normal' ports, or you'll be spending many time in
  *	this function.
  */

int ip_fw_demasquerade(struct sk_buff **skb_p)
{
	struct sk_buff 	*skb = *skb_p;
	struct iphdr	*iph = skb->nh.iph;
	union ip_masq_tphdr h;
	struct ip_masq	*ms;
	unsigned short size;
	int doff = 0;
	int csum = 0;
	int csum_ok = 0;
	__u32 maddr;

	/*
	 *	Big tappo: only PACKET_HOST (nor loopback neither mcasts)
	 *	... don't know why 1st test DOES NOT include 2nd (?)
	 */

	if (skb->pkt_type != PACKET_HOST || skb->dev == &loopback_dev) {
		IP_MASQ_DEBUG(2, "ip_fw_demasquerade(): packet type=%d proto=%d daddr=%d.%d.%d.%d ignored\n",
			skb->pkt_type,
			iph->protocol,
			NIPQUAD(iph->daddr));
		return 0;
	}

	h.raw = (char*) iph + iph->ihl * 4;

	/*
	 *	IP payload size
	 */
	size = ntohs(iph->tot_len) - (iph->ihl * 4);

	doff = proto_doff(iph->protocol, h.raw, size);

	switch (doff) {
		case 0:
			/*
			 *	Input path: other IP protos Ok, will
			 *	reach local sockets path.
			 */
			return 0;
		case -1:
			IP_MASQ_DEBUG(0, "I-pkt invalid packet data size\n");
			return -1;
	}

	maddr = iph->daddr;
	switch (iph->protocol) {
	case IPPROTO_ICMP:
		return(ip_fw_demasq_icmp(skb_p));
	case IPPROTO_TCP:
	case IPPROTO_UDP:
		/* 
		 *	Make sure packet is in the masq range 
		 *	... or some mod-ule relaxes input range
		 *	... or there is still some `special' mport opened
		 */
		if ((ntohs(h.portp[1]) < PORT_MASQ_BEGIN
				|| ntohs(h.portp[1]) > PORT_MASQ_END)
#ifdef CONFIG_IP_MASQUERADE_MOD
				&& (ip_masq_mod_in_rule(skb, iph) != 1) 
#endif
				&& atomic_read(&mport_count) == 0 )
			return 0;

		/* Check that the checksum is OK */
		if ((iph->protocol == IPPROTO_UDP) && (h.uh->check == 0))
			/* No UDP checksum */
			break;
#ifdef CONFIG_IP_MASQ_DEBUG
		if (ip_masq_get_debug_level() > 3) {
			skb->ip_summed = CHECKSUM_NONE;
		}
#endif

		switch (skb->ip_summed)
		{
			case CHECKSUM_NONE:
				csum = csum_partial(h.raw + doff, size - doff, 0);
				csum_ok++;
				skb->csum = csum_partial(h.raw , doff, csum);

			case CHECKSUM_HW:
				if (csum_tcpudp_magic(iph->saddr, iph->daddr, 
						size, iph->protocol, skb->csum))
				{
					IP_MASQ_DEBUG(0, "Incoming failed %s checksum from %d.%d.%d.%d (size=%d)!\n",
					       masq_proto_name(iph->protocol),
					       NIPQUAD(iph->saddr),
					       size);
					return -1;
				}
			default:
				/* CHECKSUM_UNNECESSARY */
		}
		break;
	default:
		return 0;
	}



 	IP_MASQ_DEBUG(2, "Incoming %s %08lX:%04X -> %08lX:%04X\n",
 		masq_proto_name(iph->protocol),
 		ntohl(iph->saddr), ntohs(h.portp[0]),
 		ntohl(iph->daddr), ntohs(h.portp[1]));

 	/*
 	 * reroute to original host:port if found...
         */

        ms = ip_masq_in_get_iph(iph);

	/*
 	 * 	Give additional modules a chance to create an entry
	 */
#ifdef CONFIG_IP_MASQUERADE_MOD
	if (!ms) 
		ms = ip_masq_mod_in_create(skb, iph, maddr);

	/*
 	 * 	Call module's input update hook
	 */
	ip_masq_mod_in_update(skb, iph, ms);
#endif


        if (ms != NULL)
        {

                /*
                 *	got reply, so clear flag
                 */
                ms->flags &= ~IP_MASQ_F_NO_REPLY;
                
		/*
		 *	Set daddr,dport if not defined yet
		 *	and tunnel is not setup as "dest loose"
                 */

		if (ms->flags & IP_MASQ_F_DLOOSE) {
			/*
			 *	update dest loose values
			 */
			ms->dport = h.portp[0];
			ms->daddr = iph->saddr;
		} else {
                if ( ms->flags & IP_MASQ_F_NO_DPORT ) { /*  && ms->protocol == IPPROTO_TCP ) { */

			write_lock(&__ip_masq_lock);

			ip_masq_unhash(ms);
                        ms->flags &= ~IP_MASQ_F_NO_DPORT;
                        ms->dport = h.portp[0];
			ip_masq_hash(ms);	/* hash on new dport */

			write_unlock(&__ip_masq_lock);

                        IP_MASQ_DEBUG(1, "ip_fw_demasquerade(): filled dport=%d\n",
                               ntohs(ms->dport));

                }
                if (ms->flags & IP_MASQ_F_NO_DADDR ) { /*  && ms->protocol == IPPROTO_TCP)  { */

			write_lock(&__ip_masq_lock);

			ip_masq_unhash(ms);
                        ms->flags &= ~IP_MASQ_F_NO_DADDR;
                        ms->daddr = iph->saddr;
			ip_masq_hash(ms);	/* hash on new daddr */

			write_unlock(&__ip_masq_lock);

                        IP_MASQ_DEBUG(1, "ip_fw_demasquerade(): filled daddr=%lX\n",
                               ntohl(ms->daddr));

                }
		}
		if ((skb=masq_skb_cow(skb_p, &iph, &h.raw)) == NULL) {
			ip_masq_put(ms);
			return -1;
		}
                iph->daddr = ms->saddr;
                h.portp[1] = ms->sport;

		/*
		 *	Invalidate csum saving if tunnel has masq helper
		 */

		if (ms->app) 
			csum_ok = 0;

                /*
                 *	Attempt ip_masq_app call.
                 *	will fix ip_masq and iph ack_seq stuff
                 */

                if (ip_masq_app_pkt_in(ms, skb_p, maddr) != 0)
                {
                        /*
                         *	skb has changed, update pointers.
                         */

                        skb = *skb_p;
                        iph = skb->nh.iph;
			h.raw = (char*) iph + iph->ihl*4;
                        size = ntohs(iph->tot_len) - (iph->ihl * 4);
                }

                /*
                 * Yug! adjust UDP/TCP checksums
		 */

		/*
		 *	Transport's payload partial csum
		 */

		if (!csum_ok) {
			csum = csum_partial(h.raw + doff, size - doff, 0);
		}
		skb->csum = csum;

		/*
		 * 	Protocol csum
		 */
		switch (iph->protocol) {
			case IPPROTO_TCP:
				h.th->check = 0;
				h.th->check=csum_tcpudp_magic(iph->saddr, iph->daddr, 
						size, iph->protocol, 
						csum_partial(h.raw , doff, csum));
				break;
			case IPPROTO_UDP:
				h.uh->check = 0;
				h.uh->check=csum_tcpudp_magic(iph->saddr, iph->daddr, 
						size, iph->protocol, 
						csum_partial(h.raw , doff, csum));
				if (h.uh->check == 0) 
					h.uh->check = 0xFFFF;
				break;
		}
                ip_send_check(iph);

                IP_MASQ_DEBUG(2, "I-routed to %08lX:%04X\n",ntohl(iph->daddr),ntohs(h.portp[1]));

		masq_set_state (ms, 0, iph, h.portp);
		ip_masq_put(ms);

                return 1;
 	}

 	/* sorry, all this trouble for a no-hit :) */
 	return 0;
}


void ip_masq_control_add(struct ip_masq *ms, struct ip_masq* ctl_ms)
{
	if (ms->control) {
		IP_MASQ_ERR( "request control ADD for already controlled: %d.%d.%d.%d:%d to %d.%d.%d.%d:%d\n",
				NIPQUAD(ms->saddr),ntohs(ms->sport),
				NIPQUAD(ms->daddr),ntohs(ms->dport));
		ip_masq_control_del(ms);
	}
	IP_MASQ_DEBUG(1, "ADDing control for: ms.dst=%d.%d.%d.%d:%d ctl_ms.dst=%d.%d.%d.%d:%d\n",
				NIPQUAD(ms->daddr),ntohs(ms->dport),
				NIPQUAD(ctl_ms->daddr),ntohs(ctl_ms->dport));
	ms->control = ctl_ms;
	atomic_inc(&ctl_ms->n_control);
}

void ip_masq_control_del(struct ip_masq *ms)
{
	struct ip_masq *ctl_ms = ms->control;
	if (!ctl_ms) {
		IP_MASQ_ERR( "request control DEL for uncontrolled: %d.%d.%d.%d:%d to %d.%d.%d.%d:%d\n",
				NIPQUAD(ms->saddr),ntohs(ms->sport),
				NIPQUAD(ms->daddr),ntohs(ms->dport));
			return;
	}
	IP_MASQ_DEBUG(1, "DELeting control for: ms.dst=%d.%d.%d.%d:%d ctl_ms.dst=%d.%d.%d.%d:%d\n",
				NIPQUAD(ms->daddr),ntohs(ms->dport),
				NIPQUAD(ctl_ms->daddr),ntohs(ctl_ms->dport));
	ms->control = NULL;
	if (atomic_read(&ctl_ms->n_control) == 0) {
		IP_MASQ_ERR( "BUG control DEL with n=0 : %d.%d.%d.%d:%d to %d.%d.%d.%d:%d\n",
				NIPQUAD(ms->saddr),ntohs(ms->sport),
				NIPQUAD(ms->daddr),ntohs(ms->dport));
			return;
		
	}
	atomic_dec(&ctl_ms->n_control);
}

struct ip_masq * ip_masq_control_get(struct ip_masq *ms)
{
	return ms->control;
}


#ifdef CONFIG_PROC_FS
/*
 *	/proc/net entries
 *	From userspace
 */
static int ip_msqhst_procinfo(char *buffer, char **start, off_t offset,
			      int length, int unused)
{
	off_t pos=0, begin;
	struct ip_masq *ms;
	char temp[129];
        int idx = 0;
	int len=0;
	struct list_head *l,*e;

	if (offset < 128)
	{
		sprintf(temp,
			"Prc FromIP   FPrt ToIP     TPrt Masq Init-seq  Delta PDelta Expires (free=%d,%d,%d)",
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
	read_lock_bh(&__ip_masq_lock);

	l = &ip_masq_m_table[idx];
	for (e=l->next; e!=l; e=e->next) {
		ms = list_entry(e, struct ip_masq, m_list);
		pos += 128;
		if (pos <= offset) {
			len = 0;
			continue;
		}

		/*
		 *	We have locked the tables, no need to del/add timers
		 *	nor cli()  8)
		 */

		sprintf(temp,"%s %08X:%04X %08X:%04X %04X %08X %6d %6d %7lu",
			masq_proto_name(ms->protocol),
			ntohl(ms->saddr), ntohs(ms->sport),
			ntohl(ms->daddr), ntohs(ms->dport),
			ntohs(ms->mport),
			ms->out_seq.init_seq,
			ms->out_seq.delta,
			ms->out_seq.previous_delta,
			ms->timer.expires-jiffies);
		len += sprintf(buffer+len, "%-127s\n", temp);

		if(len >= length) {

			read_unlock_bh(&__ip_masq_lock);
			goto done;
		}
        }
	read_unlock_bh(&__ip_masq_lock);

	}
done:


	begin = len - (pos - offset);
	*start = buffer + begin;
	len -= begin;
	if(len>length)
		len = length;
	return len;
}

#endif

/* 
 *	Timeouts handling by ipfwadm/ipchains
 * 	From ip_fw.c
 */

int ip_fw_masq_timeouts(void *m, int len) 
{
	struct ip_fw_masq *masq;
	int ret = EINVAL;

	if (len != sizeof(struct ip_fw_masq)) {
		IP_MASQ_DEBUG(1, "ip_fw_masq_timeouts: length %d, expected %d\n",
				len, sizeof(struct ip_fw_masq));
	} else {
		masq = (struct ip_fw_masq *)m;
		if (masq->tcp_timeout)
			masq_timeout_table.timeout[IP_MASQ_S_ESTABLISHED]
				= masq->tcp_timeout;

		if (masq->tcp_fin_timeout)
			masq_timeout_table.timeout[IP_MASQ_S_FIN_WAIT]
				= masq->tcp_fin_timeout;

		if (masq->udp_timeout)
			masq_timeout_table.timeout[IP_MASQ_S_UDP]
				= masq->udp_timeout;
		ret = 0;
	}
	return ret;
}
/*
 *	Module autoloading stuff
 */

static int ip_masq_user_check_hook(void) {
#ifdef CONFIG_KMOD
	if (ip_masq_user_hook == NULL) {
		IP_MASQ_DEBUG(1, "About to request \"ip_masq_user\" module\n");
		request_module("ip_masq_user");
	}
#endif /* CONFIG_KMOD */
	return (ip_masq_user_hook != NULL);
}

/*
 *	user module hook- info
 */
static int ip_masq_user_info(char *buffer, char **start, off_t offset,
			      int len, int *eof, void *data)
{
	int ret = -ENOPKG;
	if (ip_masq_user_check_hook()) {
		ret = ip_masq_user_hook->info(buffer, start, offset, len, (int) data);
	}
	return ret;
}

/*
 *	user module hook- entry mgmt
 */
static int ip_masq_user_ctl(int optname, void *arg, int arglen)
{
	int ret = -ENOPKG;
	if (ip_masq_user_check_hook())  {
		ret = ip_masq_user_hook->ctl(optname, arg, arglen);
	}
	return ret;
}

/*
 *	Control from ip_sockglue
 *	MAIN ENTRY point from userspace (apart from /proc *info entries)
 *	Returns errno
 */
int ip_masq_uctl(int optname, char * optval , int optlen)
{
	struct ip_masq_ctl masq_ctl;
	int ret = -EINVAL;

	if(optlen>sizeof(masq_ctl))
		return -EINVAL;

	if(copy_from_user(&masq_ctl,optval,optlen))
		return -EFAULT;

	IP_MASQ_DEBUG(1,"ip_masq_ctl(optname=%d, optlen=%d, target=%d, cmd=%d)\n",
		optname, optlen, masq_ctl.m_target, masq_ctl.m_cmd);

	switch (masq_ctl.m_target) {
		case IP_MASQ_TARGET_USER:
			ret = ip_masq_user_ctl(optname, &masq_ctl, optlen);
			break;
#ifdef CONFIG_IP_MASQUERADE_MOD
		case IP_MASQ_TARGET_MOD:
			ret = ip_masq_mod_ctl(optname, &masq_ctl, optlen);
			break;
#endif
	}

	/* 	
	 *	If ret>0, copy to user space 
	 */

	if (ret > 0 && ret <= sizeof (masq_ctl)) {
		if (copy_to_user(optval, &masq_ctl, ret) )
			return -EFAULT;
		ret = 0;
	}

	return ret;
}

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry	*proc_net_ip_masq = NULL;

#ifdef MODULE
static void ip_masq_proc_count(struct inode *inode, int fill)
{
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
}
#endif

int ip_masq_proc_register(struct proc_dir_entry *ent)
{
	if (!proc_net_ip_masq) return -1;
	IP_MASQ_DEBUG(1, "registering \"/proc/net/ip_masq/%s\" entry\n",
			ent->name);
	return proc_register(proc_net_ip_masq, ent);
}
void ip_masq_proc_unregister(struct proc_dir_entry *ent)
{
	if (!proc_net_ip_masq) return;
	IP_MASQ_DEBUG(1, "unregistering \"/proc/net/ip_masq/%s\" entry\n",
			ent->name);
	proc_unregister(proc_net_ip_masq, ent->low_ino);
}


__initfunc(static void masq_proc_init(void))
{	
	IP_MASQ_DEBUG(1,"registering /proc/net/ip_masq\n");
	if (!proc_net_ip_masq) {
		struct proc_dir_entry *ent;
		ent = create_proc_entry("net/ip_masq", S_IFDIR, 0);
		if (ent) {
#ifdef MODULE
			ent->fill_inode = ip_masq_proc_count;
#endif
			proc_net_ip_masq = ent;
		 } else {
			 IP_MASQ_ERR("Could not create \"/proc/net/ip_masq\" entry\n");
		 }
	}
}
#endif	/* CONFIG_PROC_FS */
/*
 *	Wrapper over inet_select_addr()
 */
u32 ip_masq_select_addr(struct device *dev, u32 dst, int scope)
{
	return inet_select_addr(dev, dst, scope);
}

/*
 *	Initialize ip masquerading
 */
__initfunc(int ip_masq_init(void))
{
	int idx;
        for(idx = 0; idx < IP_MASQ_TAB_SIZE; idx++)  {
		INIT_LIST_HEAD(&ip_masq_s_table[idx]);
		INIT_LIST_HEAD(&ip_masq_m_table[idx]);
		INIT_LIST_HEAD(&ip_masq_d_table[idx]);
	}
#ifdef CONFIG_PROC_FS        
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPMSQHST, 13, "ip_masquerade",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_msqhst_procinfo
	});
	masq_proc_init();

	ip_masq_proc_register(&(struct proc_dir_entry) {
		0, 3, "tcp",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		NULL,	/* get_info */
		NULL,	/* fill_inode */
		NULL, NULL, NULL,
		(char *) IPPROTO_TCP,
		ip_masq_user_info
	});
	ip_masq_proc_register(&(struct proc_dir_entry) {
		0, 3, "udp",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		NULL,	/* get_info */
		NULL,	/* fill_inode */
		NULL, NULL, NULL,
		(char *) IPPROTO_UDP,
		ip_masq_user_info
	});
	ip_masq_proc_register(&(struct proc_dir_entry) {
		0, 4, "icmp",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		NULL,	/* get_info */
		NULL,	/* fill_inode */
		NULL, NULL, NULL,
		(char *) IPPROTO_ICMP,
		ip_masq_user_info
	});
#endif	
#ifdef CONFIG_IP_MASQUERADE_IPAUTOFW
	ip_autofw_init();
#endif
#ifdef CONFIG_IP_MASQUERADE_IPPORTFW
	ip_portfw_init();
#endif
#ifdef CONFIG_IP_MASQUERADE_MFW
	ip_mfw_init();
#endif
        ip_masq_app_init();

        return 0;
}
