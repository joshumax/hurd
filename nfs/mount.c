/*
   Copyright (C) 1995,96,97,98,2001,02 Free Software Foundation, Inc.
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

#define malloc a_byte_for_every_bozotic_sun_lossage_and_youd_need_a_lotta_ram
#include <rpc/types.h>
#undef TRUE			/* Get rid of sun defs.  */
#undef FALSE
#undef malloc
#include <rpc/pmap_prot.h>
#include <errno.h>
#include <error.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <netinet/in.h>
#include <stdio.h>

#include "nfs.h"
#include "mount.h"

/* Service name for portmapper */
char *pmap_service_name = "sunrpc";

/* Fallback port number for portmapper */
short pmap_service_number = PMAPPORT;

/* RPC program for mount server. */
int mount_program = MOUNTPROG;

/* RPC version for mount server. */
int mount_version = MOUNTVERS;

/* Fallback port number for mount server. */
short mount_port = 0;

/* True iff MOUNT_PORT should be used even if portmapper present. */
int mount_port_override = 0;

/* RPC program number for NFS server. */
int nfs_program = NFS_PROGRAM;

/* RPC version number for NFS server. */
int nfs_version = NFS_VERSION;

/* Fallback port number for NFS server. */
short nfs_port = NFS_PORT;

/* True iff NFS_PORT should be used even if portmapper present. */
int nfs_port_override = 0;

/* Host name and port number we actually decided to use.  */
const char *mounted_hostname;
uint16_t mounted_nfs_port;	/* host order */

int protocol_version = 2;

/* Set up an RPC for procedure PROCNUM for talking to the portmapper.
   Allocate storage with malloc and point *BUF at it; caller must free
   this when done.  Return the address where the args for the
   procedure should be placed. */
static int *
pmap_initialize_rpc (int procnum, void **buf)
{
  return initialize_rpc (PMAPPROG, PMAPVERS, procnum, 0, buf, 0, 0, -1);
}

/* Set up an RPC for procedure PROCNUM for talking to the mount
   server.  Allocate storage with malloc and point *BUF at it; caller
   must free this when done.  Return the address where the args for
   the procedure should be placed.  */
static int *
mount_initialize_rpc (int procnum, void **buf)
{
  return initialize_rpc (MOUNTPROG, MOUNTVERS, procnum, 0, buf, 0, 0, -1);
}

/* Using the mount protocol, lookup NAME at host HOST.
   Return a node for it or null for an error.  If an
   error occurs, a message is automatically sent to stderr. */
struct node *
mount_root (char *name, char *host)
{
  struct sockaddr_in addr;
  struct hostent *h;
  int *p;
  void *rpcbuf;
  int port;
  error_t err;
  struct node *np;
  short pmapport;

  /* Lookup the portmapper port number */
  if (pmap_service_name)
    {
      struct servent *s;

      /* XXX This will always fail! pmap_service_name will always be "sunrpc"
         What should pmap_service_name really be?  By definition the second
	 argument is either "tcp" or "udp"  Thus, is this backwards
	 (as service_name suggests)?  If so, should it read:
             s = getservbyname (pmap_service_name, "udp");
         or is there something I am missing here?  */
      s = getservbyname ("sunrpc", pmap_service_name);
      if (s)
	pmapport = s->s_port;
      else
	pmapport = htons (pmap_service_number);
    }
  else
    pmapport = htons (pmap_service_number);

  /* Lookup the host */
  h = gethostbyname (host);
  if (!h)
    {
      herror (host);
      return 0;
    }

  addr.sin_family = h->h_addrtype;
  memcpy (&addr.sin_addr, h->h_addr_list[0], h->h_length);
  addr.sin_port = pmapport;

  if (mount_port_override)
    addr.sin_port = htons (mount_port);
  else
    {
      /* Formulate and send a PMAPPROC_GETPORT request
	 to lookup the mount program on the server.  */
      if (connect (main_udp_socket, (struct sockaddr *)&addr,
	           sizeof (struct sockaddr_in)) == -1)
	{
	  error (0, errno, "server mount program");
	  return 0;
	}

      p = pmap_initialize_rpc (PMAPPROC_GETPORT, &rpcbuf);
      if (! p)
	{
	  error (0, errno, "creating rpc packet");
	  return 0;
	}

      *(p++) = htonl (MOUNTPROG);
      *(p++) = htonl (MOUNTVERS);
      *(p++) = htonl (IPPROTO_UDP);
      *(p++) = htonl (0);
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	{
	  port = ntohl (*p);
	  p++;
	  addr.sin_port = htons (port);
	}
      else if (mount_port)
	addr.sin_port = htons (mount_port);
      else
	{
	  error (0, err, "portmap of mount");
	  goto error_with_rpcbuf;
	}
      free (rpcbuf);
    }

  /* Now, talking to the mount program, fetch a file handle
     for the root. */
  if (connect (main_udp_socket, (struct sockaddr *) &addr,
	       sizeof (struct sockaddr_in)) == -1)
    {
      error (0, errno, "connect");
      goto error_with_rpcbuf;
    }

  p = mount_initialize_rpc (MOUNTPROC_MNT, &rpcbuf);
  if (! p)
    {
      error (0, errno, "rpc");
      goto error_with_rpcbuf;
    }

  p = xdr_encode_string (p, name);
  err = conduct_rpc (&rpcbuf, &p);
  if (err)
    {
      error (0, err, "%s", name);
      goto error_with_rpcbuf;
    }
  /* XXX Protocol spec says this should be a "unix error code"; we'll
     pretend that an NFS error code is what's meant; the numbers match
     anyhow.  */
  err = nfs_error_trans (htonl (*p));
  p++;
  if (err)
    {
      error (0, err, "%s", name);
      goto error_with_rpcbuf;
    }

  /* Create the node for root */
  xdr_decode_fhandle (p, &np);
  free (rpcbuf);
  pthread_mutex_unlock (&np->lock);

  if (nfs_port_override)
    port = nfs_port;
  else
    {
      /* Send another PMAPPROC_GETPORT request to lookup the nfs server. */
      addr.sin_port = pmapport;
      if (connect (main_udp_socket, (struct sockaddr *) &addr,
	           sizeof (struct sockaddr_in)) == -1)
	{
	  error (0, errno, "connect");
	  return 0;
	}

      p = pmap_initialize_rpc (PMAPPROC_GETPORT, &rpcbuf);
      if (! p)
	{
	  error (0, errno, "rpc");
	  goto error_with_rpcbuf;
	}
      *(p++) = htonl (NFS_PROGRAM);
      *(p++) = htonl (NFS_VERSION);
      *(p++) = htonl (IPPROTO_UDP);
      *(p++) = htonl (0);
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	{
	  port = ntohl (*p);
	  p++;
	}
      else if (nfs_port)
	port = nfs_port;
      else
	{
	  error (0, err, "portmap of nfs server");
	  goto error_with_rpcbuf;
	}
      free (rpcbuf);
    }

  addr.sin_port = htons (port);
  if (connect (main_udp_socket, (struct sockaddr *) &addr,
	       sizeof (struct sockaddr_in)) == -1)
    {
      error (0, errno, "connect");
      return 0;
    }

  mounted_hostname = host;
  mounted_nfs_port = port;

  return np;

error_with_rpcbuf:
  free (rpcbuf);

  return 0;
}
