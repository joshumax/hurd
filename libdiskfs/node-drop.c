/* 
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

/* Node NP now has no more references; clean all state.  The
   _diskfs_node_refcnt_lock must be held.  */
void
diskfs_drop_node (struct node *np)
{
  fshelp_kill_translator (&np->translator);
  if (np->dn_stat.st_nlink == 0)
    {
      assert (!readonly);
      diskfs_node_truncate (np, 0);
      np->dn_stat.st_mode = 0;
      np->dn_stat.st_rdev = 0;
      np->dn_set_ctime = np->dn_set_atime = 1;
      node_update (np, 1);
      diskfs_free_disknode (np);
    }
}

      
