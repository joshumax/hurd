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

#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>

#include <mach.h>
#include <hurd.h>

#include "bpf_impl.h"
#include "queue.h"
#include "util.h"

static struct net_hash_header filter_hash_header[N_NET_HASH];

/*
 * Execute the filter program starting at pc on the packet p
 * wirelen is the length of the original packet
 * buflen is the amount of data present
 *
 * @p: packet data.
 * @wirelen: data_count (in bytes)
 * @hlen: header len (in bytes)
 */

int
bpf_do_filter(net_rcv_port_t infp, char *p,	unsigned int wirelen,
		char *header, unsigned int hlen, net_hash_entry_t **hash_headpp,
		net_hash_entry_t *entpp)
{
	bpf_insn_t pc, pc_end;
	unsigned int buflen;

	unsigned long A, X;
	int k;
	unsigned int mem[BPF_MEMWORDS];

	/* Generic pointer to either HEADER or P according to the specified offset. */
	char *data = NULL;

	pc = ((bpf_insn_t) infp->filter) + 1;
	/* filter[0].code is (NETF_BPF | flags) */
	pc_end = (bpf_insn_t)infp->filter_end;
	buflen = NET_RCV_MAX;
	*entpp = 0;			/* default */

	A = 0;
	X = 0;
	for (; pc < pc_end; ++pc) {
		switch (pc->code) {

			default:
				abort();
			case BPF_RET|BPF_K:
				if (infp->rcv_port == MACH_PORT_NULL &&
						*entpp == 0) {
					return 0;
				}
				return ((u_int)pc->k <= wirelen) ?
					pc->k : wirelen;

			case BPF_RET|BPF_A:
				if (infp->rcv_port == MACH_PORT_NULL &&
						*entpp == 0) {
					return 0;
				}
				return ((u_int)A <= wirelen) ?
					A : wirelen;

			case BPF_RET|BPF_MATCH_IMM:
				if (bpf_match ((net_hash_header_t)infp, pc->jt, mem,
							hash_headpp, entpp)) {
					return ((u_int)pc->k <= wirelen) ?
						pc->k : wirelen;
				}
				return 0;

			case BPF_LD|BPF_W|BPF_ABS:
				k = pc->k;

load_word:
				if ((u_int)k + sizeof(long) <= hlen)
					data = header;
				else if ((u_int)k + sizeof(long) <= buflen) {
					k -= hlen;
					data = p;
				} else
					return 0;

#ifdef BPF_ALIGN
				if (((int)(data + k) & 3) != 0)
					A = EXTRACT_LONG(&data[k]);
				else
#endif
					A = ntohl(*(long *)(data + k));
				continue;

			case BPF_LD|BPF_H|BPF_ABS:
				k = pc->k;

load_half:
				if ((u_int)k + sizeof(short) <= hlen)
					data = header;
				else if ((u_int)k + sizeof(short) <= buflen) {
					k -= hlen;
					data = p;
				} else
					return 0;

				A = EXTRACT_SHORT(&data[k]);
				continue;

			case BPF_LD|BPF_B|BPF_ABS:
				k = pc->k;

load_byte:
				if ((u_int)k < hlen)
					data = header;
				else if ((u_int)k < buflen) {
					data = p;
					k -= hlen;
				} else
					return 0;

				A = data[k];
				continue;

			case BPF_LD|BPF_W|BPF_LEN:
				A = wirelen;
				continue;

			case BPF_LDX|BPF_W|BPF_LEN:
				X = wirelen;
				continue;

			case BPF_LD|BPF_W|BPF_IND:
				k = X + pc->k;
				goto load_word;

			case BPF_LD|BPF_H|BPF_IND:
				k = X + pc->k;
				goto load_half;

			case BPF_LD|BPF_B|BPF_IND:
				k = X + pc->k;
				goto load_byte;

			case BPF_LDX|BPF_MSH|BPF_B:
				k = pc->k;
				if (k < hlen)
					data = header;
				else if (k < buflen) {
					data = p;
					k -= hlen;
				} else
					return 0;

				X = (data[k] & 0xf) << 2;
				continue;

			case BPF_LD|BPF_IMM:
				A = pc->k;
				continue;

			case BPF_LDX|BPF_IMM:
				X = pc->k;
				continue;

			case BPF_LD|BPF_MEM:
				A = mem[pc->k];
				continue;

			case BPF_LDX|BPF_MEM:
				X = mem[pc->k];
				continue;

			case BPF_ST:
				mem[pc->k] = A;
				continue;

			case BPF_STX:
				mem[pc->k] = X;
				continue;

			case BPF_JMP|BPF_JA:
				pc += pc->k;
				continue;

			case BPF_JMP|BPF_JGT|BPF_K:
				pc += (A > pc->k) ? pc->jt : pc->jf;
				continue;

			case BPF_JMP|BPF_JGE|BPF_K:
				pc += (A >= pc->k) ? pc->jt : pc->jf;
				continue;

			case BPF_JMP|BPF_JEQ|BPF_K:
				pc += (A == pc->k) ? pc->jt : pc->jf;
				continue;

			case BPF_JMP|BPF_JSET|BPF_K:
				pc += (A & pc->k) ? pc->jt : pc->jf;
				continue;

			case BPF_JMP|BPF_JGT|BPF_X:
				pc += (A > X) ? pc->jt : pc->jf;
				continue;

			case BPF_JMP|BPF_JGE|BPF_X:
				pc += (A >= X) ? pc->jt : pc->jf;
				continue;

			case BPF_JMP|BPF_JEQ|BPF_X:
				pc += (A == X) ? pc->jt : pc->jf;
				continue;

			case BPF_JMP|BPF_JSET|BPF_X:
				pc += (A & X) ? pc->jt : pc->jf;
				continue;

			case BPF_ALU|BPF_ADD|BPF_X:
				A += X;
				continue;

			case BPF_ALU|BPF_SUB|BPF_X:
				A -= X;
				continue;

			case BPF_ALU|BPF_MUL|BPF_X:
				A *= X;
				continue;

			case BPF_ALU|BPF_DIV|BPF_X:
				if (X == 0)
					return 0;
				A /= X;
				continue;

			case BPF_ALU|BPF_AND|BPF_X:
				A &= X;
				continue;

			case BPF_ALU|BPF_OR|BPF_X:
				A |= X;
				continue;

			case BPF_ALU|BPF_LSH|BPF_X:
				A <<= X;
				continue;

			case BPF_ALU|BPF_RSH|BPF_X:
				A >>= X;
				continue;

			case BPF_ALU|BPF_ADD|BPF_K:
				A += pc->k;
				continue;

			case BPF_ALU|BPF_SUB|BPF_K:
				A -= pc->k;
				continue;

			case BPF_ALU|BPF_MUL|BPF_K:
				A *= pc->k;
				continue;

			case BPF_ALU|BPF_DIV|BPF_K:
				A /= pc->k;
				continue;

			case BPF_ALU|BPF_AND|BPF_K:
				A &= pc->k;
				continue;

			case BPF_ALU|BPF_OR|BPF_K:
				A |= pc->k;
				continue;

			case BPF_ALU|BPF_LSH|BPF_K:
				A <<= pc->k;
				continue;

			case BPF_ALU|BPF_RSH|BPF_K:
				A >>= pc->k;
				continue;

			case BPF_ALU|BPF_NEG:
				A = -A;
				continue;

			case BPF_MISC|BPF_TAX:
				X = A;
				continue;

			case BPF_MISC|BPF_TXA:
				A = X;
				continue;
		}
	}

	return 0;
}

/*
 * Return 1 if the 'f' is a valid filter program without a MATCH
 * instruction. Return 2 if it is a valid filter program with a MATCH
 * instruction. Otherwise, return 0.
 * The constraints are that each jump be forward and to a valid
 * code.  The code must terminate with either an accept or reject.
 * 'valid' is an array for use by the routine (it must be at least
 * 'len' bytes long).
 *
 * The kernel needs to be able to verify an application's filter code.
 * Otherwise, a bogus program could easily crash the system.
 */
int
bpf_validate(bpf_insn_t f, int bytes, bpf_insn_t *match)
{
	int i, j, len;
	bpf_insn_t p;

	len = BPF_BYTES2LEN(bytes);

	/*
	 * f[0].code is already checked to be (NETF_BPF | flags).
	 * So skip f[0].
	 */

	for (i = 1; i < len; ++i) {
		/*
		 * Check that that jumps are forward, and within
		 * the code block.
		 */
		p = &f[i];
		if (BPF_CLASS(p->code) == BPF_JMP) {
			int from = i + 1;

			if (BPF_OP(p->code) == BPF_JA) {
				if (from + p->k >= len)
					return 0;
			}
			else if (from + p->jt >= len || from + p->jf >= len)
				return 0;
		}
		/*
		 * Check that memory operations use valid addresses.
		 */
		if ((BPF_CLASS(p->code) == BPF_ST ||
					(BPF_CLASS(p->code) == BPF_LD &&
					 (p->code & 0xe0) == BPF_MEM)) &&
				(p->k >= BPF_MEMWORDS || p->k < 0)) {
			return 0;
		}
		/*
		 * Check for constant division by 0.
		 */
		if (p->code == (BPF_ALU|BPF_DIV|BPF_K) && p->k == 0) {
			return 0;
		}
		/*
		 * Check for match instruction.
		 * Only one match instruction per filter is allowed.
		 */
		if (p->code == (BPF_RET|BPF_MATCH_IMM)) {
			if (*match != 0 ||
					p->jt == 0 ||
					p->jt > N_NET_HASH_KEYS)
				return 0;
			i += p->jt;		/* skip keys */
			if (i + 1 > len)
				return 0;

			for (j = 1; j <= p->jt; j++) {
				if (p[j].code != (BPF_MISC|BPF_KEY))
					return 0;
			}

			*match = p;
		}
	}
	if (BPF_CLASS(f[len - 1].code) == BPF_RET)
		return ((*match == 0) ? 1 : 2);
	else
		return 0;
}

int
bpf_eq (bpf_insn_t f1, bpf_insn_t f2, int bytes)
{
	int count;

	count = BPF_BYTES2LEN(bytes);
	for (; count--; f1++, f2++) {
		if (!BPF_INSN_EQ(f1, f2)) {
			if ( f1->code == (BPF_MISC|BPF_KEY) &&
					f2->code == (BPF_MISC|BPF_KEY) )
				continue;
			return FALSE;
		}
	};
	return TRUE;
}

unsigned int
bpf_hash (int n, unsigned int *keys)
{
	unsigned int hval = 0;

	while (n--) {
		hval += *keys++;
	}
	return (hval % NET_HASH_SIZE);
}


int
bpf_match (net_hash_header_t hash, int n_keys, unsigned int *keys,
	net_hash_entry_t **hash_headpp, net_hash_entry_t *entpp)
{
	net_hash_entry_t head, entp;
	int i;

	if (n_keys != hash->n_keys)
		return FALSE;

	*hash_headpp = &hash->table[bpf_hash(n_keys, keys)];
	head = **hash_headpp;

	if (head == 0)
		return FALSE;

	HASH_ITERATE (head, entp)
	{
		for (i = 0; i < n_keys; i++) {
			if (keys[i] != entp->keys[i])
				break;
		}
		if (i == n_keys) {
			*entpp = entp;
			return TRUE;
		}
	}
	HASH_ITERATE_END (head, entp)
		return FALSE;
}

/*
 * Removes a hash entry (ENTP) from its queue (HEAD).
 * If the reference count of filter (HP) becomes zero and not USED,
 * HP is removed from the corresponding port lists and is freed.
 */

int
hash_ent_remove (if_filter_list_t *ifp, net_hash_header_t hp, int used,
		net_hash_entry_t *head, net_hash_entry_t entp, queue_entry_t *dead_p)
{
	hp->ref_count--;

	if (*head == entp) {
		if (queue_empty((queue_t) entp)) {
			*head = 0;
			ENQUEUE_DEAD(*dead_p, entp, chain);
			if (hp->ref_count == 0 && !used) {
				if (((net_rcv_port_t)hp)->filter[0] & NETF_IN)
					queue_remove(&ifp->if_rcv_port_list,
							(net_rcv_port_t)hp,
							net_rcv_port_t, input);
				if (((net_rcv_port_t)hp)->filter[0] & NETF_OUT)
					queue_remove(&ifp->if_snd_port_list,
							(net_rcv_port_t)hp,
							net_rcv_port_t, output);
				hp->n_keys = 0;
				return TRUE;
			}
			return FALSE;
		} else {
			*head = (net_hash_entry_t)queue_next((queue_t) entp);
		}
	}

	remqueue((queue_t)*head, (queue_entry_t)entp);
	ENQUEUE_DEAD(*dead_p, entp, chain);
	return FALSE;
}

/*
 * net_free_dead_infp (dead_infp)
 *	queue_entry_t dead_infp;	list of dead net_rcv_port_t.
 *
 * Deallocates dead net_rcv_port_t.
 * No locks should be held when called.
 */
void
net_free_dead_infp (queue_entry_t dead_infp)
{
	net_rcv_port_t infp, nextfp;

	for (infp = (net_rcv_port_t) dead_infp; infp != 0; infp = nextfp) {
		nextfp = (net_rcv_port_t) queue_next(&infp->input);
		mach_port_deallocate(mach_task_self(), infp->rcv_port);
		free(infp);
		debug ("a dead infp is freed\n");
	}
}

/*
 * net_free_dead_entp (dead_entp)
 *	queue_entry_t dead_entp;	list of dead net_hash_entry_t.
 *
 * Deallocates dead net_hash_entry_t.
 * No locks should be held when called.
 */
void
net_free_dead_entp (queue_entry_t dead_entp)
{
	net_hash_entry_t entp, nextentp;

	for (entp = (net_hash_entry_t)dead_entp; entp != 0; entp = nextentp) {
		nextentp = (net_hash_entry_t) queue_next(&entp->chain);

		mach_port_deallocate(mach_task_self(), entp->rcv_port);
		free(entp);
		debug ("a dead entp is freed\n");
	}
}

/*
 * Set a filter for a network interface.
 *
 * We are given a naked send right for the rcv_port.
 * If we are successful, we must consume that right.
 */
io_return_t
net_set_filter(if_filter_list_t *ifp, mach_port_t rcv_port, int priority,
		filter_t *filter, unsigned int filter_count)
{
	int               filter_bytes;
	bpf_insn_t            match;
	net_rcv_port_t   infp, my_infp;
	net_rcv_port_t        nextfp;
	net_hash_header_t     hhp;
	net_hash_entry_t entp, hash_entp=NULL;
	net_hash_entry_t      *head, nextentp;
	queue_entry_t     dead_infp, dead_entp;
	int               i;
	int               ret, is_new_infp;
	io_return_t           rval;
	boolean_t         in, out;

	/* Check the filter syntax. */

	debug ("filter_count: %d, filter[0]: %d\n", filter_count, filter[0]);

	filter_bytes = CSPF_BYTES (filter_count);
	match = (bpf_insn_t) 0;

	if (filter_count == 0) {
		return (D_INVALID_OPERATION);
	} else if (!((filter[0] & NETF_IN) || (filter[0] & NETF_OUT))) {
		return (D_INVALID_OPERATION); /* NETF_IN or NETF_OUT required */
	} else if ((filter[0] & NETF_TYPE_MASK) == NETF_BPF) {
		ret = bpf_validate((bpf_insn_t)filter, filter_bytes, &match);
		if (!ret)
			return (D_INVALID_OPERATION);
	} else {
		return (D_INVALID_OPERATION);
	}
	debug ("net_set_filter: check over\n");

	rval = D_SUCCESS;         /* default return value */
	dead_infp = dead_entp = 0;

	if (match == (bpf_insn_t) 0) {
		/*
		 * If there is no match instruction, we allocate
		 * a normal packet filter structure.
		 */
		my_infp = (net_rcv_port_t) calloc(1, sizeof(struct net_rcv_port));
		my_infp->rcv_port = rcv_port;
		is_new_infp = TRUE;
	} else {
		/*
		 * If there is a match instruction, we assume there will be
		 * multiple sessions with a common substructure and allocate
		 * a hash table to deal with them.
		 */
		my_infp = 0;
		hash_entp = (net_hash_entry_t) calloc(1, sizeof(struct net_hash_entry));
		is_new_infp = FALSE;
	}

	/*
	 * Look for an existing filter on the same reply port.
	 * Look for filters with dead ports (for GC).
	 * Look for a filter with the same code except KEY insns.
	 */
	void check_filter_list(queue_head_t *if_port_list)
	{
		FILTER_ITERATE(if_port_list, infp, nextfp,
				(if_port_list == &ifp->if_rcv_port_list)
				? &infp->input : &infp->output)
		{
			if (infp->rcv_port == MACH_PORT_NULL) {
				if (match != 0
						&& infp->priority == priority
						&& my_infp == 0
						&& (infp->filter_end - infp->filter) == filter_count
						&& bpf_eq((bpf_insn_t)infp->filter,
							(bpf_insn_t)filter, filter_bytes))
					my_infp = infp;

				for (i = 0; i < NET_HASH_SIZE; i++) {
					head = &((net_hash_header_t) infp)->table[i];
					if (*head == 0)
						continue;

					/*
					 * Check each hash entry to make sure the
					 * destination port is still valid.  Remove
					 * any invalid entries.
					 */
					entp = *head;
					do {
						nextentp = (net_hash_entry_t) entp->he_next;

						/* checked without
						   ip_lock(entp->rcv_port) */
						if (entp->rcv_port == rcv_port) {
							ret = hash_ent_remove (ifp,
									(net_hash_header_t)infp,
									(my_infp == infp),
									head,
									entp,
									&dead_entp);
							if (ret)
								goto hash_loop_end;
						}

						entp = nextentp;
						/* While test checks head since hash_ent_remove
						 * might modify it.
						 */
					} while (*head != 0 && entp != *head);
				}

hash_loop_end:
				;
			} else if (infp->rcv_port == rcv_port) {

				/* Remove the old filter from lists */
				if (infp->filter[0] & NETF_IN)
					queue_remove(&ifp->if_rcv_port_list, infp,
							net_rcv_port_t, input);
				if (infp->filter[0] & NETF_OUT)
					queue_remove(&ifp->if_snd_port_list, infp,
							net_rcv_port_t, output);

				ENQUEUE_DEAD(dead_infp, infp, input);
			}
		}
		FILTER_ITERATE_END
	}

	in = (filter[0] & NETF_IN) != 0;
	out = (filter[0] & NETF_OUT) != 0;

	if (in)
		check_filter_list(&ifp->if_rcv_port_list);
	if (out)
		check_filter_list(&ifp->if_snd_port_list);

	if (my_infp == 0) {
		/* Allocate a dummy infp */
		for (i = 0; i < N_NET_HASH; i++) {
			if (filter_hash_header[i].n_keys == 0)
				break;
		}
		if (i == N_NET_HASH) {
			mach_port_deallocate(mach_task_self() , rcv_port);
			if (match != 0)
				free(hash_entp);

			rval = D_NO_MEMORY;
			goto clean_and_return;
		}

		hhp = &filter_hash_header[i];
		hhp->n_keys = match->jt;

		hhp->ref_count = 0;
		for (i = 0; i < NET_HASH_SIZE; i++)
			hhp->table[i] = 0;

		my_infp = (net_rcv_port_t)hhp;
		my_infp->rcv_port = MACH_PORT_NULL; /* indication of dummy */
		is_new_infp = TRUE;
	}

	if (is_new_infp) {
		my_infp->priority = priority;
		my_infp->rcv_count = 0;

		/* Copy filter program. */
		memcpy (my_infp->filter, filter, filter_bytes);
		my_infp->filter_end =
			(filter_t *)((char *)my_infp->filter + filter_bytes);

		/* Insert my_infp according to priority */
		if (in) {
			queue_iterate(&ifp->if_rcv_port_list, infp, net_rcv_port_t, input)
				if (priority > infp->priority)
					break;

			queue_enter(&ifp->if_rcv_port_list, my_infp, net_rcv_port_t, input);
		}

		if (out) {
			queue_iterate(&ifp->if_snd_port_list, infp, net_rcv_port_t, output)
				if (priority > infp->priority)
					break;

			queue_enter(&ifp->if_snd_port_list, my_infp, net_rcv_port_t, output);
		}
	}

	if (match != 0)
	{
		/* Insert to hash list */
		net_hash_entry_t *p;

		hash_entp->rcv_port = rcv_port;
		for (i = 0; i < match->jt; i++)     /* match->jt is n_keys */
			hash_entp->keys[i] = match[i+1].k;
		p = &((net_hash_header_t)my_infp)->
			table[bpf_hash(match->jt, hash_entp->keys)];

		/* Not checking for the same key values */
		if (*p == 0) {
			queue_init ((queue_t) hash_entp);
			*p = hash_entp;
		} else {
			enqueue_tail((queue_t)*p, (queue_entry_t)hash_entp);
		}

		((net_hash_header_t)my_infp)->ref_count++;
	}

clean_and_return:
	/* No locks are held at this point. */

	if (dead_infp != 0)
		net_free_dead_infp(dead_infp);
	if (dead_entp != 0)
		net_free_dead_entp(dead_entp);

	return (rval);
}

void
destroy_filters (if_filter_list_t *ifp)
{
}

void
remove_dead_filter (if_filter_list_t *ifp, queue_head_t *if_port_list,
		mach_port_t dead_port)
{
	net_rcv_port_t infp;
	net_rcv_port_t nextfp;
	net_hash_entry_t *head, nextentp;
	queue_entry_t dead_infp, dead_entp;
	net_hash_entry_t entp = NULL;
	int i, ret;

	dead_infp = dead_entp = 0;
	FILTER_ITERATE (if_port_list, infp, nextfp,
			(if_port_list == &ifp->if_rcv_port_list)
			? &infp->input : &infp->output) {
		if (infp->rcv_port == MACH_PORT_NULL) {
			for (i = 0; i < NET_HASH_SIZE; i++) {
				head = &((net_hash_header_t) infp)->table[i];
				if (*head == 0)
					continue;

				/*
				 * Check each hash entry to make sure the
				 * destination port is still valid.  Remove
				 * any invalid entries.
				 */
				entp = *head;
				do {
					nextentp = (net_hash_entry_t) entp->he_next;

					/* checked without
					   ip_lock(entp->rcv_port) */
					if (entp->rcv_port == dead_port) {
						ret = hash_ent_remove (ifp,
								(net_hash_header_t) infp,
								0,
								head,
								entp,
								&dead_entp);
						if (ret)
							goto hash_loop_end;
					}

					entp = nextentp;
					/* While test checks head since hash_ent_remove
					 * might modify it.
					 */
				} while (*head != 0 && entp != *head);
			}

hash_loop_end:
			;
		} else if (infp->rcv_port == dead_port) {
			/* Remove the old filter from lists */
			if (infp->filter[0] & NETF_IN)
				queue_remove(&ifp->if_rcv_port_list, infp,
						net_rcv_port_t, input);
			if (infp->filter[0] & NETF_OUT)
				queue_remove(&ifp->if_snd_port_list, infp,
						net_rcv_port_t, output);

			ENQUEUE_DEAD(dead_infp, infp, input);
		}
	}
	FILTER_ITERATE_END

	if (dead_infp != 0)
		net_free_dead_infp(dead_infp);
	if (dead_entp != 0)
		net_free_dead_entp(dead_entp);
}
