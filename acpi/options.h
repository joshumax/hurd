/*
   Copyright (C) 2018 Free Software Foundation, Inc.

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
   along with the GNU Hurd.  If not, see <<a rel="nofollow" href="http://www.gnu.org/licenses/">http://www.gnu.org/licenses/</a>>.
*/

/* Command line option parsing */

#ifndef OPTIONS_H
#define OPTIONS_H

#include <stdint.h>
#include <sys/types.h>
#include <argp.h>

#include <acpifs.h>

#define STR2(x)  #x
#define STR(x)  STR2(x)

/* Used to hold data during argument parsing.  */
struct parse_hook
{
  struct acpifs_perm perm;
  size_t ncache_len;
};

/* ACPI translator options.  Used for both startup and runtime.  */
static const struct argp_option options[] = {
  {0, 0, 0, 0, "These apply to the whole acpi tree:", 1},
  {"uid", 'U', "UID", 0, "User ID to give permissions to"},
  {"gid", 'G', "GID", 0, "Group ID to give permissions to"},
  {0}
};

static const char doc[] = "Permissions on acpi are currently global.";

#endif // OPTIONS_H
