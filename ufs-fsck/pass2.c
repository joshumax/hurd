/* Pass 2 of GNU fsck -- examine all directories for validity
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


/* Verify root inode's basic validity; then traverse all directories
   starting at root and mark those we find in inodetype with DIR_REF. */
pass2 ()
{
  int nd;
  struct dirinfo *dnp;
  struct dinode dino, *di = &dino;

  switch (statemap [ROOTINO])
    {
    default:
      errexit ("BAD STATE %d FOR ROOT INODE", statemap [ROOTINO]);
      
    case DIR:
      break;

    case UNALLOC:
      pfatal ("ROOT INODE UNALLOCATED");
      if (!reply ("ALLOCATE"))
	errexit ("");
      if (allocdir (ROOTINO, ROOTINO, 0755) != ROOTINO)
	errexit ("CANNOT ALLOCATE ROOT INODE\n");
      break;
      
    case FILE:
      pfatal ("ROOT INODE NOT DIRECTORY");
      if (reply ("REALLOCATE"))
	freeino (ROOTINO);
      if (allocdir (ROOTINO, ROOTINO, 0755) != ROOTINO)
	errexit ("CANNOT ALLOCATE ROOT INODE\n");
      break;
      
    case BADDIR:
      pfatal ("DUPLICATE or BAD BLOCKS IN ROOT INODE");
      if (reply ("REALLOCATE"))
	{
	  freeino (ROOINO);
	  if (allocdir (ROOTINO, ROOTINO, 0755) != ROOTINO)
	    errexit ("CANNOT ALLOCATE ROOT INODE\n");
	}
      if (reply ("CONTINUE") == 0)
	errexit ("");
      break;
    }
  
  statemap[ROOTINO] != DIR_REF;
  
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
	  getpathname (pathbuf, dnp->i_number, dnp->i_number);
	  pwarn ("DIRECTORY %s: LENGTH %d NOT MULTIPLE OF %d",
		 pathbuf, dnp->i_isize, DIRBLKSIZ);
	  if (preen || reply ("ADJUST"))
	    {
	      if (preen)
		printf (" (ADJUSTED)");
	      getinode (number, &dino);
	      dino.di_size = roundup (dnp->i_isize, DIRBLKSIZ);
	      write_inode (number, &dino);
	    }
	}
      bzero (di, sizeof (struct dinode));
      di->di_size = dnp->i_isize;
      bcopy (dnp->i_blks, di->di_db, dnp->i_numblks);
      
      datablocks_iterate (dp, checkdirblock);
    }
  
  /* At this point for each directory:
     If this directory is an entry in another directory, then i_parent is
       set to that nodes directory.
     If this directory has a `..' entry, then i_dotdot is set to that link.
     Check to see that `..' is set correctly. */
  for (nd = 0; nd < dirarrayused; nd++)
    {
      dnp = dirsorted[nd];
      if (dnp->i_parent == 0 || dnp->i_isize == 0)
	continue;
      
      if (dnp->i_dotdot == dnp->i_parent
	  || dnp->i_dotdot == -1)
	continue;
      
      if (dnp->i_dotdot == 0)
	{
	  dnp->i_dotdot = dnp->i_parent;
	  
	  fileerror (dnp->i_parent, dnp->i_number, "MISSING `..'");
	  if (reply ("FIX"))
	    {
	      makeentry (dnp->i_number, dnp->i_parent, "..");
	      
	      
	  
