/* Transfer the receive right from one port structure to another
   Copyright (C) 1996, 2003 Free Software Foundation, Inc.
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


#include "ports.h"
#include <assert-backtrace.h>
#include <hurd/ihash.h>

error_t
ports_transfer_right (void *tostruct, 
		      void *fromstruct)
{
  struct port_info *topi = tostruct;
  struct port_info *frompi = fromstruct;
  mach_port_t port;
  int dereffrompi = 0;
  int dereftopi = 0;
  int hassendrights = 0;
  error_t err;

  pthread_mutex_lock (&_ports_lock);

  /* Fetch the port in FROMPI and clear its use */
  port = frompi->port_right;
  if (port != MACH_PORT_NULL)
    {
      pthread_rwlock_wrlock (&_ports_htable_lock);
      hurd_ihash_locp_remove (&_ports_htable, frompi->ports_htable_entry);
      hurd_ihash_locp_remove (&frompi->bucket->htable, frompi->hentry);
      pthread_rwlock_unlock (&_ports_htable_lock);
      frompi->port_right = MACH_PORT_NULL;
      if (frompi->flags & PORT_HAS_SENDRIGHTS)
	{
	  frompi->flags &= ~PORT_HAS_SENDRIGHTS;
	  hassendrights = 1;
	  dereffrompi = 1;
	}
    }
  
  /* Destroy the existing right in TOPI. */
  if (topi->port_right != MACH_PORT_NULL)
    {
      pthread_rwlock_wrlock (&_ports_htable_lock);
      hurd_ihash_locp_remove (&_ports_htable, topi->ports_htable_entry);
      hurd_ihash_locp_remove (&topi->bucket->htable, topi->hentry);
      pthread_rwlock_unlock (&_ports_htable_lock);
      err = mach_port_mod_refs (mach_task_self (), topi->port_right,
				MACH_PORT_RIGHT_RECEIVE, -1);
      assert_perror_backtrace (err);
      if ((topi->flags & PORT_HAS_SENDRIGHTS) && !hassendrights)
	{
	  dereftopi = 1;
	  topi->flags &= ~PORT_HAS_SENDRIGHTS;
	}
      else if (((topi->flags & PORT_HAS_SENDRIGHTS) == 0) && hassendrights)
	{
	  topi->flags |= PORT_HAS_SENDRIGHTS;
	  refcounts_ref (&topi->refcounts, NULL);
	}
    }
  
  /* Install the new right in TOPI. */
  topi->port_right = port;
  topi->cancel_threshold = frompi->cancel_threshold;
  topi->mscount = frompi->mscount;

  pthread_mutex_unlock (&_ports_lock);

  if (port)
    {
      pthread_rwlock_wrlock (&_ports_htable_lock);
      err = hurd_ihash_add (&_ports_htable, port, topi);
      assert_perror_backtrace (err);
      err = hurd_ihash_add (&topi->bucket->htable, port, topi);
      pthread_rwlock_unlock (&_ports_htable_lock);
      assert_perror_backtrace (err);
      /* This is an optimization.  It may fail.  */
      mach_port_set_protected_payload (mach_task_self (), port,
				       (unsigned long) topi);
      if (topi->bucket != frompi->bucket)
        {
	  err = mach_port_move_member (mach_task_self (), port,
				       topi->bucket->portset);
	  assert_perror_backtrace (err);
	}
    }

  /* Take care of any lowered reference counts. */
  if (dereffrompi)
    ports_port_deref (frompi);
  if (dereftopi)
    ports_port_deref (topi);
  return 0;
}
