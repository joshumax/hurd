/* 
   Copyright (C) 1994 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "trans.h"

/* Implement fshelp_fetch_root; see <hurd/fshelp.h> for the description. */
error_t
fshelp_fetch_root (struct trans_link *link, mach_port_t *cntl,
		   int passive, error_t (*passive_fn) (char **, u_int *),
		    void *dirpt, void *nodept, 
		    mach_port_t (*genpt_fn) (mach_port_t),
		    struct mutex *unlock)
{
  mach_port_t cwdirpt, nodept;
  char buf[1000];
  char *bufp = buf;
  u_int buflen = 1000;
  mutex_lock (&link->lock);
  if (!passive && link->control == MACH_PORT_NULL)
    {
      mutex_unlock (&link->lock);
      return 0;
    }
  
  cwdirpt = (*genpt_fn)(dirpt);

  if (link->control == MACH_PORT_NULL)
    {  
      /* Start passive translator */
      nodept = (*genpt_fn)(nodept);
  
      mutex_unlock (unlock);
  
      error = fshelp_start_translator (link, 

		   
