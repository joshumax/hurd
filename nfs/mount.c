/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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

/* Using the mount protocol, lookup NAME at host HOST.
   Return a netnode for or null for an error. */
struct netnode *
mount_root (char *name, char *host)
{
  error_t err;
  struct sockaddr_in addr;
  struct hostent *h;
  struct servent *s;
  int *p;
  void *rpcbuf;
  error_t err;
  int port;

  /* Lookup the portmapper port number */
  s = getservbyname ("portmap", "udp");
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
  
  addr->sin_family = h->h_addrtype;
  bcopy (h->h_addr_list[0], &addr.sin_addr, h->h_length);
  addr->sin_port = htons (s->s_port);
  
  /* Formulate and send a PMAPPROC_GETPORT request
     to lookup the mount program on the server.  */
  p = pmap_initialize_rpc (PMAPPROC_GETPORT, &rpcbuf);
  *p++ = htonl (MOUNT_RPC_PROGRAM);
  *p++ = htonl (MOUNT_RPC_VERSION);
  *p++ = htonl (IPPROTO_UDP);
  *p++ = htonl (0);
  err = conduct_rpc (&rpcbuf, &p);
  if (err)
    {
      perror ("portmap of mount");
      return 0;
    }
  
  /* Fetch the reply port and clean the RPC  */
  port = ntohl (*p++);
  addr->sin_port = htons (port); /* note that htons and ntohl are not inverses  */
  free (rpcbuf);
  
  /* Now talking to the mount program, fetch the file handle
     for the root. */
  
  p = mount_initialize_rpc (MOUNTPROC_MNT, &rpcbuf);
  p = xdr_encode_string (p, name);
  err = conduct_rpc (&rpcbuf, &p);
  if (err)
    {
      perror (name);
      return 0;
    }
  
  
