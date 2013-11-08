/* Implement ifsock inteface
   Copyright (C) 1994, 1996 Free Software Foundation

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
#include <stdio.h>
#include <hurd/socket.h>

static pthread_spinlock_t pflocalserverlock = PTHREAD_SPINLOCK_INITIALIZER;
static mach_port_t pflocalserver = MACH_PORT_NULL;

kern_return_t
diskfs_S_ifsock_getsockaddr (struct protid *cred,
			     mach_port_t *address)
{
  error_t err;
  struct node *np;
  unsigned restart_tries = 0;
  
  /* Make sure this is a socket */
  if (!cred)
    return EOPNOTSUPP;
  np = cred->po->np;
  
 retry:
  pthread_mutex_lock (&np->lock);
  if ((np->dn_stat.st_mode & S_IFMT) != S_IFSOCK)
    {
      pthread_mutex_unlock (&np->lock);
      return EOPNOTSUPP;
    }
  err = fshelp_access (&np->dn_stat, S_IWRITE, cred->user);
  if (err)
    {
      pthread_mutex_unlock (&np->lock);
      return err;
    }

  if (np->sockaddr == MACH_PORT_NULL)
    {
      mach_port_t server;
      mach_port_t sockaddr;
      mach_port_t old;

      pthread_mutex_unlock (&np->lock);

      /* Fetch a port to the PF_LOCAL server, caching it. */

      pthread_spin_lock (&pflocalserverlock);
      if (pflocalserver == MACH_PORT_NULL)
	{
	  /* Find out who the PF_LOCAL server is. */
	  char buf[100];

	  pthread_spin_unlock (&pflocalserverlock);

	  /* Look it up */
	  sprintf (buf, "%s/%d", _SERVERS_SOCKET, PF_LOCAL);
	  server = file_name_lookup (buf, 0, 0);
	  if (server == MACH_PORT_NULL)
	    return EIEIO;

	  /* Set it unless someone is already here */
	  pthread_spin_lock (&pflocalserverlock);
	  if (pflocalserver != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), server);
	  else
	    pflocalserver = server;
	  pthread_spin_unlock (&pflocalserverlock);
	  
	  goto retry;
	}
      server = pflocalserver;
      pthread_spin_unlock (&pflocalserverlock);
      
      /* Create an address for the node */
      err = socket_fabricate_address (server, AF_LOCAL, &sockaddr);
      if ((err == MACH_SEND_INVALID_DEST || err == MIG_SERVER_DIED)
	  && restart_tries++ == 0)
	/* The PF_LOCAL server died; try to restart it.  */
	{
	  pthread_spin_lock (&pflocalserverlock);
	  if (pflocalserver == server)
	    pflocalserver = MACH_PORT_NULL;
	  pthread_spin_unlock (&pflocalserverlock);
	  goto retry;
	}
      if (err)
	return EIEIO;

      pthread_mutex_lock (&np->lock);
      if (np->sockaddr != MACH_PORT_NULL)
	/* Someone beat us */
	mach_port_deallocate (mach_task_self (), sockaddr);
      else
	{
	  /* The receive right of the sockaddr holds a reference;
	     when we get a dead name on that right we drop our
	     reference. */
	  mach_port_request_notification (mach_task_self (), sockaddr,
					  MACH_NOTIFY_DEAD_NAME, 1,
					  cred->pi.port_right, 
					  MACH_MSG_TYPE_MAKE_SEND_ONCE,
					  &old);
	  if (old != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), old);
	  np->sockaddr = sockaddr;
	  diskfs_nref_light (np);
	}
    }      
  
  *address = np->sockaddr;
  pthread_mutex_unlock (&np->lock);
  return 0;
}
