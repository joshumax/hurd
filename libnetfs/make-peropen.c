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

struct peropen *
netfs_make_peropen (struct node *np, int flags, mach_port_t dotdotport)
{
  struct peropen *po = malloc (sizeof (struct peropen));
  
  po->filepointer = 0;
  po->lock_status = LOCK_UN;
  po->refcnt = 0;
  po->openstat = flags;
  po->np = np;
  po->dotdotport = dotdotport;
  if (dotdotport != MACH_PORT_NULL)
    mach_port_mod_refs (mach_task_self (), dotdotport, 
			MACH_PORT_RIGHT_SEND, 1);
  diskfs_nref (np);
  return po;
}

