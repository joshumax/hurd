/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

#include "nfs.h"

#include <rpcsvc/mount.h>
#include <rpc/pmap_prot.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <netinet/in.h>
#include <stdio.h>


int *
pmap_initialize_rpc (int procnum, void **buf)
{
  return initialize_rpc (PMAPPROG, PMAPVERS, procnum, 0, buf, 0, 0, -1);
}

int *
mount_initialize_rpc (int procnum, void **buf)
{
  return initialize_rpc (MOUNTPROG, MOUNTVERS, procnum, 0, buf, 0, 0, -1);
}

/* Using the mount protocol, lookup NAME at host HOST.
   Return a node for it or null for an error. */
struct node *
mount_root (char *name, char *host)
{
  struct sockaddr_in addr;
  struct hostent *h;
  struct servent *s;
  int *p;
  void *rpcbuf;
  int port;
  struct node *np;

  /* Lookup the portmapper port number */
  s = getservbyname ("sunrpc", "udp");
  if (!s)
    {
      fprintf (stderr, "portmap/udp: unknown service\n");
      return 0;
    }

  /* Lookup the host */
  h = gethostbyname (host);
  if (!h)
    {
      herror (host);
      return 0;
    }
  
  addr.sin_family = h->h_addrtype;
  bcopy (h->h_addr_list[0], &addr.sin_addr, h->h_length);
  addr.sin_port = s->s_port;
  
  connect (main_udp_socket,
	   (struct sockaddr *)&addr, sizeof (struct sockaddr_in));

  /* Formulate and send a PMAPPROC_GETPORT request
     to lookup the mount program on the server.  */
  p = pmap_initialize_rpc (PMAPPROC_GETPORT, &rpcbuf);
  *p++ = htonl (MOUNTPROG);
  *p++ = htonl (MOUNTVERS);
  *p++ = htonl (IPPROTO_UDP);
  *p++ = htonl (0);
  errno = conduct_rpc (&rpcbuf, &p);
  if (errno)
    {
      perror ("portmap of mount");
      return 0;
    }
  
  /* Fetch the reply port and clean the RPC  */
  port = ntohl (*p++);
  addr.sin_port = htons (port); /* note: htons and ntohl aren't inverses  */
  free (rpcbuf);

  /* Now talking to the mount program, fetch the file handle
     for the root. */
  connect (main_udp_socket, 
	   (struct sockaddr *) &addr, sizeof (struct sockaddr_in));
  p = mount_initialize_rpc (MOUNTPROC_MNT, &rpcbuf);
  p = xdr_encode_string (p, name);
  errno = conduct_rpc (&rpcbuf, &p);
  if (errno)
    {
      free (rpcbuf);
      perror (name);
      return 0;
    }
  /* XXX Protocol spec says this should be a "unix error code"; we'll
     pretend that an NFS error code is what's meant, the numbers match
     anyhow.  */
  errno = nfs_error_trans (htonl (*p++));
  if (errno)
    {
      free (rpcbuf);
      perror (name);
      return 0;
    }
  
  /* Create the node for root */
  np = lookup_fhandle (p);
  p += NFS_FHSIZE / sizeof (int);
  free (rpcbuf);
  mutex_unlock (&np->lock);

  /* Now send another PMAPPROC_GETPORT request to lookup the nfs server. */
  addr.sin_port = s->s_port;
  connect (main_udp_socket, 
	   (struct sockaddr *) &addr, sizeof (struct sockaddr_in));
  p = pmap_initialize_rpc (PMAPPROC_GETPORT, &rpcbuf);
  *p++ = htonl (NFS_PROGRAM);
  *p++ = htonl (NFS_VERSION);
  *p++ = htonl (IPPROTO_UDP);
  *p++ = htonl (0);
  errno = conduct_rpc (&rpcbuf, &p);
  if (errno)
    port = NFS_PORT;
  else
    port = ntohl (*p++);
  free (rpcbuf);
  
  addr.sin_port = htons (port);
  connect (main_udp_socket, 
	   (struct sockaddr *) &addr, sizeof (struct sockaddr_in));
  
  return np;
}
