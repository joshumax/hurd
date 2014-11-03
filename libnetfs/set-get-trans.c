/* Default versions of netfs_set_translator & netfs_get_translator

   Copyright (C) 1997 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include "netfs.h"

/* Default implementations of the netfs_set_translator and
   netfs_set_translator functions. */

/* The user may define this function.  Attempt to set the passive
   translator record for FILE to ARGZ (of length ARGZLEN) for user
   CRED. */
error_t __attribute__ ((weak))
netfs_set_translator (struct iouser *cred, struct node *np,
		      char *argz, size_t argzlen)
{
  return EOPNOTSUPP;
}

/* The user may define this function (but should define it together with
   netfs_set_translator).  For locked node NODE with S_IPTRANS set in its
   mode, look up the name of its translator.  Store the name into newly
   malloced storage, and return it in *ARGZ; set *ARGZ_LEN to the total
   length.  */
error_t __attribute__ ((weak))
netfs_get_translator (struct node *node, char **argz, size_t *argz_len)
{
  return EOPNOTSUPP;
}
