/* SunRPC management for NFS client
   Copyright (C) 1994, 1995, 1996, 1997 Free Software Foundation

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

/* Needed in order to get the RPC header files to include correctly */
#undef TRUE
#undef FALSE
#define malloc spoiufasdf	/* Avoid bogus definition in rpc/types.h */

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#include <rpc/rpc_msg.h>
#include <rpc/auth_unix.h>

#undef malloc			/* Get rid protection.  */

#include <netinet/in.h>
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

/* One of these exists for each pending RPC. */
struct rpc_list
{
  struct rpc_list *next, **prevp;
  void *reply;
};

/* A list of all the pending RPCs. */
static struct rpc_list *outstanding_rpcs;

/* Wake up this condition when an outstanding RPC has received a reply
   or we should check for timeouts. */
static struct condition rpc_wakeup = CONDITION_INITIALIZER;

/* Lock the global data and the REPLY fields of outstanding RPC's. */
static struct mutex outstanding_lock = MUTEX_INITIALIZER;



/* Generate and return a new transaction ID. */
static int
generate_xid ()
{
  static int nextxid;
  
  if (nextxid == 0)
    nextxid = mapped_time->seconds;
  
  return nextxid++;
}

/* Set up an RPC for procdeure RPC_PROC, for talking to the server
   PROGRAM of version VERSION.  Allocate storage with malloc and point
   *BUF at it; caller must free this when done.  Allocate at least LEN
   bytes more than the usual amount for an RPC.  Initialize the RPC
   credential structure with UID, GID, and SECOND_GID.  (Any of those
   may be -1 to indicate that it does not apply; exactly or two of UID
   and GID must be -1, however.) */
int *
initialize_rpc (int program, int version, int rpc_proc, 
		size_t len, void **bufp, 
		uid_t uid, gid_t gid, gid_t second_gid)
{
  void *buf = malloc (len + 1024);
  int *p, *lenaddr;
  struct rpc_list *hdr;

  /* First the struct rpc_list bit. */
  hdr = buf;
  hdr->reply = 0;
  
  p = buf + sizeof (struct rpc_list);

  /* RPC header */
  *p++ = htonl (generate_xid ());
  *p++ = htonl (CALL);
  *p++ = htonl (RPC_MSG_VERSION);
  *p++ = htonl (program);
  *p++ = htonl (version);
  *p++ = htonl (rpc_proc);
  
  assert ((uid == -1) == (gid == -1));

  if (uid != -1)
    {
      *p++ = htonl (AUTH_UNIX);
      lenaddr = p++;
      *p++ = htonl (mapped_time->seconds);
      p = xdr_encode_string (p, hostname);
      *p++ = htonl (uid);
      *p++ = htonl (gid);
      if (second_gid != -1)
	{
	  *p++ = htonl (1);
	  *p++ = htonl (second_gid);
	}
      else
	*p++ = 0;
      *lenaddr = htonl ((p - (lenaddr + 1)) * sizeof (int));
    }
  else
    {
      *p++ = htonl (AUTH_NULL);
      *p++ = 0;
    }
        
  /* VERF field */
  *p++ = htonl (AUTH_NULL);
  *p++ = 0;
  
  *bufp = buf;
  return p;
}

/* Remove HDR from the list of pending RPC's.  rpc_list_lock must be
   held */
static void
unlink_rpc (struct rpc_list *hdr)
{
  *hdr->prevp = hdr->next;
  if (hdr->next)
    hdr->next->prevp = hdr->prevp;
}

/* Send the specified RPC message.  *RPCBUF is the initialized buffer
   from a previous initialize_rpc call; *PP points past the filled
   in args.  Set *PP to the address of the reply contents themselves.
   The user will be expected to free *RPCBUF (which will have changed)
   when done with the reply contents.  The old value of *RPCBUF will
   be freed by this routine. */
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
  
  mutex_lock (&outstanding_lock);

  /* Link it in */
  hdr->next = outstanding_rpcs;
  if (hdr->next)
    hdr->next->prevp = &hdr->next;
  hdr->prevp = &outstanding_rpcs;
  outstanding_rpcs = hdr;

  xid = * (int *) (*rpcbuf + sizeof (struct rpc_list));

  do
    {
      /* If we've sent enough, give up */
      if (mounted_soft && ntransmit == soft_retries)
	{
	  unlink_rpc (hdr);
	  mutex_unlock (&outstanding_lock);
	  return ETIMEDOUT;
	}

      /* Issue the RPC */
      lasttrans = mapped_time->seconds;
      ntransmit++;
      nc = (void *) *pp - *rpcbuf - sizeof (struct rpc_list);
      cc = write (main_udp_socket, *rpcbuf + sizeof (struct rpc_list), nc);
      if (cc == -1)
	{
	  unlink_rpc (hdr);
	  mutex_unlock (&outstanding_lock);
	  return errno;
	}
      else 
	assert (cc == nc);
      
      /* Wait for reply */
      cancel = 0;
      while (!hdr->reply
	     && (mapped_time->seconds - lasttrans < timeout)
	     && !cancel)
	cancel = hurd_condition_wait (&rpc_wakeup, &outstanding_lock);
  
      if (cancel)
	{
	  unlink_rpc (hdr);
	  mutex_unlock (&outstanding_lock);
	  return EINTR;
	}

      if (!hdr->reply)
	{
	  timeout *=2;
	  if (timeout > max_transmit_timeout)
	    timeout = max_transmit_timeout;
	}
    }
  while (!hdr->reply);

  mutex_unlock (&outstanding_lock);

  /* Switch to the reply buffer. */
  *rpcbuf = hdr->reply;
  free (hdr);

  /* Process the reply, dissecting errors.  When we're done, set *PP to
     the rpc return contents, if there is no error. */
  p = (int *) *rpcbuf;
  
  assert (*p == xid);
  p++;
  
  switch (ntohl (*p++))
    {
    default:
      err = EBADRPC;
      break;
      
    case REPLY:
      switch (ntohl (*p++))
	{
	default:
	  err = EBADRPC;
	  break;
	  
	case MSG_DENIED:
	  switch (ntohl (*p++))
	    {
	    default:
	      err = EBADRPC;
	      break;
	      
	    case RPC_MISMATCH:
	      err = ERPCMISMATCH;
	      break;
	      	      
	    case AUTH_ERROR:
	      switch (ntohl (*p++))
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

	  /* Process VERF field. */
	  p++;			/* skip verf type */
	  n = ntohl (*p++);	/* size of verf */
	  p += INTSIZE (n);	/* skip verf itself */

	  switch (ntohl (*p++))
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

/* Dedicated thread to wakeup rpc_wakeup once a second. */
void
timeout_service_thread ()
{
  while (1)
    {
      sleep (1);
      mutex_lock (&outstanding_lock);
      condition_broadcast (&rpc_wakeup);
      mutex_unlock (&outstanding_lock);
    }
}

/* Dedicate thread to receive RPC replies, register them on the queue
   of pending wakeups, and deal appropriately. */
void
rpc_receive_thread ()
{
  int cc;
  void *buf;
  struct rpc_list *r;
  int xid;

  while (1)
    {
      buf = malloc (1024 + read_size);

      do
	{
	  cc = read (main_udp_socket, buf, 1024 + read_size);
	  if (cc == -1)
	    {
	      perror ("nfs read");
	      r = 0;
	    }
	  else
	    {
	      xid = *(int *)buf;
	      mutex_lock (&outstanding_lock);
	      for (r = outstanding_rpcs; r; r = r->next)
		{
		  if (* (int *) &r[1] == xid)
		    {
		      /* Remove it from the list */
		      *r->prevp = r->next;
		      if (r->next)
			r->next->prevp = r->prevp;

		      r->reply = buf;
		      condition_broadcast (&rpc_wakeup);
		      break;
		    }
		}
#if notanymore
	      if (!r)
		fprintf (stderr, "NFS dropping reply xid %d\n", xid);
#endif
	      mutex_unlock (&outstanding_lock);
	    }
	}
      while (!r);
    }
}
