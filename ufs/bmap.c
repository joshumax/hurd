/* Interpretation of indirect block structure
   Copyright (C) 1994, 1995, 1996 Free Software Foundation, Inc.
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

#include "ufs.h"

/* For logical block number LBN of file NP, look it the block address,
   giving the "path" of indirect blocks to the file, starting 
   with the least indirect.  Fill *INDIRS with information for
   the block.  */
error_t
fetch_indir_spec (struct node *np, volatile daddr_t lbn,
		  struct iblock_spec *indirs)
{
  struct dinode *di = dino (np->dn->number);
  error_t err;
  daddr_t *siblock;
  
  err = diskfs_catch_exception ();
  if (err)
    return err;
  
  indirs[0].offset = -2;
  indirs[1].offset = -2;
  indirs[2].offset = -2;
  indirs[3].offset = -2;

  if (lbn < NDADDR)
    {
      if (lbn >= 0)
	{
	  indirs[0].bno = read_disk_entry (di->di_db[lbn]);
	  indirs[0].offset = -1;
	}
  
      diskfs_end_catch_exception ();
      return 0;
    }

  lbn -= NDADDR;

  indirs[0].offset = lbn % NINDIR (sblock);
  
  if (lbn / NINDIR (sblock))
    {
      /* We will use the double indirect block */
      int ibn;
      daddr_t *diblock;

      ibn = lbn / NINDIR (sblock) - 1;

      indirs[1].offset = ibn % NINDIR (sblock);
      
      /* We don't support triple indirect blocks, but this 
	 is where we'd do it. */
      assert (!(ibn / NINDIR (sblock)));
  
      indirs[2].offset = -1;
      indirs[2].bno = read_disk_entry (di->di_ib[INDIR_DOUBLE]);

      if (indirs[2].bno)
	{
	  diblock = indir_block (indirs[2].bno);
	  indirs[1].bno = read_disk_entry (diblock[indirs[1].offset]);
	}
      else
	indirs[1].bno = 0;
    }
  else
    {
      indirs[1].offset = -1;
      indirs[1].bno = read_disk_entry (di->di_ib[INDIR_SINGLE]);
    }

  if (indirs[1].bno)
    {
      siblock = indir_block (indirs[1].bno);
      indirs[0].bno = read_disk_entry (siblock[indirs[0].offset]);
    }
  else
    indirs[0].bno = 0;

  diskfs_end_catch_exception ();
  return 0;
}


/* Mark indirect block BNO as dirty on node NP's list.  NP must
   be locked. */
void
mark_indir_dirty (struct node *np, daddr_t bno)
{
  struct dirty_indir *d;
  
  for (d = np->dn->dirty; d; d = d->next)
    if (d->bno == bno)
      return;
  
  d = malloc (sizeof (struct dirty_indir));
  d->bno = bno;
  d->next = np->dn->dirty;
  np->dn->dirty = d;
}

