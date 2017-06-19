/* rpc.c - SunRPC management for NFS client.
   Copyright (C) 1994, 1995, 1996, 1997, 2002 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "nfs.h"

/* Needed in order to get the RPC header files to include correctly.  */
#undef TRUE
#undef FALSE
#define malloc spoiufasdf	/* Avoid bogus definition in rpc/types.h.  */

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/rpc_msg.h>
#include <rpc/auth_unix.h>

#undef malloc			/* Get rid of the sun block.  */

#include <netinet/in.h>
#include <assert-backtrace.h>
#include <errno.h>
#include <error.h>
#include <unistd.h>
#include <stdio.h>

/* One of these exists for each pending RPC.  */
struct rpc_list
{
  struct rpc_list *next, **prevp;
  void *reply;
};

/* A list of all pending RPCs.  */
static struct rpc_list *outstanding_rpcs;

/* Wake up this condition when an outstanding RPC has received a reply
   or we should check for timeouts.  */
static pthread_cond_t rpc_wakeup = PTHREAD_COND_INITIALIZER;

/* Lock the global data and the REPLY fields of outstanding RPC's.  */
static pthread_mutex_t outstanding_lock = PTHREAD_MUTEX_INITIALIZER;



/* Generate and return a new transaction ID.  */
static inline int
generate_xid ()
{
  static int nextxid;
  
  if (nextxid == 0)
    nextxid = mapped_time->seconds;
  
  return nextxid++;
}

/* Set up an RPC for procdeure RPC_PROC for talking to the server
   PROGRAM of version VERSION.  Allocate storage with malloc and point
   *BUF at it; caller must free this when done.  Allocate at least LEN
   bytes more than the usual amount for the RPC.  Initialize the RPC
   credential structure with UID, GID, and SECOND_GID;  any of these
   may be -1 to indicate that it does not apply, however, exactly zero
   or two of UID and GID must be -1.  The returned address is a pointer
   to the start of the payload.  If NULL is returned, an error occurred
   and the code is set in errno.  */
int *
initialize_rpc (int program, int version, int rpc_proc, 
		size_t len, void **bufp, 
		uid_t uid, gid_t gid, gid_t second_gid)
{
  void *buf;
  int *p, *lenaddr;
  struct rpc_list *hdr;

  buf = malloc (len + 1024);
  if (! buf)
    {
      errno = ENOMEM;
      return NULL;
    }

  /* First the struct rpc_list bit. */
  hdr = buf;
  hdr->reply = 0;
  
  p = buf + sizeof (struct rpc_list);

  /* RPC header */
  *(p++) = htonl (generate_xid ());
  *(p++) = htonl (CALL);
  *(p++) = htonl (RPC_MSG_VERSION);
  *(p++) = htonl (program);
  *(p++) = htonl (version);
  *(p++) = htonl (rpc_proc);
  
  assert_backtrace ((uid == -1) == (gid == -1));

  if (uid == -1)
    {
      /* No authentication */
      *(p++) = htonl (AUTH_NONE);
      *(p++) = 0;
    }
  else
    {
      /* Unixy authentication */
      *(p++) = htonl (AUTH_UNIX);
      /* The length of the message.  We do not yet know what this
         is, so, just remember where we should put it when we know */
      lenaddr = p++;
      *(p++) = htonl (mapped_time->seconds);
      p = xdr_encode_string (p, hostname);
      *(p++) = htonl (uid);
      *(p++) = htonl (gid);
      if (second_gid == -1)
	*(p++) = 0;
      else
	{
	  *(p++) = htonl (1);
	  *(p++) = htonl (second_gid);
	}
      *lenaddr = htonl ((p - (lenaddr + 1)) * sizeof (int));
    }
        
  /* VERF field */
  *(p++) = htonl (AUTH_NONE);
  *(p++) = 0;
  
  *bufp = buf;
  return p;
}

/* Remove HDR from the list of pending RPC's.  The rpc_list's lock
   (OUTSTANDING_LOCK) must be held.  */
static inline void
unlink_rpc (struct rpc_list *hdr)
{
  *hdr->prevp = hdr->next;
  if (hdr->next)
    hdr->next->prevp = hdr->prevp;
}

/* Insert HDR at the head of the LIST.  The rpc_list's lock
   (OUTSTANDING_LOCK) must be held.  */
static inline void
link_rpc (struct rpc_list **list, struct rpc_list *hdr)
{
  hdr->next = *list;
  if (hdr->next)
    hdr->next->prevp = &hdr->next;
  hdr->prevp = list;
  *list = hdr;
}

/* Send the specified RPC message.  *RPCBUF is the initialized buffer
   from a previous initialize_rpc call; *PP, the payload, points past
   the filledin args.  Set *PP to the address of the reply contents
   themselves.  The user will be expected to free *RPCBUF (which will
   have changed) when done with the reply contents.  The old value of
   *RPCBUF will be freed by this routine.  */
error_t
conduct_rpc (void **rpcbuf, int **pp)
{
  struct rpc_list *hdr = *rpcbuf;
  error_t err;
  size_t cc, nc;
  int timeout = initial_transmit_timeout;
  time_t lasttrans;
  int ntransmit = 0;
  int *p;
  int xid;
  int n;
  int cancel;
  
  pthread_mutex_lock (&outstanding_lock);

  link_rpc (&outstanding_rpcs, hdr);

  xid = * (int *) (*rpcbuf + sizeof (struct rpc_list));

  do
    {
      /* If we've sent enough, give up.  */
      if (mounted_soft && ntransmit == soft_retries)
	{
	  unlink_rpc (hdr);
	  pthread_mutex_unlock (&outstanding_lock);
	  return ETIMEDOUT;
	}

      /* Issue the RPC.  */
      lasttrans = mapped_time->seconds;
      ntransmit++;
      nc = (void *) *pp - *rpcbuf - sizeof (struct rpc_list);
      cc = write (main_udp_socket, *rpcbuf + sizeof (struct rpc_list), nc);
      if (cc == -1)
	{
	  unlink_rpc (hdr);
	  pthread_mutex_unlock (&outstanding_lock);
	  return errno;
	}
      else 
	assert_backtrace (cc == nc);
      
      /* Wait for reply.  */
      cancel = 0;
      while (!hdr->reply
	     && (mapped_time->seconds - lasttrans < timeout)
	     && !cancel)
	cancel = pthread_hurd_cond_wait_np (&rpc_wakeup, &outstanding_lock);
  
      if (cancel)
	{
	  unlink_rpc (hdr);
	  pthread_mutex_unlock (&outstanding_lock);
	  return EINTR;
	}

      /* hdr->reply will have been filled in by rpc_receive_thread,
         if it has been filled in, then the rpc has been fulfilled,
         otherwise, retransmit and continue to wait.  */
      if (!hdr->reply)
	{
	  timeout *= 2;
	  if (timeout > max_transmit_timeout)
	    timeout = max_transmit_timeout;
	}
    }
  while (!hdr->reply);

  pthread_mutex_unlock (&outstanding_lock);

  /* Switch to the reply buffer.  */
  *rpcbuf = hdr->reply;
  free (hdr);

  /* Process the reply, dissecting errors.  When we're done and if
     there is no error, set *PP to the rpc return contents.  */ 
  p = (int *) *rpcbuf;
  
  /* If the transmition id does not match that in the message,
     something strange happened in rpc_receive_thread.  */
  assert_backtrace (*p == xid);
  p++;
  
  switch (ntohl (*p))
    {
    default:
      err = EBADRPC;
      break;
      
    case REPLY:
      p++;
      switch (ntohl (*p))
	{
	default:
	  err = EBADRPC;
	  break;
	  
	case MSG_DENIED:
	  p++;
	  switch (ntohl (*p))
	    {
	    default:
	      err = EBADRPC;
	      break;
	      
	    case RPC_MISMATCH:
	      err = ERPCMISMATCH;
	      break;
	      	      
	    case AUTH_ERROR:
	      p++;
	      switch (ntohl (*p))
		{
		case AUTH_BADCRED:
		case AUTH_REJECTEDCRED:
		  err = EAUTH;
		  break;

		case AUTH_TOOWEAK:
		  err = ENEEDAUTH;
		  break;
		  		  
		case AUTH_BADVERF:
		case AUTH_REJECTEDVERF:
		default:
		  err = EBADRPC;
		  break;
		}
	      break;
	    }
	  break;
	  
	case MSG_ACCEPTED:
	  p++;

	  /* Process VERF field.  */
	  p++;			/* Skip verf type.  */
	  n = ntohl (*p);	/* Size of verf.  */
	  p++;
	  p += INTSIZE (n);	/* Skip verf itself.  */

	  switch (ntohl (*p))
	    {
	    default:
	    case GARBAGE_ARGS:
	      err = EBADRPC;
	      break;

	    case PROG_UNAVAIL:
	      err = EPROGUNAVAIL;
	      break;
	      
	    case PROG_MISMATCH:
	      err = EPROGMISMATCH;
	      break;
	      
	    case PROC_UNAVAIL:
	      err = EPROCUNAVAIL;
	      break;

	    case SUCCESS:
	      p++;
	      *pp = p;
	      err = 0;
	      break;
	    }
	  break;
	}
      break;
    }
  return err;
}

/* Dedicated thread to signal those waiting on rpc_wakeup
   once a second.  */
void *
timeout_service_thread (void *arg)
{
  (void) arg;

  while (1)
    {
      sleep (1);
      pthread_mutex_lock (&outstanding_lock);
      pthread_cond_broadcast (&rpc_wakeup);
      pthread_mutex_unlock (&outstanding_lock);
    }

  return NULL;
}

/* Dedicate thread to receive RPC replies, register them on the queue
   of pending wakeups, and deal appropriately.  */
void *
rpc_receive_thread (void *arg)
{
  void *buf;

  (void) arg;

  /* Allocate a receive buffer.  */
  buf = malloc (1024 + read_size);
  assert_backtrace (buf);

  while (1)
    {
      int cc = read (main_udp_socket, buf, 1024 + read_size);
      if (cc == -1)
        {
          error (0, errno, "nfs read");
          continue;
        }
      else
        {
          struct rpc_list *r;
          int xid = *(int *)buf;

          pthread_mutex_lock (&outstanding_lock);

          /* Find the rpc that we just fulfilled.  */
          for (r = outstanding_rpcs; r; r = r->next)
    	    {
    	      if (* (int *) &r[1] == xid)
	        {
	          unlink_rpc (r);
	          r->reply = buf;
	          pthread_cond_broadcast (&rpc_wakeup);
		  break;
	        }
	    }
#if 0
	  if (! r)
	    fprintf (stderr, "NFS dropping reply xid %d\n", xid);
#endif
	  pthread_mutex_unlock (&outstanding_lock);

	  /* If r is not null then we had a message from a pending
	     (i.e. known) rpc.  Thus, it was fulfilled and if we want
	     to get another request, a new buffer is needed.  */
	  if (r)
	    {
              buf = malloc (1024 + read_size);
              assert_backtrace (buf);
	    }
        }
    }

  return NULL;
}
