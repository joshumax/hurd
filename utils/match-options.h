/* Common functionality for the --test-opts flag of mount and umount.

   Copyright (C) 2013 Free Software Foundation, Inc.
   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <mntent.h>

extern char *test_opts;
extern size_t test_opts_len;

/* Check whether the given mount entry matches the given set of
   options.

   Returns 0 if foo is in the options vector but nofoo is in test_opts.
   Returns 0 if foo is in test_opts but foo is not in the options vector. */
int
match_options (struct mntent *mntent);
