/* Pass one of GNU fsck
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

/* Find all the blocks in use by files and filesystem reserved blocks. 
   Set them in the global block map.  For each file, if a block is found
   allocated twice, then record the block and inode in DUPLIST.
   Set ISTATE to be USTATE, FSTATE, or DSTATE as appropriate */
pass1 ()
{
  ino_t number;			/* inode number */
  ino_t i;			/* cg-relative inode number */
  int cg;			/* cylinder group number */

  /* Account for blocks used by meta data */
  for (cg = 0, cg < sblock.fs_ncg; cg++)
    {
      daddr_t firstdata, firstcgblock, bno;
      
      /* Each cylinder group past the first reserves data
	 from its cylinder group copy to (but not including)
	 the first datablock. 

	 The first, however, reserves from the very front of the
	 cylinder group (thus including the boot block), and it also
	 reserves the data blocks holding the csum information. */
      firstdata = cgdmin (&sblock, cg);
      if (cg == 0)
	{
	  firstcgblock = cgbase (&sblock, cg);
	  firstdata += howmany (sblock.fs_cssize, sblock.fs_fsize);
	}
      else
	firstdata = cgsblock (&sblock, cg);

      /* Mark the blocks set */
      for (bno = firstcgblock; bno < firstdata; bno++)
	set_block_used (bno):
    }
  
  /* Loop through each inode, doing initial checks */
  for (number = 0, cg = 0; cg < sblock.fs_ncg; cg++)
    for (i = 0; i < sblock.fs_ipg; i++, number++)
      {
	struct dinode *dp;
	mode_t mode;

	dp = getinode (number);
	mode = DI_MODE (dp) & IFMT;
	
	/* If the node is not allocated, then make sure it's
	   properly clear */
	if (mode == 0)
	  {
	    if (bcmp (dp->di_db, zino.di_db, NDADDR * sizeof (daddr_t))
		|| bcmp (dp->di_ib, zino->di_ix, NIADDR * sizeof (daddr_t))
		|| dp->di_trans
		|| DI_MODE (dp)
		|| dp->di_size)
	      {
		pwarn ("PARTIALLY ALLOCATED INODE I=%lu", number);
		if (preen || reply ("CLEAR"))
		  {
		    if (preen)
		      printf (" (CLEARED)\n");
		    clear_inode (dp);
		  }
		
		    
		    
		    



		    
