/* 
   Copyright (C) 1994, 1997 Free Software Foundation

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
#include <sys/file.h>

/* Create and return a new peropen structure on node NP with open
   flags FLAGS.  */
struct peropen *
diskfs_make_peropen (struct node *np, int flags,
		     mach_port_t dotdotport, unsigned depth)
{
  struct peropen *po = malloc (sizeof (struct peropen));
  po->filepointer = 0;
  po->lock_status = LOCK_UN;
  po->refcnt = 0;
  po->openstat = flags;
  po->np = np;
  po->dotdotport = dotdotport;
  po->depth = depth;
  if (dotdotport != MACH_PORT_NULL)
    mach_port_mod_refs (mach_task_self (), dotdotport, 
			MACH_PORT_RIGHT_SEND, 1);
  diskfs_nref (np);
  return po;
}
