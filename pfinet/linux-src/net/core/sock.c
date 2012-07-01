/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Generic socket support routines. Memory allocators, socket lock/release
 *		handler for protocols to use and generic option handler.
 *
 *
 * Version:	$Id: sock.c,v 1.80 1999/05/08 03:04:34 davem Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *
 * Fixes:
 *		Alan Cox	: 	Numerous verify_area() problems
 *		Alan Cox	:	Connecting on a connecting socket
 *					now returns an error for tcp.
 *		Alan Cox	:	sock->protocol is set correctly.
 *					and is not sometimes left as 0.
 *		Alan Cox	:	connect handles icmp errors on a
 *					connect properly. Unfortunately there
 *					is a restart syscall nasty there. I
 *					can't match BSD without hacking the C
 *					library. Ideas urgently sought!
 *		Alan Cox	:	Disallow bind() to addresses that are
 *					not ours - especially broadcast ones!!
 *		Alan Cox	:	Socket 1024 _IS_ ok for users. (fencepost)
 *		Alan Cox	:	sock_wfree/sock_rfree don't destroy sockets,
 *					instead they leave that for the DESTROY timer.
 *		Alan Cox	:	Clean up error flag in accept
 *		Alan Cox	:	TCP ack handling is buggy, the DESTROY timer
 *					was buggy. Put a remove_sock() in the handler
 *					for memory when we hit 0. Also altered the timer
 *					code. The ACK stuff can wait and needs major 
 *					TCP layer surgery.
 *		Alan Cox	:	Fixed TCP ack bug, removed remove sock
 *					and fixed timer/inet_bh race.
 *		Alan Cox	:	Added zapped flag for TCP
 *		Alan Cox	:	Move kfree_skb into skbuff.c and tidied up surplus code
 *		Alan Cox	:	for new sk_buff allocations wmalloc/rmalloc now call alloc_skb
 *		Alan Cox	:	kfree_s calls now are kfree_skbmem so we can track skb resources
 *		Alan Cox	:	Supports socket option broadcast now as does udp. Packet and raw need fixing.
 *		Alan Cox	:	Added RCVBUF,SNDBUF size setting. It suddenly occurred to me how easy it was so...
 *		Rick Sladkey	:	Relaxed UDP rules for matching packets.
 *		C.E.Hawkins	:	IFF_PROMISC/SIOCGHWADDR support
 *	Pauline Middelink	:	identd support
 *		Alan Cox	:	Fixed connect() taking signals I think.
 *		Alan Cox	:	SO_LINGER supported
 *		Alan Cox	:	Error reporting fixes
 *		Anonymous	:	inet_create tidied up (sk->reuse setting)
 *		Alan Cox	:	inet sockets don't set sk->type!
 *		Alan Cox	:	Split socket option code
 *		Alan Cox	:	Callbacks
 *		Alan Cox	:	Nagle flag for Charles & Johannes stuff
 *		Alex		:	Removed restriction on inet fioctl
 *		Alan Cox	:	Splitting INET from NET core
 *		Alan Cox	:	Fixed bogus SO_TYPE handling in getsockopt()
 *		Adam Caldwell	:	Missing return in SO_DONTROUTE/SO_DEBUG code
 *		Alan Cox	:	Split IP from generic code
 *		Alan Cox	:	New kfree_skbmem()
 *		Alan Cox	:	Make SO_DEBUG superuser only.
 *		Alan Cox	:	Allow anyone to clear SO_DEBUG
 *					(compatibility fix)
 *		Alan Cox	:	Added optimistic memory grabbing for AF_UNIX throughput.
 *		Alan Cox	:	Allocator for a socket is settable.
 *		Alan Cox	:	SO_ERROR includes soft errors.
 *		Alan Cox	:	Allow NULL arguments on some SO_ opts
 *		Alan Cox	: 	Generic socket allocation to make hooks
 *					easier (suggested by Craig Metz).
 *		Michael Pall	:	SO_ERROR returns positive errno again
 *              Steve Whitehouse:       Added default destructor to free
 *                                      protocol private data.
 *              Steve Whitehouse:       Added various other default routines
 *                                      common to several socket families.
 *              Chris Evans     :       Call suser() check last on F_SETOWN
 *		Jay Schulist	:	Added SO_ATTACH_FILTER and SO_DETACH_FILTER.
 *		Andi Kleen	:	Add sock_kmalloc()/sock_kfree_s()
 *		Andi Kleen	:	Fix write_space callback
 *
 * To Fix:
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/poll.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/rarp.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>
#include <net/icmp.h>
#include <linux/ipsec.h>

#ifdef CONFIG_FILTER
#include <linux/filter.h>
#endif

#define min(a,b)	((a)<(b)?(a):(b))

/* Run time adjustable parameters. */
__u32 sysctl_wmem_max = SK_WMEM_MAX;
__u32 sysctl_rmem_max = SK_RMEM_MAX;
__u32 sysctl_wmem_default = SK_WMEM_MAX;
__u32 sysctl_rmem_default = SK_RMEM_MAX;

/* Maximal space eaten by iovec or ancillary data plus some space */
int sysctl_optmem_max = sizeof(unsigned long)*(2*UIO_MAXIOV + 512);

/*
 *	This is meant for all protocols to use and covers goings on
 *	at the socket level. Everything here is generic.
 */

int sock_setsockopt(struct socket *sock, int level, int optname,
		    char *optval, int optlen)
{
	struct sock *sk=sock->sk;
#ifdef CONFIG_FILTER
	struct sk_filter *filter;
#endif
	int val;
	int valbool;
	int err;
	struct linger ling;
	int ret = 0;
	
	/*
	 *	Options without arguments
	 */

#ifdef SO_DONTLINGER		/* Compatibility item... */
	switch(optname)
	{
		case SO_DONTLINGER:
			sk->linger=0;
			return 0;
	}
#endif	
		
  	if(optlen<sizeof(int))
  		return(-EINVAL);
  	
	err = get_user(val, (int *)optval);
	if (err)
		return err;
	
  	valbool = val?1:0;
  	
  	switch(optname) 
  	{
		case SO_DEBUG:	
			if(val && !capable(CAP_NET_ADMIN))
			{
				ret = -EACCES;
			}
			else
				sk->debug=valbool;
			break;
		case SO_REUSEADDR:
			sk->reuse = valbool;
			break;
		case SO_TYPE:
		case SO_ERROR:
			ret = -ENOPROTOOPT;
		  	break;
		case SO_DONTROUTE:
			sk->localroute=valbool;
			break;
		case SO_BROADCAST:
			sk->broadcast=valbool;
			break;
		case SO_SNDBUF:
			/* Don't error on this BSD doesn't and if you think
			   about it this is right. Otherwise apps have to
			   play 'guess the biggest size' games. RCVBUF/SNDBUF
			   are treated in BSD as hints */
			   
			if (val > sysctl_wmem_max)
				val = sysctl_wmem_max;

			sk->sndbuf = max(val*2,2048);

			/*
			 *	Wake up sending tasks if we
			 *	upped the value.
			 */
			sk->write_space(sk);
			break;

		case SO_RCVBUF:
			/* Don't error on this BSD doesn't and if you think
			   about it this is right. Otherwise apps have to
			   play 'guess the biggest size' games. RCVBUF/SNDBUF
			   are treated in BSD as hints */
			  
			if (val > sysctl_rmem_max)
				val = sysctl_rmem_max;

			/* FIXME: is this lower bound the right one? */
			sk->rcvbuf = max(val*2,256);
			break;

		case SO_KEEPALIVE:
#ifdef CONFIG_INET
			if (sk->protocol == IPPROTO_TCP)
			{
				tcp_set_keepalive(sk, valbool);
			}
#endif
			sk->keepopen = valbool;
			break;

	 	case SO_OOBINLINE:
			sk->urginline = valbool;
			break;

	 	case SO_NO_CHECK:
			sk->no_check = valbool;
			break;

		case SO_PRIORITY:
			if ((val >= 0 && val <= 6) || capable(CAP_NET_ADMIN)) 
				sk->priority = val;
			else
				return(-EPERM);
			break;

		case SO_LINGER:
			if(optlen<sizeof(ling))
				return -EINVAL;	/* 1003.1g */
			err = copy_from_user(&ling,optval,sizeof(ling));
			if (err)
			{
				ret = -EFAULT;
				break;
			}
			if(ling.l_onoff==0)
				sk->linger=0;
			else
			{
				sk->lingertime=ling.l_linger;
				sk->linger=1;
			}
			break;

		case SO_BSDCOMPAT:
			sk->bsdism = valbool;
			break;

		case SO_PASSCRED:
			sock->passcred = valbool;
			break;
			
			
#ifdef CONFIG_NETDEVICES
		case SO_BINDTODEVICE:
		{
			char devname[IFNAMSIZ]; 

			/* Sorry... */ 
			if (!capable(CAP_NET_RAW)) 
				return -EPERM; 

			/* Bind this socket to a particular device like "eth0",
			 * as specified in the passed interface name. If the
			 * name is "" or the option length is zero the socket 
			 * is not bound. 
			 */ 

			if (!valbool) {
				sk->bound_dev_if = 0;
			} else {
				if (optlen > IFNAMSIZ) 
					optlen = IFNAMSIZ; 
				if (copy_from_user(devname, optval, optlen))
					return -EFAULT;

				/* Remove any cached route for this socket. */
				lock_sock(sk);
				dst_release(xchg(&sk->dst_cache, NULL));
				release_sock(sk);

				if (devname[0] == '\0') {
					sk->bound_dev_if = 0;
				} else {
					struct device *dev = dev_get(devname);
					if (!dev)
						return -EINVAL;
					sk->bound_dev_if = dev->ifindex;
				}
				return 0;
			}
		}
#endif


#ifdef CONFIG_FILTER
		case SO_ATTACH_FILTER:
			ret = -EINVAL;
			if (optlen == sizeof(struct sock_fprog)) {
				struct sock_fprog fprog;

				ret = -EFAULT;
				if (copy_from_user(&fprog, optval, sizeof(fprog)))
					break;

				ret = sk_attach_filter(&fprog, sk);
			}
			break;

		case SO_DETACH_FILTER:
			filter = sk->filter;
                        if(filter) {
				sk->filter = NULL;
				synchronize_bh();
				sk_filter_release(sk, filter);
				return 0;
			}
			return -ENOENT;
#endif
		/* We implement the SO_SNDLOWAT etc to
		   not be settable (1003.1g 5.3) */
		default:
		  	return(-ENOPROTOOPT);
  	}
	return ret;
}


int sock_getsockopt(struct socket *sock, int level, int optname,
		    char *optval, int *optlen)
{
	struct sock *sk = sock->sk;
	
	union
	{
  		int val;
  		struct linger ling;
		struct timeval tm;
	} v;
	
	int lv=sizeof(int),len;
  	
  	if(get_user(len,optlen))
  		return -EFAULT;

  	switch(optname) 
  	{
		case SO_DEBUG:		
			v.val = sk->debug;
			break;
		
		case SO_DONTROUTE:
			v.val = sk->localroute;
			break;
		
		case SO_BROADCAST:
			v.val= sk->broadcast;
			break;

		case SO_SNDBUF:
			v.val=sk->sndbuf;
			break;
		
		case SO_RCVBUF:
			v.val =sk->rcvbuf;
			break;

		case SO_REUSEADDR:
			v.val = sk->reuse;
			break;

		case SO_KEEPALIVE:
			v.val = sk->keepopen;
			break;

		case SO_TYPE:
			v.val = sk->type;		  		
			break;

		case SO_ERROR:
			v.val = -sock_error(sk);
			if(v.val==0)
				v.val=xchg(&sk->err_soft,0);
			break;

		case SO_OOBINLINE:
			v.val = sk->urginline;
			break;
	
		case SO_NO_CHECK:
			v.val = sk->no_check;
			break;

		case SO_PRIORITY:
			v.val = sk->priority;
			break;
		
		case SO_LINGER:	
			lv=sizeof(v.ling);
			v.ling.l_onoff=sk->linger;
 			v.ling.l_linger=sk->lingertime;
			break;
					
		case SO_BSDCOMPAT:
			v.val = sk->bsdism;
			break;
			
		case SO_RCVTIMEO:
		case SO_SNDTIMEO:
			lv=sizeof(struct timeval);
			v.tm.tv_sec=0;
			v.tm.tv_usec=0;
			break;

		case SO_RCVLOWAT:
		case SO_SNDLOWAT:
			v.val=1;
			break; 

		case SO_PASSCRED:
			v.val = sock->passcred;
			break;

		case SO_PEERCRED:
			lv=sizeof(sk->peercred);
			len=min(len, lv);
			if(copy_to_user((void*)optval, &sk->peercred, len))
				return -EFAULT;
			goto lenout;
			
		default:
			return(-ENOPROTOOPT);
	}
	len=min(len,lv);
	if(copy_to_user(optval,&v,len))
		return -EFAULT;
lenout:
  	if(put_user(len, optlen))
  		return -EFAULT;
  	return 0;
}

static kmem_cache_t *sk_cachep;

/*
 *	All socket objects are allocated here. This is for future
 *	usage.
 */
 
struct sock *sk_alloc(int family, int priority, int zero_it)
{
	struct sock *sk = kmem_cache_alloc(sk_cachep, priority);

	if(sk) {
		if (zero_it) 
			memset(sk, 0, sizeof(struct sock));
		sk->family = family;
	}

	return sk;
}

void sk_free(struct sock *sk)
{
#ifdef CONFIG_FILTER
	struct sk_filter *filter;
#endif
	if (sk->destruct)
		sk->destruct(sk);

#ifdef CONFIG_FILTER
	filter = sk->filter;
	if (filter) {
		sk_filter_release(sk, filter);
		sk->filter = NULL;
	}
#endif

	if (atomic_read(&sk->omem_alloc))
		printk(KERN_DEBUG "sk_free: optmem leakage (%d bytes) detected.\n", atomic_read(&sk->omem_alloc));

	kmem_cache_free(sk_cachep, sk);
}

void __init sk_init(void)
{
	sk_cachep = kmem_cache_create("sock", sizeof(struct sock), 0,
				      SLAB_HWCACHE_ALIGN, 0, 0);

}

/*
 *	Simple resource managers for sockets.
 */


/* 
 * Write buffer destructor automatically called from kfree_skb. 
 */
void sock_wfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	/* In case it might be waiting for more memory. */
	atomic_sub(skb->truesize, &sk->wmem_alloc);
	sk->write_space(sk);
}

/* 
 * Read buffer destructor automatically called from kfree_skb. 
 */
void sock_rfree(struct sk_buff *skb)
{
	struct sock *sk = skb->sk;

	atomic_sub(skb->truesize, &sk->rmem_alloc);
}


/*
 * Allocate a skb from the socket's send buffer.
 */
struct sk_buff *sock_wmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
	if (force || atomic_read(&sk->wmem_alloc) < sk->sndbuf) {
		struct sk_buff * skb = alloc_skb(size, priority);
		if (skb) {
			atomic_add(skb->truesize, &sk->wmem_alloc);
			skb->destructor = sock_wfree;
			skb->sk = sk;
			return skb;
		}
	}
	return NULL;
}

/*
 * Allocate a skb from the socket's receive buffer.
 */ 
struct sk_buff *sock_rmalloc(struct sock *sk, unsigned long size, int force, int priority)
{
	if (force || atomic_read(&sk->rmem_alloc) < sk->rcvbuf) {
		struct sk_buff *skb = alloc_skb(size, priority);
		if (skb) {
			atomic_add(skb->truesize, &sk->rmem_alloc);
			skb->destructor = sock_rfree;
			skb->sk = sk;
			return skb;
		}
	}
	return NULL;
}

/* 
 * Allocate a memory block from the socket's option memory buffer.
 */ 
void *sock_kmalloc(struct sock *sk, int size, int priority)
{
	if (atomic_read(&sk->omem_alloc)+size < sysctl_optmem_max) {
		void *mem;
		/* First do the add, to avoid the race if kmalloc
 		 * might sleep.
		 */
		atomic_add(size, &sk->omem_alloc);
		mem = kmalloc(size, priority);
		if (mem)
			return mem;
		atomic_sub(size, &sk->omem_alloc);
	}
	return NULL;
}

/*
 * Free an option memory block.
 */
void sock_kfree_s(struct sock *sk, void *mem, int size)
{
	kfree_s(mem, size); 
	atomic_sub(size, &sk->omem_alloc);
}

/* FIXME: this is insane. We are trying suppose to be controlling how
 * how much space we have for data bytes, not packet headers.
 * This really points out that we need a better system for doing the
 * receive buffer. -- erics
 * WARNING: This is currently ONLY used in tcp. If you need it else where
 * this will probably not be what you want. Possibly these two routines
 * should move over to the ipv4 directory.
 */
unsigned long sock_rspace(struct sock *sk)
{
	int amt = 0;

	if (sk != NULL) {
		/* This used to have some bizarre complications that
		 * to attempt to reserve some amount of space. This doesn't
	 	 * make sense, since the number returned here does not
		 * actually reflect allocated space, but rather the amount
		 * of space we committed to. We gamble that we won't
		 * run out of memory, and returning a smaller number does
		 * not change the gamble. If we lose the gamble tcp still
		 * works, it may just slow down for retransmissions.
		 */
		amt = sk->rcvbuf - atomic_read(&sk->rmem_alloc);
		if (amt < 0) 
			amt = 0;
	}
	return amt;
}


/* It is almost wait_for_tcp_memory minus release_sock/lock_sock.
   I think, these locks should be removed for datagram sockets.
 */
static void sock_wait_for_wmem(struct sock * sk)
{
	struct wait_queue wait = { current, NULL };

	sk->socket->flags &= ~SO_NOSPACE;
	add_wait_queue(sk->sleep, &wait);
	for (;;) {
		if (signal_pending(current))
			break;
		current->state = TASK_INTERRUPTIBLE;
		if (atomic_read(&sk->wmem_alloc) < sk->sndbuf)
			break;
		if (sk->shutdown & SEND_SHUTDOWN)
			break;
		if (sk->err)
			break;
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(sk->sleep, &wait);
}


/*
 *	Generic send/receive buffer handlers
 */

struct sk_buff *sock_alloc_send_skb(struct sock *sk, unsigned long size, 
			unsigned long fallback, int noblock, int *errcode)
{
	int err;
	struct sk_buff *skb;

	while (1) {
		unsigned long try_size = size;

		err = sock_error(sk);
		if (err != 0)
			goto failure;

		/*
		 *	We should send SIGPIPE in these cases according to
		 *	1003.1g draft 6.4. If we (the user) did a shutdown()
		 *	call however we should not. 
		 *
		 *	Note: This routine isn't just used for datagrams and
		 *	anyway some datagram protocols have a notion of
		 *	close down.
		 */

		err = -EPIPE;
		if (sk->shutdown&SEND_SHUTDOWN)
			goto failure;

		if (fallback) {
			/* The buffer get won't block, or use the atomic queue.
			 * It does produce annoying no free page messages still.
			 */
			skb = sock_wmalloc(sk, size, 0, GFP_BUFFER);
			if (skb)
				break;
			try_size = fallback;
		}
		skb = sock_wmalloc(sk, try_size, 0, sk->allocation);
		if (skb)
			break;

		/*
		 *	This means we have too many buffers for this socket already.
		 */

		sk->socket->flags |= SO_NOSPACE;
		err = -EAGAIN;
		if (noblock)
			goto failure;
		err = -ERESTARTSYS;
		if (signal_pending(current))
			goto failure;
		sock_wait_for_wmem(sk);
	}

	return skb;

failure:
	*errcode = err;
	return NULL;
}


void __release_sock(struct sock *sk)
{
#ifdef CONFIG_INET
	if (!sk->prot || !sk->backlog_rcv)
		return;
		
	/* See if we have any packets built up. */
	start_bh_atomic();
	while (!skb_queue_empty(&sk->back_log)) {
		struct sk_buff * skb = sk->back_log.next;
		__skb_unlink(skb, &sk->back_log);
		sk->backlog_rcv(sk, skb);
	}
	end_bh_atomic();
#endif  
}


/*
 *	Generic socket manager library. Most simpler socket families
 *	use this to manage their socket lists. At some point we should
 *	hash these. By making this generic we get the lot hashed for free.
 */
 
void sklist_remove_socket(struct sock **list, struct sock *sk)
{
	struct sock *s;

	start_bh_atomic();

	s= *list;
	if(s==sk)
	{
		*list = s->next;
		end_bh_atomic();
		return;
	}
	while(s && s->next)
	{
		if(s->next==sk)
		{
			s->next=sk->next;
			break;
		}
		s=s->next;
	}
	end_bh_atomic();
}

void sklist_insert_socket(struct sock **list, struct sock *sk)
{
	start_bh_atomic();
	sk->next= *list;
	*list=sk;
	end_bh_atomic();
}

/*
 *	This is only called from user mode. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */

void sklist_destroy_socket(struct sock **list, struct sock *sk);

/*
 *	Handler for deferred kills.
 */

static void sklist_destroy_timer(unsigned long data)
{
	struct sock *sk=(struct sock *)data;
	sklist_destroy_socket(NULL,sk);
}

/*
 *	Destroy a socket. We pass NULL for a list if we know the
 *	socket is not on a list.
 */
 
void sklist_destroy_socket(struct sock **list,struct sock *sk)
{
	struct sk_buff *skb;
	if(list)
		sklist_remove_socket(list, sk);

	while((skb=skb_dequeue(&sk->receive_queue))!=NULL)
	{
		kfree_skb(skb);
	}

	if(atomic_read(&sk->wmem_alloc) == 0 &&
	   atomic_read(&sk->rmem_alloc) == 0 &&
	   sk->dead)
	{
		sk_free(sk);
	}
	else
	{
		/*
		 *	Someone is using our buffers still.. defer
		 */
		init_timer(&sk->timer);
		sk->timer.expires=jiffies+SOCK_DESTROY_TIME;
		sk->timer.function=sklist_destroy_timer;
		sk->timer.data = (unsigned long)sk;
		add_timer(&sk->timer);
	}
}

/*
 * Set of default routines for initialising struct proto_ops when
 * the protocol does not support a particular function. In certain
 * cases where it makes no sense for a protocol to have a "do nothing"
 * function, some default processing is provided.
 */

int sock_no_dup(struct socket *newsock, struct socket *oldsock)
{
	struct sock *sk = oldsock->sk;

	return net_families[sk->family]->create(newsock, sk->protocol);
}

int sock_no_release(struct socket *sock, struct socket *peersock)
{
	return 0;
}

int sock_no_bind(struct socket *sock, struct sockaddr *saddr, int len)
{
	return -EOPNOTSUPP;
}

int sock_no_connect(struct socket *sock, struct sockaddr *saddr, 
		    int len, int flags)
{
	return -EOPNOTSUPP;
}

int sock_no_socketpair(struct socket *sock1, struct socket *sock2)
{
	return -EOPNOTSUPP;
}

int sock_no_accept(struct socket *sock, struct socket *newsock, int flags)
{
	return -EOPNOTSUPP;
}

int sock_no_getname(struct socket *sock, struct sockaddr *saddr, 
		    int *len, int peer)
{
	return -EOPNOTSUPP;
}

unsigned int sock_no_poll(struct file * file, struct socket *sock, poll_table *pt)
{
	return 0;
}

int sock_no_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	return -EOPNOTSUPP;
}

int sock_no_listen(struct socket *sock, int backlog)
{
	return -EOPNOTSUPP;
}

int sock_no_shutdown(struct socket *sock, int how)
{
	return -EOPNOTSUPP;
}

int sock_no_setsockopt(struct socket *sock, int level, int optname,
		    char *optval, int optlen)
{
	return -EOPNOTSUPP;
}

int sock_no_getsockopt(struct socket *sock, int level, int optname,
		    char *optval, int *optlen)
{
	return -EOPNOTSUPP;
}

/* 
 * Note: if you add something that sleeps here then change sock_fcntl()
 *       to do proper fd locking.
 */
int sock_no_fcntl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;

	switch(cmd)
	{
		case F_SETOWN:
			/*
			 * This is a little restrictive, but it's the only
			 * way to make sure that you can't send a sigurg to
			 * another process.
			 */
			if (current->pgrp != -arg &&
				current->pid != arg &&
				!capable(CAP_KILL)) return(-EPERM);
			sk->proc = arg;
			return(0);
		case F_GETOWN:
			return(sk->proc);
		default:
			return(-EINVAL);
	}
}

int sock_no_sendmsg(struct socket *sock, struct msghdr *m, int flags,
		    struct scm_cookie *scm)
{
	return -EOPNOTSUPP;
}

int sock_no_recvmsg(struct socket *sock, struct msghdr *m, int flags,
		    struct scm_cookie *scm)
{
	return -EOPNOTSUPP;
}



/*
 *	Default Socket Callbacks
 */

void sock_def_wakeup(struct sock *sk)
{
	if(!sk->dead)
		wake_up_interruptible(sk->sleep);
}

void sock_def_error_report(struct sock *sk)
{
	if (!sk->dead) {
		wake_up_interruptible(sk->sleep);
		sock_wake_async(sk->socket,0); 
	}
}

void sock_def_readable(struct sock *sk, int len)
{
	if(!sk->dead) {
		wake_up_interruptible(sk->sleep);
		sock_wake_async(sk->socket,1);
	}
}

void sock_def_write_space(struct sock *sk)
{
	/* Do not wake up a writer until he can make "significant"
	 * progress.  --DaveM
	 */
	if(!sk->dead &&
	   ((atomic_read(&sk->wmem_alloc) << 1) <= sk->sndbuf)) {
		wake_up_interruptible(sk->sleep);

		/* Should agree with poll, otherwise some programs break */
		if (sock_writeable(sk))
			sock_wake_async(sk->socket, 2);
	}
}

void sock_def_destruct(struct sock *sk)
{
	if (sk->protinfo.destruct_hook)
		kfree(sk->protinfo.destruct_hook);
}

void sock_init_data(struct socket *sock, struct sock *sk)
{
	skb_queue_head_init(&sk->receive_queue);
	skb_queue_head_init(&sk->write_queue);
	skb_queue_head_init(&sk->back_log);
	skb_queue_head_init(&sk->error_queue);
	
	init_timer(&sk->timer);
	
	sk->allocation	=	GFP_KERNEL;
	sk->rcvbuf	=	sysctl_rmem_default;
	sk->sndbuf	=	sysctl_wmem_default;
	sk->state 	= 	TCP_CLOSE;
	sk->zapped	=	1;
	sk->socket	=	sock;

	if(sock)
	{
		sk->type	=	sock->type;
		sk->sleep	=	&sock->wait;
		sock->sk	=	sk;
	}

	sk->state_change	=	sock_def_wakeup;
	sk->data_ready		=	sock_def_readable;
	sk->write_space		=	sock_def_write_space;
	sk->error_report	=	sock_def_error_report;
	sk->destruct            =       sock_def_destruct;

	sk->peercred.pid 	=	0;
	sk->peercred.uid	=	-1;
	sk->peercred.gid	=	-1;

}
