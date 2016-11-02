 /*
  * Mach Operating System
  * Copyright (c) 1993-1989 Carnegie Mellon University
  * All Rights Reserved.
  *
  * Permission to use, copy, modify and distribute this software and its
  * documentation is hereby granted, provided that both the copyright
  * notice and this permission notice appear in all copies of the
  * software, derivative works or modified versions, and any portions
  * thereof, and that both notices appear in supporting documentation.
  *
  * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
  * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
  * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
  *
  * Carnegie Mellon requests users of this software to return to
  *
  *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
  *  School of Computer Science
  *  Carnegie Mellon University
  *  Pittsburgh PA 15213-3890
  *
  * any improvements or extensions that they make and grant Carnegie Mellon
  * the rights to redistribute these changes.
  */
/*
 *	Author: David B. Golub, Carnegie Mellon University
 *	Date:	3/98
 *
 *	Network IO.
 *
 *	Packet filter code taken from vaxif/enet.c written
 *		CMU and Stanford.
 */

/* the code copied from device/net_io.c in Mach */

#ifndef BPF_IMPL_H
#define BPF_IMPL_H

#include <device/bpf.h>

#include "queue.h"

typedef struct
{
  queue_head_t if_rcv_port_list;	/* input filter list */
  queue_head_t if_snd_port_list;	/* output filter list */
}if_filter_list_t;

typedef	unsigned short	filter_t;
typedef filter_t	*filter_array_t;

#define	NET_MAX_FILTER		128 /* was 64, bpf programs are big */

#define NET_HASH_SIZE   256
#define N_NET_HASH      4
#define N_NET_HASH_KEYS 4

#ifndef BPF_ALIGN
#define EXTRACT_SHORT(p)	((u_short)ntohs(*(u_short *)p))
#define EXTRACT_LONG(p)		(ntohl(*(u_long *)p))
#else
#define EXTRACT_SHORT(p)\
	((u_short)\
	 ((u_short)*((u_char *)p+0)<<8|\
	  (u_short)*((u_char *)p+1)<<0))
#define EXTRACT_LONG(p)\
	((u_long)*((u_char *)p+0)<<24|\
	 (u_long)*((u_char *)p+1)<<16|\
	 (u_long)*((u_char *)p+2)<<8|\
	 (u_long)*((u_char *)p+3)<<0)
#endif

#define HASH_ITERATE(head, elt) (elt) = (net_hash_entry_t) (head); do {
#define HASH_ITERATE_END(head, elt) \
	(elt) = (net_hash_entry_t) queue_next((queue_entry_t) (elt));	   \
} while ((elt) != (head));

#define FILTER_ITERATE(if_port_list, fp, nextfp, chain)	\
	for ((fp) = (net_rcv_port_t) queue_first(if_port_list);	\
			!queue_end(if_port_list, (queue_entry_t)(fp));	\
			(fp) = (nextfp)) {					\
		(nextfp) = (net_rcv_port_t) queue_next(chain);
#define FILTER_ITERATE_END }

/* entry_p must be net_rcv_port_t or net_hash_entry_t */
#define ENQUEUE_DEAD(dead, entry_p, chain) {			\
	queue_next(&(entry_p)->chain) = (queue_entry_t) (dead);	\
	(dead) = (queue_entry_t)(entry_p);			\
}

#define CSPF_BYTES(n) ((n) * sizeof (filter_t))

/*
 * Receive port for net, with packet filter.
 * This data structure by itself represents a packet
 * filter for a single session.
 */
struct net_rcv_port {
	queue_chain_t	input;		/* list of input open_descriptors */
	queue_chain_t	output;		/* list of output open_descriptors */
	mach_port_t	rcv_port;	/* port to send packet to */
	int		rcv_count;	/* number of packets received */
	int		priority;	/* priority for filter */
	filter_t	*filter_end;	/* pointer to end of filter */
	filter_t	filter[NET_MAX_FILTER];
	/* filter operations */
};
typedef struct net_rcv_port *net_rcv_port_t;

/*
 * A single hash entry.
 */
struct net_hash_entry {
	queue_chain_t   chain;	        /* list of entries with same hval */
#define he_next chain.next
#define he_prev chain.prev
	mach_port_t      rcv_port;	/* destination port */
	unsigned int	keys[N_NET_HASH_KEYS];
};
typedef struct net_hash_entry *net_hash_entry_t;

/*
 * This structure represents a packet filter with multiple sessions.
 *
 * For example, all application level TCP sessions might be
 * represented by one of these structures.  It looks like a
 * net_rcv_port struct so that both types can live on the
 * same packet filter queues.
 */
struct net_hash_header {
	struct net_rcv_port rcv;
	int n_keys;			/* zero if not used */
	int ref_count;			/* reference count */
	net_hash_entry_t table[NET_HASH_SIZE];
};

typedef struct net_hash_header *net_hash_header_t;

int bpf_do_filter(net_rcv_port_t infp, char *p,	unsigned int wirelen,
		char *header, unsigned int hlen, net_hash_entry_t **hash_headpp,
		net_hash_entry_t *entpp);
io_return_t net_set_filter(if_filter_list_t *ifp, mach_port_t rcv_port,
		int priority, filter_t *filter, unsigned int filter_count);

int bpf_validate(bpf_insn_t f, int bytes, bpf_insn_t *match);
int bpf_eq (bpf_insn_t f1, bpf_insn_t f2, int bytes);
unsigned int bpf_hash (int n, unsigned int *keys);
int bpf_match (net_hash_header_t hash, int n_keys, unsigned int *keys,
	net_hash_entry_t **hash_headpp, net_hash_entry_t *entpp);

int hash_ent_remove (if_filter_list_t *ifp, net_hash_header_t hp, int used,
		net_hash_entry_t *head, net_hash_entry_t entp, queue_entry_t *dead_p);
void net_free_dead_infp (queue_entry_t dead_infp);
void net_free_dead_entp (queue_entry_t dead_entp);
void remove_dead_filter (if_filter_list_t *ifp,
		queue_head_t *if_port_list, mach_port_t dead_port);
void destroy_filters (if_filter_list_t *ifp);

#endif /* _DEVICE_BPF_H_ */
