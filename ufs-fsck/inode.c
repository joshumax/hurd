/* Inode allocation, deallocation, etc.
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

static void
inode_iterate (struct dinode *dp, 
	       int (*fn (daddr_t, int)),
	       int doaddrblocks)
{
  mode_t mode = dp->di_model & IFMT;
  int nb, maxb;
  
  if (mode == IFBLK || mode == IFCHR
      || (mode == IFLNK && sblock->fs_maxsymlinklen != -1
	  && (dp->di_size < sblock->fs_maxsymlinklen
	      || (sblock->fs_maxsymlinklen == 0 && dp->di_blocks == 0))))
    return;

  maxb = howmany (dp->di_size, sblock->fs_bsize);
  for (nb = 0; nb < NDADDR; nb++)
    {
      int offset;
      int nfrags;
      int cont;
      
      if (nb == maxb && (offset = blkoff (sblock, dp->di_size)))
	nfrags = numfrags (sblock, fragroundup (sblock, offset));
      else
	nfrags = sblock->fs_frag;
      
      if (dp->di_db[nb] != 0)
	if ((*fn)(dp->di_db[nb], nfrags) != RET_GOOD)
	  return;
    }
  
  for (nb = 0; nb < NIADDR; nb++)
    if (scaniblock (dp->di_db[nb], nb) != RET_GOOD)
      return;

  if (doaddrblocks && dp->di_trans)
    (*fn)(dp->di_trans, sblock->fs_frag);
}

      
