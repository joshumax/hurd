#ifndef __LINUX_NET_AFUNIX_H
#define __LINUX_NET_AFUNIX_H
extern void unix_proto_init(struct net_proto *pro);
extern struct proto_ops unix_proto_ops;
extern void unix_inflight(struct file *fp);
extern void unix_notinflight(struct file *fp);
typedef struct sock unix_socket;
extern void unix_gc(void);

#define UNIX_HASH_SIZE	16

extern unix_socket *unix_socket_table[UNIX_HASH_SIZE+1];

#define forall_unix_sockets(i, s) for (i=0; i<=UNIX_HASH_SIZE; i++) \
                                    for (s=unix_socket_table[i]; s; s=s->next)

struct unix_address
{
	atomic_t	refcnt;
	int		len;
	unsigned	hash;
	struct sockaddr_un name[0];
};

struct unix_skb_parms
{
	struct ucred		creds;		/* Skb credentials	*/
	struct scm_fp_list	*fp;		/* Passed files		*/
	unsigned		attr;		/* Special attributes	*/
};

#define UNIXCB(skb) 	(*(struct unix_skb_parms*)&((skb)->cb))
#define UNIXCREDS(skb)	(&UNIXCB((skb)).creds)

#endif
