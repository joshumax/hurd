#ifndef _HACK_SOCKET_H_
#define _HACK_SOCKET_H_

#include <linux/types.h>
#include <asm/system.h>

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <limits.h>


/* #define IP_MAX_MEMBERSHIPS 10 */

#define IPTOS_LOWDELAY 0x10
#define IPTOS_THROUGHPUT 0x08
#define IPTOS_RELIABILITY 0x04

#define SOPRI_INTERACTIVE 0
#define SOPRI_NORMAL 1
#define SOPRI_BACKGROUND 2

#ifndef SOL_IP
#define SOL_IP IPPROTO_IP
#endif
#define SOL_TCP IPPROTO_TCP
#ifndef SOL_IPV6
#define SOL_IPV6 IPPROTO_IPV6
#endif
#ifndef SOL_ICMPV6
#define SOL_ICMPV6 IPPROTO_ICMPV6
#endif
#define SOL_RAW IPPROTO_RAW

/* IP options */
#define IP_PKTINFO	190
#define IP_PKTOPTIONS	191
#define IP_MTU_DISCOVER	192
#define IP_RECVERR	193
#define IP_RECVTTL	194
#define	IP_RECVTOS	195
#define IP_MTU		196
#define IP_ROUTER_ALERT	197


/* TCP options */
#define TCP_NODELAY 1
#define TCP_MAXSEG 2
#define TCP_CORK 3

#define SO_NO_CHECK 11
#define SO_PRIORITY 12

#define	SO_PASSCRED 190
#define	SO_PEERCRED 191
#define	SO_BSDCOMPAT 192

/* Maximum queue length specifiable by listen.  */
#ifndef SOMAXCONN
#define SOMAXCONN	128
#endif

#ifndef CMSG_DATA
#define msg_control	msg_accrights
#define msg_controllen	msg_accrightslen
struct cmsghdr { int cmsg_garbage; };
#define cmsg_len	cmsg_garbage
#define cmsg_type	cmsg_garbage
#define cmsg_level	cmsg_garbage
static inline int
put_cmsg(struct msghdr *msg, int level, int type, int len, void *data)
{ return 0; }
#define CMSG_FIRSTHDR(msg)	(0)
#define CMSG_NXTHDR(msg, cmsg)	(0)
#define CMSG_DATA(cmsg)		(0)
#define CMSG_ALIGN(size)	(0)
#define CMSG_LEN(size) 		(0)
#else
static inline int
put_cmsg(struct msghdr *msg, int level, int type, int len, void *data)
{ return 0; }
#endif

#ifndef MSG_NOSIGNAL
# warning "http://lists.gnu.org/archive/html/bug-hurd/2008-10/msg00007.html"
# define MSG_NOSIGNAL	0
#endif
#define MSG_ERRQUEUE	0

/* There is no SOCK_PACKET, it is a bad bad thing.  This chicanery is
   because the one use of it is a comparison against a `short int' value;
   using a value outside the range of that type ensures that the comparison
   will always fail, and in fact it and the dead code will get optimized
   out entirely at compile time.  */
#define SOCK_PACKET	((int)((uint32_t)USHRT_MAX) * 2)
#define PF_PACKET	0

#ifndef UIO_MAXIOV
#define UIO_MAXIOV 4		/* 1 would do */
#endif


struct ucred {
  pid_t pid;
  uid_t uid;
  gid_t gid;
};


extern inline int		/* Does not modify IOV.  */
memcpy_fromiovecend (unsigned char *kdata, struct iovec *iov,
		     int offset, int len)
{
  assert_backtrace (offset + len <= iov->iov_len);
  memcpy (kdata, iov->iov_base + offset, len);
  return 0;
}
extern inline int		/* Modifies IOV to consume LEN bytes.  */
memcpy_fromiovec (unsigned char *kdata, struct iovec *iov, int len)
{
  assert_backtrace (len <= iov->iov_len);
  memcpy (kdata, iov->iov_base, len);
  iov->iov_base += len;
  iov->iov_len -= len;
  return 0;
}
extern inline void		/* Modifies IOV to consume LEN bytes.  */
memcpy_tokerneliovec (struct iovec *iov, unsigned char *kdata, int len)
{
  assert_backtrace (len <= iov->iov_len);
  memcpy (iov->iov_base, kdata, len);
  iov->iov_base += len;
  iov->iov_len -= len;
}
extern inline int		/* Modifies IOV to consume LEN bytes.  */
memcpy_toiovec (struct iovec *iov, unsigned char *kdata, int len)
{
  memcpy_tokerneliovec (iov, kdata, len);
  return 0;
}

extern int csum_partial_copy_fromiovecend(unsigned char *kdata,
					  struct iovec *iov,
					  int offset,
					  unsigned int len, int *csump);

static inline int move_addr_to_kernel(void *uaddr, int ulen, void *kaddr)
{
  abort ();
  return 0;
}
#if 0
extern int verify_iovec(struct msghdr *m, struct iovec *iov, char *address, int mode);
extern int move_addr_to_user(void *kaddr, int klen, void *uaddr, int *ulen);
#endif


#endif
