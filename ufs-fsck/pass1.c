/* Pass one of GNU fsck -- count blocks and verify inodes
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



#include "fsck.h"

static struct dinode zino;

/* Find all the blocks in use by files and filesystem reserved blocks. 
   Set them in the global block map.  For each file, if a block is found
   allocated twice, then record the block and inode in DUPLIST.
   Initialize INODESTATE, LINKCOUNT, and TYPEMAP. */
void
pass1 ()
{
  ino_t number;
  ino_t i;
  int cg;
  struct dinode dino;
  struct dinode *dp = &dino;
  mode_t mode, type;
  int ndb;
  int holdallblocks;
  int lbn;
  int nblocks;
  int blkerror;
  int nblkrngerrors;
  int nblkduperrors;

  /* This function is called for each block of DP.  Check to see
     if the block number is valid.  If so, set the entry in the
     block map.  If the block map entry is already set, then keep
     track of this block and see if the user wants to clear the
     node.  Increment NBLOCKS by the number of data blocks held.
     Set BLKERROR if this block is invalid.
     Return RET_GOOD, RET_BAD, RET_STOP if the block is good,
     bad, or if we should entirely stop checking blocks in this 
     inode. */
  int
  checkblock (daddr_t bno, int nfrags, off_t offset)
    {
#define MAXBAD 10
      int outofrange;
      struct dups *dlp, *new;
      int wasbad = 0;

      /* Check to see if this block is in range */
      outofrange = check_range (bno, nfrags);
      if (outofrange)
	{
	  blkerror = 1;
	  wasbad = 1;
	  if (nblkrngerrors == 0)
	    warning (0, "I=%d HAS BAD BLOCKS", number);
	  if (nblkrngerrors++ > MAXBAD)
	    {
	      problem (0, "EXCESSIVE BAD BLKS I=%d", number);
	      if (preen || reply ("SKIP"))
		{
		  pfail ("SKIPPING");
		  return RET_STOP;
		}
	    }
	}

      for (; nfrags > 0; bno++, nfrags--)
	{
	  if (outofrange && check_range (bno, 1))
	    warning (0, "BAD BLOCK %lu", bno);
	  else
	    {
	      if (!testbmap (bno))
		setbmap (bno);
	      else
		{
		  blkerror = 1;
		  if (nblkduperrors == 0)
		    warning (0, "I=%d HAS DUPLICATE BLOCKS", number);
		  warning (0, "DUPLICATE BLOCK %ld", bno);
		  wasbad = 1;
		  if (nblkduperrors++ > MAXBAD)
		    {
		      problem (0, "EXCESSIVE DUP BLKS I=%d", number);
		      if (preen || reply ("SKIP"))
			{
			  pfail ("SKIPPING");
			  return RET_STOP;
			}
		    }
		  new = malloc (sizeof (struct dups));
		  new->dup = bno;
		  if (muldup == 0)
		    {
		      duplist = muldup = new;
		      new->next = 0;
		    }
		  else
		    {
		      new->next = muldup->next;
		      muldup->next = new;
		    }
		  for (dlp = duplist; dlp != muldup; dlp = dlp->next)
		    if (dlp->dup == bno)
		      break;
		  if (dlp == muldup && dlp->dup != bno)
		    muldup = new;
		}
	    }
	  nblocks += sblock->fs_fsize / DEV_BSIZE;
	}
      return wasbad ? RET_BAD : RET_GOOD;
    }
  

  /* Account for blocks used by meta data */
  for (cg = 0; cg < sblock->fs_ncg; cg++)
    {
      daddr_t firstdata, firstcgblock, bno;
      
      /* Each cylinder group past the first reserves data
	 from its cylinder group copy to (but not including)
	 the first datablock. 

	 The first, however, reserves from the very front of the
	 cylinder group (thus including the boot block), and it also
	 reserves the data blocks holding the csum information. */
      firstdata = cgdmin (sblock, cg);
      if (cg == 0)
	{
	  firstcgblock = cgbase (sblock, cg);
	  firstdata += howmany (sblock->fs_cssize, sblock->fs_fsize);
	}
      else
	firstcgblock = cgsblock (sblock, cg);

      /* Mark the blocks set */
      for (bno = firstcgblock; bno < firstdata; bno++)
	setbmap (bno);
    }
  
  /* Loop through each inode, doing initial checks */
  for (number = 0, cg = 0; cg < sblock->fs_ncg; cg++)
    for (i = 0; i < sblock->fs_ipg; i++, number++)
      {
	/* These record whether we've already complained about extra
	   direct/indirect blocks.  */
	int dbwarn = 0, ibwarn = 0;

/*	if (!preen && !(number % 10000))
	  printf ("I=%d\n", number); */

	if (number < ROOTINO)
	  continue;
	
	getinode (number, dp);
	mode = DI_MODE (dp);
	type = mode & IFMT;
	
	/* If the node is not allocated, then make sure it's
	   properly clear */
	if (type == 0)
	  {
	    if (bcmp (dp->di_db, zino.di_db, NDADDR * sizeof (daddr_t))
		|| bcmp (dp->di_ib, zino.di_ib, NIADDR * sizeof (daddr_t))
		|| dp->di_trans
		|| DI_MODE (dp)
		|| dp->di_size)
	      {
		problem (0, "PARTIALLY ALLOCATED INODE I=%d", number);
		if (preen || reply ("CLEAR"))
		  {
		    clear_inode (number, dp);
		    pfix ("CLEARED");
		  }
	      }
	    inodestate[number] = UNALLOC;
	  }
	else
	  {
	    /* Node is allocated. */
	    
	    /* Check to see if we think the node should be cleared */

	    /* Verify size for basic validity */
	    holdallblocks = 0;
	    
	    if (dp->di_size + sblock->fs_bsize - 1 < dp->di_size)
	      {
		problem (1, "OVERFLOW IN FILE SIZE I=%d (SIZE == %lld)", number, 
			dp->di_size);
		if (reply ("CLEAR"))
		  {
		    clear_inode (number, dp);
		    inodestate[number] = UNALLOC;
		    continue;
		  }
		inodestate[number] = UNALLOC;
		warning (0, "WILL TREAT ANY BLOCKS HELD BY I=%d AS ALLOCATED",
			number);
		holdallblocks = 1;
	      }

	    /* Decode type and set NDB 
	       also set inodestate correctly. */
	    inodestate[number] = REG;
	    switch (type)
	      {
	      case IFBLK:
	      case IFCHR:
		ndb = 1;
		break;
		    
	      case IFIFO:
	      case IFSOCK:
		ndb = 0;
		break;
		    
	      case IFLNK:
		if (sblock->fs_maxsymlinklen != -1)
		  {
		    /* Check to see if this is a fastlink.  The
		       old fast link format has fs_maxsymlinklen
		       of zero and di_blocks zero; the new format has
		       fs_maxsymlinklen set and we ignore di_blocks. 
		       So check for either. */
		    if ((sblock->fs_maxsymlinklen
			 && dp->di_size < sblock->fs_maxsymlinklen)
			|| (!sblock->fs_maxsymlinklen && !dp->di_blocks))
		      {
			/* Fake NDB value so that we will check
			   all the block pointers past the symlink */
			ndb = howmany (dp->di_size, sizeof (daddr_t));
			if (ndb > NDADDR)
			  {
			    int j = ndb - NDADDR;
			    for (ndb = 1; j > 1; i--)
			      ndb *= NINDIR (sblock);
			    ndb += NDADDR;
			  }
		      }
		    else
		      ndb = howmany (dp->di_size, sblock->fs_bsize);
		  }
		else
		  ndb = howmany (dp->di_size, sblock->fs_bsize);
		break;
		    
	      case IFDIR:
		inodestate[number] = DIRECTORY;
		/* Fall through */
	      case IFREG:
		ndb = howmany (dp->di_size, sblock->fs_bsize);
		break;
		
	      default:
		problem (1, "UNKNOWN FILE TYPE I=%d (MODE=%ol)", number, mode);
		if (reply ("CLEAR"))
		  {
		    clear_inode (number, dp);
		    inodestate[number] = UNALLOC;
		    continue;
		  }
		inodestate[number] = UNALLOC;
		holdallblocks = 1;
		warning (0, "WILL TREAT ANY BLOCKS HELD BY I=%d "
			"AS ALLOCATED", number);
		ndb = 0;
	      }

	    if (ndb < 0)
	      {
		problem (1, "BAD FILE SIZE I= %d (SIZE == %lld)", number,
			dp->di_size);
		if (reply ("CLEAR"))
		  {
		    clear_inode (number, dp);
		    inodestate[number] = UNALLOC;
		    continue;
		  }
		inodestate[number] = UNALLOC;
		warning (0, "WILL TREAT ANY BLOCKS HELD BY I=%d AS ALLOCATED", 
			 number);
		holdallblocks = 1;
	      }

	    /* Make sure that direct and indirect block pointers
	       past the file size are zero.  If the size is bogus, then
	       don't bother (they should all be zero, but the user has
	       requested that they be treated as allocated). */
	    if (!holdallblocks)
	      {
		if (dp->di_size
		    && (type == IFBLK || type == IFCHR 
			|| type == IFSOCK || type == IFIFO))
		  {
		    problem (1, "SPECIAL NODE I=%d (MODE=%ol) HAS SIZE %lld",
			    number, mode, dp->di_size);
		    if (reply ("TRUNCATE"))
		      {
			dp->di_size = 0;
			write_inode (number, dp);
		      }
		  }
		
		/* If we haven't set NDB speciall above, then it is set from
		   the file size correctly by the size check. */
		
		/* Check all the direct and indirect blocks that are past the
		   amount necessary to be zero. */
		for (lbn = ndb; lbn < NDADDR; lbn++)
		  {
		    if (dp->di_db[lbn])
		      {
			if (!dbwarn)
			  {
			    dbwarn = 1;
			    problem (0, "INODE I=%d HAS EXTRA DIRECT BLOCKS", 
				   number);
			    if (preen || reply ("DEALLOCATE"))
			      {
				dp->di_db[lbn] = 0;
				dbwarn = 2;
				pfix ("DEALLOCATED");
			      }
			  }
			else if (dbwarn == 2)
			  dp->di_db[lbn] = 0;
		      }
		    if (dbwarn == 2)
		      write_inode (number, dp);
		  }

		for (lbn = 0, ndb -= NDADDR; ndb > 0; lbn++)
		  ndb /= NINDIR (sblock);
		for (; lbn < NIADDR; lbn++)
		  {
		    if (dp->di_ib[lbn])
		      {
			if (ibwarn)
			  {
			    ibwarn = 1;
			    problem (0, "INODE I=%d HAS EXTRA INDIRECT BLOCKS",
				   number);
			    if (preen || reply ("DEALLOCATE"))
			      {
				dp->di_ib[lbn] = 0;
				ibwarn = 2;
				pfix ("DEALLOCATED");
			      }
			  }
			else if (ibwarn == 2)
			  dp->di_ib[lbn] = 0;
		      }
		    if (ibwarn == 2)
		      write_inode (number, dp);
		  }
	      }
	    
	    /* If this node is really allocated (as opposed to something
	       that we should clear but the user won't) then set LINKCOUNT
	       and TYPEMAP entries. */
	    if (inodestate[number] != UNALLOC)
	      {
		linkcount[number] = dp->di_nlink;
		typemap[number] = IFTODT (mode);
	      }

	    /* Iterate over the blocks of the file,
	       calling CHECKBLOCK for each file. */
	    nblocks = 0;
	    blkerror = 0;
	    nblkduperrors = 0;
	    nblkrngerrors = 0;
	    allblock_iterate (dp, checkblock);

	    if (blkerror)
	      {
		if (preen)
		  warning (1, "DUPLICATE or BAD BLOCKS");
		else
		  {
		    problem (0, "I=%d has ", number);
		    if (nblkduperrors)
		      {
			pextend ("%d DUPLICATE BLOCKS", nblkduperrors);
			if (nblkrngerrors)
			  pextend (" and ");
		      }
		    if (nblkrngerrors)
		      pextend ("%d BAD BLOCKS", nblkrngerrors);
		    if (reply ("CLEAR"))
		      {
			clear_inode (number, dp);
			inodestate[number] = UNALLOC;
			continue;
		      }
		    else if (inodestate[number] == DIRECTORY)
		      inodestate[number] = BADDIR;
		  }
	      }
	    else if (dp->di_blocks != nblocks)
	      {
		problem (0, "INCORRECT BLOCK COUNT I=%d (%ld should be %d)",
			 number, dp->di_blocks, nblocks);
		if (preen || reply ("CORRECT"))
		  {
		    dp->di_blocks = nblocks;
		    write_inode (number, dp);
		    pfix ("CORRECTED");
		  }
	      }

	    num_files++;

	    if (type == IFDIR)
	      record_directory (dp, number);
	  }
      }
}

	    
