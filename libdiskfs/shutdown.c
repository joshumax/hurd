/*
   Copyright (C) 1993, 1994 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include <hurd/fsys.h>

/* Shutdown the filesystem; flags are as for fsys_goaway. */
error_t 
diskfs_shutdown (int flags)
{
  void sync_trans (struct trans_link *trans, void *arg)
    {
      fsys_goaway (trans->control, (int) arg);
    }
  
  if ((flags & FSYS_GOAWAY_UNLINK)
       && S_ISDIR (diskfs_root_node->dn_stat.st_mode))
    return EBUSY;

  if (flags & FSYS_GOAWAY_RECURSE)
    fshelp_translator_iterate (sync_trans, (void *)flags);
  
  /* XXX doesn't handle GOAWAY_FORCE yet */

  if (!(flags & FSYS_GOAWAY_NOSYNC))
    {
      diskfs_shutdown_pager ();
      diskfs_set_hypermetadata (1, 1);
    }
  return 0;
}
