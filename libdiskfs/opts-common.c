/* Options common to both startup and runtime

   Copyright (C) 1995, 1996, 1997 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <argp.h>

const struct argp_option diskfs_common_options[] =
{
  {"readonly", 'r', 0, 0, "Never write to disk or allow opens for writing"},
  {"rdonly",   0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"writable", 'w', 0, 0, "Use normal read/write behavior"},
  {"rdwr",     0,   0, OPTION_ALIAS | OPTION_HIDDEN},
  {"sync",     's', "INTERVAL", OPTION_ARG_OPTIONAL,
     "If INTERVAL is supplied, sync all data not actually written to disk"
     " every INTERVAL seconds, otherwise operate in synchronous mode (the"
     " default is to sync every 30 seconds)"},
  {"no-sync",  'n',  0, 0, "Don't automatically sync data to disk"},
  {"nosync", 0, 0, OPTION_ALIAS | OPTION_HIDDEN},
  {"no-suid", 'S', 0, 0, "Don't permit set-uid or set-gid execution"},
  {"no-exec", 'E', 0, 0, "Don't permit any execution of files on this filesystem"},
  {0, 0}
};
