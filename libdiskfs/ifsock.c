/* Implement ifsock inteface
   Copyright (C) 1994 Free Software Foundation

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

#include "priv.h"
#include "ifsock_S.h"
#include <hurd/paths.h>
#include <sys/socket.h>

spin_lock_t pflocalserverlock = SPIN_LOCK_INITIALIZER;
mach_port_t pflocalserver = MACH_PORT_NULL;

S_ifsock_getsockaddr (struct protid *cred,
		      mach_port_t *address)
{
  error_t err;
  struct node *np;
  
  /* Make sure this is a socket */
  if (!cred)
    return EOPNOTSUPP;
  np = cred->po->np;
  
 retry:
  mutex_lock (&np->lock);
  if ((np->dn_stat.st_mode & S_IFMT) != S_IFSOCK)
    {
      mutex_unlock (&np->lock);
      return EOPNOTSUPP;
    }
  err = diskfs_access (np, S_IREAD, cred);
  if (err)
    {
      mutex_unlock (&np->lock);
      return err;
    }

  if (np->sockaddr == MACH_PORT_NULL)
    {
      mach_port_t server;

      /* Fetch a port to the PF_LOCAL server, caching it. */

      spin_lock (&pflocalserverlock);
      if (pflocalserver == MACH_PORT_NULL)
	{
	  /* Find out who the PF_LOCAL server is.  Unfortunately,
	     we have to release our lock on NP to do this lookup
	     with maximal safety, so after we find out who the
	     server is, we have to loop back. */
	  char buf[100];

	  spin_unlock (&pflocalserverlock);
	  mutex_unlock (&np->lock);

	  /* Look it up */
	  sprintf (buf, "%s/%d", _SERVERS_SOCKET, PF_LOCAL);
	  server = path_lookup (buf, 0);
	  if (server == MACH_PORT_NULL)
	    return EIEIO;

	  /* Set it unless someone is already here */
	  spin_lock (&pflocalserverlock);
	  if (pflocalserver != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), server);
	  else
	    pflocalserver = server;
	  spin_unlock (&pflocalserverlock);
	  
	  goto retry;
	}
      server = pflocalserver;
      spin_unlock (&pflocalserverlock);
      
      /* Create an address for the node */
      err = socket_fabricate_address (server, &np->sockaddr);
      if (err)
	{
	  mutex_unlock (&np->lock);
	  return EIEIO;
	}
    }      
  
  *address = np->sockaddr;
  mutex_unlock (&np->lock);
  return 0;
}
      
