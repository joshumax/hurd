/* 
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <string.h>
#include <fcntl.h>

#include "nfsd.h"

#include <rpc/pmap_prot.h>
#include "../nfs/rpcsvc/mount.h"

#undef TRUE
#undef FALSE
#define malloc spoogie_woogie /* barf */
#include <rpc/xdr.h>
#include <rpc/types.h>
#include <rpc/auth.h>
#include <rpc/rpc_msg.h>
#undef malloc

void
server_loop (int fd)
{
  char buf[MAXIOSIZE];
  int xid;
  int *p, *r;
  char *rbuf;
  struct cached_reply *cr;
  int program;
  struct sockaddr_in sender;
  int version;
  int procedure;
  struct proctable *table = 0;
  struct procedure *proc;
  struct idspec *cred;
  struct cache_handle *c, fakec;
  error_t err;
  size_t addrlen;
  int *errloc;
  int cc;

  bzero (&fakec, sizeof (struct cache_handle));

  for (;;)
    {
      p = (int *) buf;
      proc = 0;
      addrlen = sizeof (struct sockaddr_in);
      cc = recvfrom (fd, buf, MAXIOSIZE, 0, &sender, &addrlen);
      if (cc == -1)
	continue;		/* ignore errors */
      xid = *p++;
      
      /* Ignore things that aren't proper RPCs. */
      if (ntohl (*p++) != CALL)
	continue;
      
      cr = check_cached_replies (xid, &sender);
      if (cr->data)
	/* This transacation has already completed */
	goto repost_reply;
      
      r = (int *) rbuf = malloc (MAXIOSIZE);
      
      if (ntohl (*p++) != RPC_MSG_VERSION)
	{
	  /* Reject RPC */
	  *r++ = xid;
	  *r++ = htonl (REPLY);
	  *r++ = htonl (MSG_DENIED);
	  *r++ = htonl (RPC_MISMATCH);
	  *r++ = htonl (RPC_MSG_VERSION);
	  *r++ = htonl (RPC_MSG_VERSION);
	  goto send_reply;
	}
      
      program = ntohl (*p++);
      switch (program)
	{
	case MOUNTPROG:
	  version = MOUNTVERS;
	  table = &mounttable;
	  break;
	  
	case NFS_PROGRAM:
	  version = NFS_VERSION;
	  table = &nfstable;
	  break;
	  
	case PMAPPROG:
	  version = PMAPVERS;
	  table = &pmaptable;
	  break;
	  
	default:
	  /* Program unavailable */
	  *r++ = xid;
	  *r++ = htonl (REPLY);
	  *r++ = htonl (MSG_ACCEPTED);
	  *r++ = htonl (AUTH_NULL);
	  *r++ = htonl (0);
	  *r++ = htonl (PROG_UNAVAIL);
	  goto send_reply;
	}	  
      
      if (ntohl (*p++) != version)
	{
	  /* Program mismatch */
	  *r++ = xid;
	  *r++ = htonl (REPLY);
	  *r++ = htonl (MSG_ACCEPTED);
	  *r++ = htonl (AUTH_NULL);
	  *r++ = htonl (0);
	  *r++ = htonl (PROG_MISMATCH);
	  *r++ = htonl (version);
	  *r++ = htonl (version);
	  goto send_reply;
	}
      
      procedure = htonl (*p++);
      if (procedure < table->min
	  || procedure > table->max
	  || table->procs[procedure - table->min].func == 0)
	{
	  /* Procedure unavailable */
	  *r++ = xid;
	  *r++ = htonl (REPLY);
	  *r++ = htonl (MSG_ACCEPTED);
	  *r++ = htonl (AUTH_NULL);
	  *r++ = htonl (0);
	  *r++ = htonl (PROC_UNAVAIL);
	  *r++ = htonl (table->min);
	  *r++ = htonl (table->max);
	  goto send_reply;
	}
      proc = &table->procs[procedure - table->min];
      
      p = process_cred (p, &cred);
      
      if (proc->need_handle)
	p = lookup_cache_handle (p, &c, cred);
      else
	{
	  fakec.ids = cred;
	  c = &fakec;
	}
      
      if (proc->alloc_reply)
	{
	  size_t amt;
	  amt = (*proc->alloc_reply) (p, version) + 256;
	  if (amt > MAXIOSIZE)
	    {
	      free (rbuf);
	      r = (int *) rbuf = malloc (amt);
	    }
	}
      
      /* Fill in beginning of reply */
      *r++ = xid;
      *r++ = htonl (REPLY);
      *r++ = htonl (MSG_ACCEPTED);
      *r++ = htonl (AUTH_NULL);
      *r++ = htonl (0);
      *r++ = htonl (SUCCESS);
      if (proc->process_error)
	{
	  /* Assume success for now and patch it later if necessary */
	  errloc = r;
	  *r++ = htonl (0);
	}
      
      if (c)
	err = (*proc->func) (c, p, &r, version);
      else
	err = ESTALE;
      
      if (proc->process_error && err)
	{
	  r = errloc;
	  *r++ = htonl (nfs_error_trans (err));
	}
      
      cred_rele (cred);
      if (c && c != &fakec)
	cache_handle_rele (c);
      
    send_reply:
      cr->data = rbuf;
      cr->len = (char *)r - rbuf;
      
    repost_reply:
      sendto (fd, cr->data, cr->len, 0, 
	      (struct sockaddr *)&sender, addrlen);
      release_cached_reply (cr);
    }
}
