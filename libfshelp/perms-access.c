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

/* Check to see whether the user USER can operate on a file identified
   by ST.  OP is one of S_IREAD, S_IWRITE, and S_IEXEC.  If the access
   is permitted, return zero; otherwise return an appropriate error
   code.  */
error_t
fshelp_access (struct stat *st, int op, struct iouser *user)
{
  int gotit;
  if (idvec_contains (user->uids, 0))
    gotit = (op != S_IEXEC) || !S_ISREG(st->st_mode) || (st->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
  else if (user->uids->num == 0 && (st->st_mode & S_IUSEUNK))
    gotit = st->st_mode & (op << S_IUNKSHIFT);
  else if (!fshelp_isowner (st, user))
    gotit = st->st_mode & op;
  else if (idvec_contains (user->gids, st->st_gid))
    gotit = st->st_mode & (op >> 3);
  else
    gotit = st->st_mode & (op >> 6);
  return gotit ? 0 : EACCES;
}
