/* 
   Copyright (C) 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, BSG.

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


#include "fshelp.h"

/* Check to see whether USER is allowed to modify DIR with respect to
   existing file ST.  (If there is no existing file, pass 0 for ST.)
   If the access is permissible return 0; otherwise return an
   appropriate error code.  */
error_t
fshelp_checkdirmod (struct stat *dir, struct stat *st, struct iouser *user)
{
  error_t err;

  /* The user must be able to write the directory. */
  err = fshelp_access (dir, S_IWRITE, user);
  if (err)
    return err;

  /* If the directory is sticky, the user must own either it or the file.  */
  if ((dir->st_mode & S_ISVTX) && st
      && fshelp_isowner (dir, user) && fshelp_isowner (st, user))
    return EACCES;

  return 0;
}
