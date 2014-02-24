/*
   Copyright (C) 1995,96,2000 Free Software Foundation, Inc.
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

#include "pfinet.h"
#include <string.h>
#include <stddef.h>

#include <linux/socket.h>
#include <linux/net.h>


/* Create a sockaddr port.  Fill in *ADDR and *ADDRTYPE accordingly.
   The address should come from SOCK; PEER is 0 if we want this socket's
   name and 1 if we want the peer's name. */
error_t
make_sockaddr_port (struct socket *sock,
		    int peer,
		    mach_port_t *addr,
		    mach_msg_type_name_t *addrtype)
{
  union { struct sockaddr_storage storage; struct sockaddr sa; } buf;
  int buflen = sizeof buf;
  error_t err;
  struct sock_addr *addrstruct;

  err = (*sock->ops->getname) (sock, &buf.sa, &buflen, peer);
  if (err)
    return -err;

  err = ports_create_port (addrport_class, pfinet_bucket,
			   (offsetof (struct sock_addr, address)
			    + buflen), &addrstruct);
  if (!err)
    {
      addrstruct->address.sa_family = buf.sa.sa_family;
      addrstruct->address.sa_len = buflen;
      memcpy (addrstruct->address.sa_data, buf.sa.sa_data,
	      buflen - offsetof (struct sockaddr, sa_data));
      *addr = ports_get_right (addrstruct);
      *addrtype = MACH_MSG_TYPE_MAKE_SEND;
    }

  ports_port_deref (addrstruct);

  return 0;
}

/* Nothing need be done here. */
void
clean_addrport (void *arg)
{
}
