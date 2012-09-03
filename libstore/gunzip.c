/* Decompressing store backend

   Copyright (C) 1997, 1999, 2002 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <sys/mman.h>

#include "store.h"

/* gzip.h makes several annoying defines & decls, which we have to work
   around. */
#define file_t gzip_file_t
#include "gzip.h"
#undef file_t
#undef head

static error_t
DO_UNZIP (void)
{
  /* Entry points to unzip engine.  */
  int get_method (int);
  extern long int bytes_out;

  if (get_method (0) != 0)
    /* Not a happy gzip file.  */
    return EINVAL;

  /* Matched gzip magic number.  Ready to unzip.
     Set up the output stream and let 'er rip.  */
  bytes_out = 0;
  unzip (17, 23);		/* Arguments ignored.  */
  return 0;
}

#define UNZIP		gunzip
#include "unzipstore.c"
