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


#include <linux/wait.h>
#include <linux/socket.h>


#define NSOCKETS	2000		/* Dynamic, this is MAX LIMIT	*/
#define NSOCKETS_UNIX	128		/* unix domain static limit	*/
#define NPROTO		16		/* should be enough for now..	*/


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
/*
 * Internal representation of a socket. not all the fields are used by
 * all configurations:
 *
 *		server			client
 * conn		client connected to	server connected to
 * iconn	list of clients		-unused-
 *		 awaiting connections
 * wait		sleep for clients,	sleep for connection,
 *		sleep for i/o		sleep for i/o
 */
struct socket {
  short			type;		/* SOCK_STREAM, ...		*/
  socket_state		state;
  long			flags;
  struct proto_ops	*ops;		/* protocols do most everything	*/
  void			*data;		/* protocol data		*/
  struct socket		*conn;		/* server socket connected to	*/
  struct socket		*iconn;		/* incomplete client conn.s	*/
  struct socket		*next;
  struct wait_queue	**wait;		/* ptr to place to wait on	*/
  struct inode		*inode;
  struct fasync_struct  *fasync_list;	/* Asynchronous wake up list	*/
};

#define SOCK_INODE(S)	((S)->inode)

struct proto_ops {
  int	family;

  int	(*create)	(struct socket *sock, int protocol);
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
  int	(*read)		(struct socket *sock, char *ubuf, int size,
			 int nonblock);
  int	(*write)	(struct socket *sock, char *ubuf, int size,
			 int nonblock);
  int	(*select)	(struct socket *sock, int sel_type,
			 select_table *wait);
  int	(*ioctl)	(struct socket *sock, unsigned int cmd,
			 unsigned long arg);
  int	(*listen)	(struct socket *sock, int len);
  int	(*send)		(struct socket *sock, void *buff, int len, int nonblock,
			 unsigned flags);
  int	(*recv)		(struct socket *sock, void *buff, int len, int nonblock,
			 unsigned flags);
  int	(*sendto)	(struct socket *sock, void *buff, int len, int nonblock,
			 unsigned flags, struct sockaddr *, int addr_len);
  int	(*recvfrom)	(struct socket *sock, void *buff, int len, int nonblock,
			 unsigned flags, struct sockaddr *, int *addr_len);
  int	(*shutdown)	(struct socket *sock, int flags);
  int	(*setsockopt)	(struct socket *sock, int level, int optname,
			 char *optval, int optlen);
  int	(*getsockopt)	(struct socket *sock, int level, int optname,
			 char *optval, int *optlen);
  int	(*fcntl)	(struct socket *sock, unsigned int cmd,
			 unsigned long arg);	
};

struct net_proto {
	char *name;		/* Protocol name */
	void (*init_func)(struct net_proto *);	/* Bootstrap */
};

extern int	sock_awaitconn(struct socket *mysock, struct socket *servsock, int flags);
extern int	sock_wake_async(struct socket *sock, int how);
extern int	sock_register(int family, struct proto_ops *ops);
extern int	sock_unregister(int family);
extern struct socket *sock_alloc(void);
extern void	sock_release(struct socket *sock);
#endif /* __KERNEL__ */
#endif	/* _LINUX_NET_H */
