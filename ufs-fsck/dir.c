/* Directory management subroutines
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


/* This routine is used in pass 1 to initialize DIRARRAY and DIRSORTED.
   Copy information from DP (for number NUMBER) into a newly allocated
   dirinfo structure and add it to the arrays. */
record_directory (struct dinode *dp, ino_t number)
{
  u_int blks;
  struct dirinfo *dnp;
  
  blks = howmany (dp->di_size, sblock.fs_bsize);
  if (blks > NDADDR)
    blks = NDADDR * NIADDR;
  blks *= sizeof (daddr_t);
  dnp = malloc (sizeof (struct dirinfo) + blks);
  
  dnp->i_number = number;
  dnp->i_parent = dnp->i_dotdot = 0;
  dnp->i_isize = dnp->di_size;
  dnp->i_numblks = blks * sizeof (daddr_t);
  bcopy (dp->di_db, dnp->i_blks, blks);
  
  if (dirarrayused ==  dirarraysize)
    {
      if (dirarraysize == 0)
	{
	  dirarraysize = 100;
	  dirarray = malloc (dirarraysize * sizeof (struct dirinfo *));
	  dirsorted = malloc (dirarraysize * sizeof (struct dirinfo *));
	}
      else
	{
	  dirarraysize *= 2;
	  dirarray = realloc (dirarray,
			      dirarraysize * sizeof (struct dirinfo *));
	  dirsorted = realloc (dirsorted,
			       dirarraysize * sizeof (struct dirinfo *));
	}
    }
  dirarray[dirarrayused] = dnp;
  dirsorted[dirarrayused] = dnp;
  dirarrayused++;
}


/* Link node INO into lost+found.  If PARENT is positive then INO is
   a directory, and PARENT is the number of `..' as found in INO.
   If PARENT is zero then INO is a directory without any .. entry. */
void
linkup (ino_t ino, ino_t parent)
{
  struct dinode lfdino;
  char tempnam[MAXNAMLEN];

  if (lfdir == 0)
    {
      struct dinode rootdino;
      getinode (ROOTINO, &rootdino);
      
      scan_dir (lfname, &lfdir);
      if (lfdir == 0)
	{
	  pwarn ("NO lost+found DIRECTORY");
	  if (preen || reply ("CREATE"))
	    {
	      lfdir = allocdir (ROOTINO, 0, lfmode);
	      if (lfdir != 0)
		{
		  if (makeentry (ROOTINO, lfdir, lfname))
		    {
		      if (preen)
			printf (" (CREATED)");
		    }
		  else
		    {
		      freedir (lfdir, ROOTINO);
		      lfdir = 0;
		      if (preen)
			printf ("\n");
		    }
		}
	    }
	  if (!lfdir)
	    {
	      pfatal ("SORRY, CANNOT CREATE lost+found DIRECTORY");
	      printf ("\n\n");
	      return;
	    }
	}
    }
  
  getinode (lfdir, &lfdino);
  if ((DI_MODE (&lfdino) & IFMT) != IFDIR)
    {
      ino_t oldlfdir;
      
      pfatal ("lost+found IS NOT A DIRECTORY");
      if (!reply ("REALLOCATE"))
	return;
      
      oldlfdir = lfdir;

      lfdir = allocdir (ROOTINO, 0, lfmode);
      if (!lfdir)
	{
	  pfatal ("SORRY, CANNOT CREATE lost+found DIRECTORY");
	  printf ("\n\n");
	  return;
	}
      if (!changeino (ROOTINO, lfname, lfdir))
	{
	  pfatal ("SORRY, CANNOT CREATE lost+found DIRECTORY");
	  printf ("\n\n");
	  return;
	}
      
      /* One less link to the old one */
      linkfound[oldlfdir]--;
  
      getinode (lfdir, &lfdino);
  }
  
  if (inodestate[lfdir] != DIR && inodestate[lfdir] != (DIR|DIR_REF))
    {
      pfatal ("SORRY.  lost+found DIRECTORY NOT ALLOCATED.\n\n");
      return;
    }
  lftempnam (tempname, ino);
  if (makeentry (lfdir, ino, tempname))
    {
      pfatal("SORRY. NO SPACE IN lost+found DIRECTORY");
      printf("\n\n");
      return;
    }
  linkfound[ino]++;
  
  if (parent >= 0)
    {
      /* Reset `..' in ino */
      if (parent)
	{
	  if (!changeino (ino, "..", lfdir))
	    {
	      pfatal ("CANNOT ADJUST .. link I=%lu", ino);
	      return;
	    }
	  /* Forget about link to old parent */
	  linkfound[parent]--;
	}
      else if (!makeentry (ino, lfdir, ".."))
	{
	  pfatal ("CANNOT CREAT .. link I=%lu", ino);
	  return;
	}
      
      /* Account for link to lost+found; update inode directly
	 here to avoid confusing warning later. */
      linkfound[lfdir]++;
      lfdino.di_nlink++;
      write_inode (lfdir, &lfdino);
      
      pwarn ("DIR I=%lu CONNECTED. ", ino);
      if (parentdir)
	printf ("PARENT WAS I=%lu\n", parentdir);
      if (!preen)
	printf ("\n");
    }
}

  
