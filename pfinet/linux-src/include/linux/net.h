/*
 * NET		An implementation of the SOCKET network access protocol.
 *		This is the master header file for the Linux NET layer,
 *		or, in plain English: the networking handling part of the
 *		kernel.
 *
 * Version:	@(#)net.h	1.0.3	05/25/93
 *
 * Authors:	Orest Zborowski, <obz@Kodak.COM>
 *		Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_NET_H
#define _LINUX_NET_H

#include <linux/socket.h>

struct poll_table_struct;

#define NPROTO		32		/* should be enough for now..	*/


#define SYS_SOCKET	1		/* sys_socket(2)		*/
#define SYS_BIND	2		/* sys_bind(2)			*/
#define SYS_CONNECT	3		/* sys_connect(2)		*/
#define SYS_LISTEN	4		/* sys_listen(2)		*/
#define SYS_ACCEPT	5		/* sys_accept(2)		*/
#define SYS_GETSOCKNAME	6		/* sys_getsockname(2)		*/
#define SYS_GETPEERNAME	7		/* sys_getpeername(2)		*/
#define SYS_SOCKETPAIR	8		/* sys_socketpair(2)		*/
#define SYS_SEND	9		/* sys_send(2)			*/
#define SYS_RECV	10		/* sys_recv(2)			*/
#define SYS_SENDTO	11		/* sys_sendto(2)		*/
#define SYS_RECVFROM	12		/* sys_recvfrom(2)		*/
#define SYS_SHUTDOWN	13		/* sys_shutdown(2)		*/
#define SYS_SETSOCKOPT	14		/* sys_setsockopt(2)		*/
#define SYS_GETSOCKOPT	15		/* sys_getsockopt(2)		*/
#define SYS_SENDMSG	16		/* sys_sendmsg(2)		*/
#define SYS_RECVMSG	17		/* sys_recvmsg(2)		*/


typedef enum {
  SS_FREE = 0,				/* not allocated		*/
  SS_UNCONNECTED,			/* unconnected to any socket	*/
  SS_CONNECTING,			/* in process of connecting	*/
  SS_CONNECTED,				/* connected to socket		*/
  SS_DISCONNECTING			/* in process of disconnecting	*/
} socket_state;

#define SO_ACCEPTCON	(1<<16)		/* performed a listen		*/
#define SO_WAITDATA	(1<<17)		/* wait data to read		*/
#define SO_NOSPACE	(1<<18)		/* no space to write		*/

#ifdef __KERNEL__

struct file;		/* forward decl magic */
struct socket
{
	socket_state		state;

	unsigned long		flags;
	struct proto_ops	*ops;
	struct inode		*inode;
#ifdef _HURD_
 	uint_fast32_t		refcnt;	/* # of sock_user's pointing to this */
	mach_port_t 		identity; /* for io_identity */
  	ino_t			st_ino;
#else
	struct fasync_struct	*fasync_list;	/* Asynchronous wake up list	*/
	struct file		*file;		/* File back pointer for gc	*/
#endif
	struct sock		*sk;
	struct wait_queue	*wait;

	short			type;
	unsigned char		passcred;
	unsigned char		tli;
};

#define SOCK_INODE(S)	((S)->inode)

struct scm_cookie;

struct proto_ops {
  int	family;

  int	(*dup)		(struct socket *newsock, struct socket *oldsock);
  int	(*release)	(struct socket *sock, struct socket *peer);
  int	(*bind)		(struct socket *sock, struct sockaddr *umyaddr,
			 int sockaddr_len);
  int	(*connect)	(struct socket *sock, struct sockaddr *uservaddr,
			 int sockaddr_len, int flags);
  int	(*socketpair)	(struct socket *sock1, struct socket *sock2);
  int	(*accept)	(struct socket *sock, struct socket *newsock,
			 int flags);
  int	(*getname)	(struct socket *sock, struct sockaddr *uaddr,
			 int *usockaddr_len, int peer);
  unsigned int (*poll)	(struct file *file, struct socket *sock, struct poll_table_struct *wait);
  int	(*ioctl)	(struct socket *sock, unsigned int cmd,
			 unsigned long arg);
  int	(*listen)	(struct socket *sock, int len);
  int	(*shutdown)	(struct socket *sock, int flags);
  int	(*setsockopt)	(struct socket *sock, int level, int optname,
			 char *optval, int optlen);
  int	(*getsockopt)	(struct socket *sock, int level, int optname,
			 char *optval, int *optlen);
  int	(*fcntl)	(struct socket *sock, unsigned int cmd,
			 unsigned long arg);
  int   (*sendmsg)	(struct socket *sock, struct msghdr *m, int total_len, struct scm_cookie *scm);
  int   (*recvmsg)	(struct socket *sock, struct msghdr *m, int total_len, int flags, struct scm_cookie *scm);
};

struct net_proto_family
{
	int	family;
	int	(*create)(struct socket *sock, int protocol);
	/* These are counters for the number of different methods of
	   each we support */
	short	authentication;
	short	encryption;
	short	encrypt_net;
};

struct net_proto
{
	const char *name;		/* Protocol name */
	void (*init_func)(struct net_proto *);	/* Bootstrap */
};

extern struct net_proto_family *net_families[];
extern int	sock_wake_async(struct socket *sk, int how);
extern int	sock_register(struct net_proto_family *fam);
extern int	sock_unregister(int family);
extern struct socket *sock_alloc(void);
extern int	sock_create(int family, int type, int proto, struct socket **);
extern void	sock_release(struct socket *);
extern int   	sock_sendmsg(struct socket *, struct msghdr *m, int len);
extern int	sock_recvmsg(struct socket *, struct msghdr *m, int len, int flags);
extern int	sock_readv_writev(int type, struct inode * inode, struct file * file,
				  const struct iovec * iov, long count, long size);

extern int	net_ratelimit(void);
extern unsigned long net_random(void);
extern void net_srandom(unsigned long);

#endif /* __KERNEL__ */
#endif	/* _LINUX_NET_H */
