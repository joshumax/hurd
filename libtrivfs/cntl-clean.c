/* 
   Copyright (C) 1994, 1996, 1997 Free Software Foundation

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

/* Clean pointers in a struct trivfs_control when its last reference
   vanishes before it's freed. */
void
trivfs_clean_cntl (void *arg)
{
  struct trivfs_control *cntl = arg;
  
  mach_port_destroy (mach_task_self (), cntl->filesys_id);
  mach_port_destroy (mach_task_self (), cntl->file_id);
  mach_port_deallocate (mach_task_self (), cntl->underlying);

  trivfs_remove_control_port_class (cntl->pi.class);
  trivfs_remove_port_bucket (cntl->pi.bucket);
  trivfs_remove_protid_port_class (cntl->protid_class);
  trivfs_remove_port_bucket (cntl->protid_bucket);
}
