/*
 *	IP_MASQ user space control interface
 *	$Id: ip_masq.h,v 1.2 1998/12/08 05:41:48 davem Exp $
 */

#ifndef _LINUX_IP_MASQ_H
#define _LINUX_IP_MASQ_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/stddef.h>
#else
#include <sys/types.h>
#include <stddef.h>
#endif

struct ip_masq_user {
	int 	protocol;
	u_int16_t	sport, dport, mport;
	u_int32_t 	saddr, daddr, maddr;
	u_int32_t	rt_daddr;	/* dst address to use for rt query */
	u_int32_t	rt_saddr;	
	u_int32_t	ip_tos;		/* TOS */
	unsigned timeout;		/* in ticks (HZ per sec) */
	unsigned flags;
	int fd;				/* NOT IMPL: attach tunnel to this fd */
	int state;			/* NOT IMPL: return conn state */
};

#define IP_MASQ_USER_F_LISTEN	0x01	/* put entry to LISTEN state */
#define IP_MASQ_USER_F_DEAD	0x02	/* mark as DEAD */
#define IP_MASQ_USER_F_FORCE	0x04	/* force operation */

struct ip_masq_timeout {
	int protocol;
	union {
		struct {
			unsigned established;
			unsigned syn_sent;
			unsigned syn_recv;
			unsigned fin_wait;
			unsigned time_wait;
			unsigned close;
			unsigned close_wait;
			unsigned last_ack;
			unsigned listen;
		} tcp;
		unsigned udp;
		unsigned icmp;
	} u;
};

/* 
 *	AUTOFW stuff 
 */
#define IP_FWD_RANGE 		1
#define IP_FWD_PORT		2
#define IP_FWD_DIRECT		3

#define IP_AUTOFW_ACTIVE	1
#define IP_AUTOFW_USETIME	2
#define IP_AUTOFW_SECURE	4


/* WARNING: bitwise equal to ip_autofw  in net/ip_autofw.h */
struct ip_autofw_user {
	void * next;
	u_int16_t type;
	u_int16_t low;
	u_int16_t hidden;
	u_int16_t high;
	u_int16_t visible;
	u_int16_t protocol;
	u_int32_t lastcontact;
	u_int32_t where;
	u_int16_t ctlproto;
	u_int16_t ctlport;
	u_int16_t flags;
	/* struct timer_list timer; */
};

/* 
 *	PORTFW stuff 
 */
struct ip_portfw_user {
	u_int16_t	protocol;       /* Which protocol are we talking? */
	u_int32_t	laddr, raddr;   /* Remote address */
	u_int16_t	lport, rport;   /* Local and remote port */
	int 		pref;		/* Preference value */
};

/* 
 *	MFW stuff 
 */
struct ip_mfw_user {
	u_int32_t           fwmark;	/* Firewalling mark */
	u_int32_t           raddr;  	/* remote port */
	u_int16_t           rport;	/* remote port */
	u_int16_t           dummy;          /* Make up to multiple of 4 */
	int 		pref;		/* Preference value */
	unsigned 	flags;		/* misc flags */
};

#define IP_MASQ_MFW_SCHED	0x01

#define IP_FW_MASQCTL_MAX 256
#define IP_MASQ_TNAME_MAX  32

struct ip_masq_ctl {
	int m_target;
	int m_cmd;
	char m_tname[IP_MASQ_TNAME_MAX];
	union {
		struct ip_portfw_user portfw_user;
		struct ip_autofw_user autofw_user;
		struct ip_mfw_user mfw_user;
		struct ip_masq_user user;
		unsigned char m_raw[IP_FW_MASQCTL_MAX];
	} u;
};

#define IP_MASQ_CTL_BSIZE (offsetof (struct ip_masq_ctl,u))

#define IP_MASQ_TARGET_CORE	1
#define IP_MASQ_TARGET_MOD	2	/* masq_mod is selected by "name" */
#define IP_MASQ_TARGET_USER	3	
#define IP_MASQ_TARGET_LAST	4

#define IP_MASQ_CMD_NONE	0	/* just peek */
#define IP_MASQ_CMD_INSERT	1
#define IP_MASQ_CMD_ADD		2
#define IP_MASQ_CMD_SET		3
#define IP_MASQ_CMD_DEL		4
#define IP_MASQ_CMD_GET		5
#define IP_MASQ_CMD_FLUSH	6
#define IP_MASQ_CMD_LIST	7	/* actually fake: done via /proc */
#define IP_MASQ_CMD_ENABLE	8
#define IP_MASQ_CMD_DISABLE	9

#endif /* _LINUX_IP_MASQ_H */
