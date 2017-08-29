/* fsys_get_children

   Copyright (C) 2017 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include "priv.h"
#include "fsys_S.h"

#include <argz.h>

/* Return any active child translators.  NAMES is an argz vector
   containing file names relative to the root of the translator.
   CONTROLS is an array containing the corresponding control ports.
   Note that translators are bound to nodes, and nodes can have zero
   or more links in the file system, therefore there is no guarantee
   that a translators name refers to an existing link in the file
   system.  */
error_t
netfs_S_fsys_get_children (struct netfs_control *fsys,
			   mach_port_t reply,
			   mach_msg_type_name_t reply_type,
			   char **names,
			   mach_msg_type_number_t *names_len,
			   mach_port_t **controls,
			   mach_msg_type_name_t *controlsPoly,
			   mach_msg_type_number_t *controlsCnt)
{
  error_t err;
  char *n = NULL;
  size_t n_len = 0;
  mach_port_t *c;
  size_t c_count;

  if (! fsys)
    return EOPNOTSUPP;


  err = fshelp_get_active_translators (&n, &n_len, &c, &c_count);
  if (err)
    goto errout;

  err = iohelp_return_malloced_buffer (n, n_len, names, names_len);
  if (err)
    goto errout;
  n = NULL; /* n was freed by iohelp_return_malloced_buffer. */

  err = iohelp_return_malloced_buffer ((char *) c, c_count * sizeof *c,
                                       (char **) controls, controlsCnt);
  if (err)
    goto errout;
  c = NULL; /* c was freed by iohelp_return_malloced_buffer. */

  *controlsPoly = MACH_MSG_TYPE_MOVE_SEND;
  *controlsCnt = c_count;

 errout:
  free (n);
  free (c);
  return err;
}
