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

#include <argz.h>
#include <error.h>
#include <stdlib.h>
#include <string.h>

#include "match-options.h"

char *test_opts;
size_t test_opts_len;

int
match_options (struct mntent *mntent)
{
  char *opts;
  size_t opts_len;

  error_t err = argz_create_sep (mntent->mnt_opts, ',', &opts, &opts_len);
  if (err)
    error (3, err, "parsing mount options failed");

  for (char *test = test_opts;
       test; test = argz_next (test_opts, test_opts_len, test))
    {
      char *needle = test;
      int inverse = strncmp("no", needle, 2) == 0;
      if (inverse)
        needle += 2;

      int match = 0;
      for (char *opt = opts; opt; opt = argz_next (opts, opts_len, opt))
        {
          if (strcmp (opt, needle) == 0) {
            if (inverse)
              return 0; /* foo in opts, nofoo in test_opts. */

            /* foo in opts, foo in test_opts, record match. */
            match = 1;
          }
        }

      if (! inverse && ! match)
        return 0; /* No foo in opts, but foo in test_opts. */
    }

  /* If no conflicting test_opt was encountered, return success. */
  return 1;
}
