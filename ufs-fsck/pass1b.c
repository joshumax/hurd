/* Pass 1b of fsck -- scan inodes for references to duplicate blocks
   Copyright (C) 1994, 1996 Free Software Foundation, Inc.
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

void
pass1b ()
{
  struct dinode dino;
  struct dinode *dp = &dino;
  int cg, i;
  ino_t number;
  int dupblk;
  struct dups *duphead = duplist;

  /* Check each block of file DP; if the block is in the dup block
     list then add it to the dup block list under this file.  
     Return RET_GOOD or RET_BAD if the block is
     good or bad, respectively.  */
  int
  checkblock (daddr_t bno, int nfrags, off_t offset)
    {
      struct dups *dlp;
      int hadbad = 0;
      
      for (; nfrags > 0; bno++, nfrags--)
	{
	  if (check_range (bno, 1))
	    return RET_BAD;
	  for (dlp = duphead; dlp; dlp = dlp->next)
	    {
	      if (dlp->dup == bno)
		{
		  dupblk++;
		  warning (0, "DUPLICATE BLOCK %ld\n", bno);
		  dlp->dup = duphead->dup;
		  duphead->dup = bno;
		  duphead = duphead->next;
		  hadbad = 1;
		}
	      if (dlp == muldup)
		break;
	    }
	}
      return hadbad ? RET_BAD : RET_GOOD;
    }

  /* Call CHECKBLOCK for each block of each node, to see if it holds
     a block already found to be a duplicate. */
  for (cg = 0; cg < sblock->fs_ncg; cg++)
    for (i = 0; i < sblock->fs_ipg; i++, number++)
      {
	if (number < ROOTINO)
	  continue;
	if (inodestate[number] != UNALLOC)
	  {
	    getinode (number, dp);
	    dupblk = 0;
	    allblock_iterate (dp, checkblock);
	    if (dupblk)
	      {
		problem (1, "I=%d HAS %d DUPLICATE BLOCKS", number, dupblk);
		if (reply ("CLEAR"))
		  {
		    clear_inode (number, dp);
		    inodestate[number] = UNALLOC;
		  }
		else if (inodestate[number] == DIRECTORY)
		  inodestate[number] = BADDIR;
	      }
	  }
      }
}

  
