/* Pass 2 of GNU fsck -- examine all directories for validity
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

/* Verify root inode's allocation and check all directories for
   viability.  Set DIRSORTED array fully and check to make sure
   each directory has a correct . and .. in it.  */
void
pass2 ()
{
  int nd;
  struct dirinfo *dnp;
  struct dinode dino;

  /* Return negative, zero, or positive according to the
     ordering of the first data block of **DNP1 and **DNP2. */
  int
  sortfunc (const void *ptr1, const void *ptr2)
    {
      struct dirinfo * const *dnp1 = ptr1;
      struct dirinfo * const *dnp2 = ptr2;
      return ((*dnp1)->i_blks[0] - (*dnp2)->i_blks[0]);
    }

  /* Called for each DIRBLKSIZ chunk of the directory.
     BUF is the data of the directory block.  Return
     1 if this block has been modified and should be written
     to disk; otherwise return 0.  */
  int
  check1block (void *buf)
    {
      struct directory_entry *dp;
      int mod = 0;
      u_char namlen;
      char type;
      int i;

      for (dp = buf; (void *)dp - buf < DIRBLKSIZ;
	   dp = (struct directory_entry *) ((void *)dp + dp->d_reclen))
	{
	  /* Check RECLEN for basic validity */
	  if (dp->d_reclen == 0
	      || dp->d_reclen + (void *)dp - buf > DIRBLKSIZ)
	    {
	      /* Perhaps the entire dir block is zero.  UFS does that
		 when extending directories.  So allow preening
		 to safely patch up all-null dir blocks. */
	      if (dp == buf)
		{
		  char *bp;
		  for (bp = (char *)buf; bp < (char *)buf + DIRBLKSIZ; bp++)
		    if (*bp)
		      goto reclen_problem;
		  
		  problem (0, "NULL BLOCK IN DIRECTORY");
		  if (preen || reply ("PATCH"))
		    {
		      /* Mark this entry free, and return. */
		      dp->d_ino = 0;
		      dp->d_reclen = DIRBLKSIZ;
		      pfix ("PATCHED");
		      return 1;
		    }
		  else
		    return mod;
		}
	      
	    reclen_problem:
	      problem (1, "BAD RECLEN IN DIRECTORY");
	      if (reply ("SALVAGE"))
		{
		  /* Skip over everything else in this dirblock;
		     mark this entry free. */
		  dp->d_ino = 0;
		  dp->d_reclen = DIRBLKSIZ - ((void *)dp - buf);
		  return 1;
		}
	      else
		/* But give up regardless */
		return mod;
	    }
	  
	  /* Check INO */
	  if (dp->d_ino > maxino)
	    {
	      problem (1, "BAD INODE NUMBER IN DIRECTORY");
	      if (reply ("SALVAGE"))
		{
		  /* Mark this entry clear */
		  dp->d_ino = 0;
		  mod = 1;
		}
	    }

	  if (!dp->d_ino)
	    continue;
	  
	  /* Check INO */
	  if (inodestate[dp->d_ino] == UNALLOC)
	    {
	      pinode (0, dnp->i_number, "REF TO UNALLOCATED NODE IN");
	      if (preen || reply ("REMOVE"))
		{
		  dp->d_ino = 0;
		  mod = 1;
		  pfix ("REMOVED");
		  continue;
		}
	    }

	  /* Check NAMLEN */
	  namlen = DIRECT_NAMLEN (dp);
	  if (namlen > MAXNAMLEN)
	    {
	      problem (1, "BAD NAMLEN IN DIRECTORY");
	      if (reply ("SALVAGE"))
		{
		  /* Mark this entry clear */
		  dp->d_ino = 0;
		  mod = 1;
		}
	    }		  
	  else
	    {
	      /* Check for illegal characters */
	      for (i = 0; i < DIRECT_NAMLEN (dp); i++)
		if (dp->d_name[i] == '\0' || dp->d_name[i] == '/')
		  {
		    problem (1, "ILLEGAL CHARACTER IN FILE NAME");
		    if (reply ("SALVAGE"))
		      {
			/* Mark this entry clear */
			dp->d_ino = 0;
			mod = 1;
			break;
		      }
		  }
	      if (dp->d_name[DIRECT_NAMLEN (dp)])
		{
		  problem (1, "DIRECTORY NAME NOT TERMINATED");
		  if (reply ("SALVAGE"))
		    {
		      /* Mark this entry clear */
		      dp->d_ino = 0;
		      mod = 1;
		    }
		}
	    }
	  
	  if (!dp->d_ino)
	    continue;

	  /* Check TYPE */
	  type = DIRECT_TYPE (dp);
	  if (type != DT_UNKNOWN && type != typemap[dp->d_ino])
	    {
	      problem (0, "INCORRECT NODE TYPE IN DIRECTORY");
	      if (preen || reply ("CLEAR"))
		{
		  pfix ("CLEARED");
		  dp->d_type = 0;
		  mod = 1;
		}
	    }
	  
	  /* Here we should check for duplicate directory entries;
	     that's too much trouble right now. */
	  
	  /* Account for the inode in the linkfound map */
	  if (inodestate[dp->d_ino] != UNALLOC)
	    linkfound[dp->d_ino]++;

	   if (inodestate[dp->d_ino] == DIRECTORY
	      || inodestate[dp->d_ino] == BADDIR)
	    {
	      if (DIRECT_NAMLEN (dp) == 1 && dp->d_name[0] == '.')
		dnp->i_dot = dp->d_ino;
	      else if (DIRECT_NAMLEN (dp) == 2
		       && dp->d_name[0] == '.' && dp->d_name[1] == '.')
		dnp->i_dotdot = dp->d_ino;
	      else
		{
		  struct dirinfo *targetdir;
		  targetdir = lookup_directory (dp->d_ino);
		  if (targetdir->i_parent)
		    {
		      problem (0, "EXTRANEOUS LINK `%s' TO DIR I=%ld",
			       dp->d_name, dp->d_ino);
		      pextend (" FOUND IN DIR I=%d", dnp->i_number);
		      if (preen || reply ("REMOVE"))
			{
			  dp->d_ino = 0;
			  mod = 1;
			  pfix ("REMOVED");
			}
		    }
		  else
		    targetdir->i_parent = dnp->i_number;
		}
	    }
	}
      return mod;
    }

  /* Called for each filesystem block of the directory.  Load BNO
     into core and then call CHECK1BLOCK for each DIRBLKSIZ chunk.
     OFFSET is the offset this block occupies ithe file.
     Always return 1.  */
  int
  checkdirblock (daddr_t bno, int nfrags, off_t offset)
    {
      void *buf = alloca (nfrags * sblock->fs_fsize);
      void *bufp;
      int rewrite;

      readblock (fsbtodb (sblock, bno), buf, nfrags * sblock->fs_fsize);
      rewrite = 0;
      for (bufp = buf; 
	   bufp - buf < nfrags * sblock->fs_fsize
	   && offset + (bufp - buf) + DIRBLKSIZ <= dnp->i_isize; 
	   bufp += DIRBLKSIZ)
	{
	  if (check1block (bufp))
	    rewrite = 1;
	}
      if (rewrite)
	writeblock (fsbtodb (sblock, bno), buf, nfrags * sblock->fs_fsize);
      return 1;
    }

  switch (inodestate [ROOTINO])
    {
    default:
      errexit ("BAD STATE %d FOR ROOT INODE", (int) (inodestate[ROOTINO]));
      
    case DIRECTORY:
      break;

    case UNALLOC:
      problem (1, "ROOT INODE UNALLOCATED");
      if (!reply ("ALLOCATE"))
	errexit ("ABORTING");
      if (allocdir (ROOTINO, ROOTINO, 0755) != ROOTINO)
	errexit ("CANNOT ALLOCATE ROOT INODE");
      break;
      
    case REG:
      problem (1, "ROOT INODE NOT DIRECTORY");
      if (reply ("REALLOCATE"))
	freeino (ROOTINO);
      if (allocdir (ROOTINO, ROOTINO, 0755) != ROOTINO)
	errexit ("CANNOT ALLOCATE ROOT INODE");
      break;
      
    case BADDIR:
      problem (1, "DUPLICATE or BAD BLOCKS IN ROOT INODE");
      if (reply ("REALLOCATE"))
	{
	  freeino (ROOTINO);
	  if (allocdir (ROOTINO, ROOTINO, 0755) != ROOTINO)
	    errexit ("CANNOT ALLOCATE ROOT INODE");
	}
      if (reply ("CONTINUE") == 0)
	errexit ("ABORTING");
      break;
    }
  
  /* Sort inpsort */
  qsort (dirsorted, dirarrayused, sizeof (struct dirinfo *), sortfunc);
  
  /* Check basic integrity of each directory */
  for (nd = 0; nd < dirarrayused; nd++)
    {
      dnp = dirsorted[nd];
      
      if (dnp->i_isize == 0)
	continue;
      if (dnp->i_isize % DIRBLKSIZ)
	{
	  problem (0, "DIRECTORY INO=%d: LENGTH %d NOT MULTIPLE OF %d",
		   dnp->i_number, dnp->i_isize, DIRBLKSIZ);
	  if (preen || reply ("ADJUST"))
	    {
	      getinode (dnp->i_number, &dino);
	      dino.di_size = roundup (dnp->i_isize, DIRBLKSIZ);
	      write_inode (dnp->i_number, &dino);
	      pfix ("ADJUSTED");
	    }
	}
      bzero (&dino, sizeof (struct dinode));
      dino.di_size = dnp->i_isize;
      bcopy (dnp->i_blks, dino.di_db, dnp->i_numblks);
      
      datablocks_iterate (&dino, checkdirblock);
    }
  

  /* At this point for each directory:
     If this directory is an entry in another directory, then i_parent is
       set to that node's number.
     If this directory has a `..' entry, then i_dotdot is set to that link.
     Check to see that `..' is set correctly. */
  for (nd = 0; nd < dirarrayused; nd++)
    {
      dnp = dirsorted[nd];
      
      /* Root is considered to be its own parent even though it isn't
	 listed. */
      if (dnp->i_number == ROOTINO && !dnp->i_parent)
	dnp->i_parent = ROOTINO;
	  
      /* Check `.' to make sure it exists and is correct */
      if (dnp->i_dot == 0)
	{
	  dnp->i_dot = dnp->i_number;
	  pinode (0, dnp->i_number, "MISSING `.' IN");
	  if ((preen || reply ("FIX"))
	       && makeentry (dnp->i_number, dnp->i_number, "."))
	    {
	      linkfound[dnp->i_number]++;
	      pfix ("FIXED");
	    }
	  else
	    pfail (0);
	}
      else if (dnp->i_dot != dnp->i_number)
	{
	  pinode (0, dnp->i_number, "BAD INODE NUMBER FOR `.' IN");
	  if (preen || reply ("FIX"))
	    {
	      ino_t old_dot = dnp->i_dot;
	      dnp->i_dot = dnp->i_number;
	      if (changeino (dnp->i_number, ".", dnp->i_number))
		{
		  linkfound[dnp->i_number]++;
		  if (inodestate[old_dot] != UNALLOC)
		    linkfound[old_dot]--;
		  pfix ("FIXED");
		}
	      else
		pfail (0);
	    }
	}

      /* Check `..' to make sure it exists and is correct */
      if (dnp->i_parent && dnp->i_dotdot == 0)
	{
	  dnp->i_dotdot = dnp->i_parent;
	  pinode (0, dnp->i_number, "MISSING `..' IN");
	  if ((preen || reply ("FIX"))
	      && makeentry (dnp->i_number, dnp->i_parent, ".."))
	    {
	      linkfound[dnp->i_parent]++;
	      pfix ("FIXED");
	    }
	  else
	    pfail (0);
	}
      else if (dnp->i_parent && dnp->i_dotdot != dnp->i_parent)
	{
	  pinode (0, dnp->i_number, "BAD INODE NUMBER FOR `..' IN");
	  if (preen || reply ("FIX"))
	    {
	      ino_t parent = dnp->i_parent, old_dotdot = dnp->i_dotdot;
	      dnp->i_dotdot = parent;
	      if (changeino (dnp->i_number, "..", parent))
		/* Adjust what the parent's link count should be; the actual
		   count will be corrected in an later pass.  */
		{
		  linkfound[parent]++;
		  if (inodestate[old_dotdot] != UNALLOC)
		    linkfound[old_dotdot]--;
		  pfix ("FIXED");
		}
	      else
		pfail (0);
	    }
	}
    }
}  
	      
