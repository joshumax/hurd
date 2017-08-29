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
#include "trivfs_fsys_S.h"

/* Return any active child translators.  NAMES is an argz vector
   containing file names relative to the root of the translator.
   CONTROLS is an array containing the corresponding control ports.
   Note that translators are bound to nodes, and nodes can have zero
   or more links in the file system, therefore there is no guarantee
   that a translators name refers to an existing link in the file
   system.  */
error_t
trivfs_S_fsys_get_children (struct trivfs_control *fsys,
			    mach_port_t reply,
			    mach_msg_type_name_t replyPoly,
			    char **names,
			    mach_msg_type_number_t *names_len,
                            mach_port_t **controls,
                            mach_msg_type_name_t *controlsPoly,
			    mach_msg_type_number_t *controlsCnt)
{
  return EOPNOTSUPP;
}
