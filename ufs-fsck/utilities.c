/* Miscellaneous functions for fsck
   Copyright (C) 1994 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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

#include "fsck.h"

/* Read disk block ADDR into BUF of SIZE bytes. */
void
readblock (daddr_t addr, void *buf, size_t size)
{
  if (lseek (readfd, addr * DEV_BSIZE, L_SET) == -1)
    errexit ("CANNOT SEEK TO BLOCK %d", addr);
  if (read (readfd, buf, size) != size)
    errexit ("CANNOT READ BLOCK %d", addr);
}

/* Write disk block BLKNO from BUF of SIZE bytes. */
void
writeblock (daddr_t addr, void *buf, size_t size)
{
  if (lseek (writefd, addr * DEV_BSIZE, L_SET) == -1)
    errexit ("CANNOT SEEK TO BLOCK %d", addr);
  if (write (writefd, buf, size) != size)
    errexit ("CANNOT READ BLOCK %d", addr);
}
