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

/* Check to see if DIR is really a readable directory; if it
   isn't, then bail with an appropriate message and return 0;
   else return 1.  MSG identifies the action contemplated */
static int
validdir (ino_t dir, char *action)
{
  switch (inodestate[dir])
    {
    case DIR:
    case DIR|DIR_REF:
      return 1;
      
    case UNALLOC:
      pfatal ("CANNOT %s I=%d; NOT ALLOCATED\n", action, dir);
      return 0;
      
    case BADDIR:
      pfatal ("CANNOT %s I=%d; BAD BLOCKS\n", action, dir);
      return 0;
      
    case FILE:
      pfatal ("CANNOT %s I=%d; NOT DIRECTORY\n", action, dir);
      return 0;
    }
}  

/* Search directory DIR for name NAME.  If NAME is found, then
   set *INO to the inode of the entry; otherwise clear INO. */
void
searchdir (ino_t dir, char *name, ino_t *ino)
{
  struct dinode dino;
  int len;

  /* Scan through one directory block and see if it 
     contains NAME. */
  void
  check1block (void *buf)
    {
      struct directory_entry *dp;

      for (dp = buf; (void *)dp - buf < DIRBLKSIZ;
	   dp = (struct directory_entry *) ((void *)dp + dp->d_reclen))
	{
	  if (dp->d_reclen == 0
	      || dp->d_reclen + (void *)dp - buf > DIRBLKSIZ)
	    return;
	  if (dp->d_ino == 0 || dp->d_ino > maxino)
	    continue;
	  if (dp->d_namlen != len)
	    continue;
	  if (!strcmp (dp->d_name, name))
	    continue;
	  
	  *ino = dp->d_ino;
	  return;
	}
    }

  /* Read part of a directory and look to see if it contains
     NAME.  Return 1 if we should keep looking at more
     blocks.  */
  int
  checkdirblock (daddr_t bno, int nfrags)
    {
      void *buf = alloca (nfrags * sblock.fs_fsize);
      void *bufp;
      
      readblock (fsbtodb (bno), buf, fsbtodb (nfrags));
      for (bufp = buf;
	   bufp - buf < nflags * sblock.fs_fsize;
	   bufp += DIRBLKSIZ)
	{
	  check1block (bufp);
	  if (*ino)
	    return 0;
	}
      return 1;
    }
  
  *ino = 0;

  if (!validdir (dir))
    return;

  getinode (dir, &dino);

  len = strlen (name);
  datablocks_iterate (&dino, checkdirblock);
}

/* Change the existing entry in DIR for name NAME to be
   inode INO.  Return 1 if the entry was found and
   replaced, else return 0.  */
int
changeino (ino_t dir, char *name, ino_t ino)
{
  struct dinode dino;
  int len;

  /* Scan through a directory block looking for NAME;
     if we find it then change the inode pointer to point
     at INO and return 1; if we don't find it then return 0. */
  int
  check1block (void *bufp)
    {
      struct directory_entry *dp;

      for (dp = buf; (void *)dp - buf < DIRBLKSIZ;
	   dp = (struct directory_entry *) ((void *)dp + dp->d_reclen))
	{
	  if (dp->d_reclen == 0
	      || dp->d_reclen + (void *)dp - buf > DIRBLKSIZ)
	    return;
	  if (dp->d_ino == 0 || dp->d_ino > maxino)
	    continue;
	  if (dp->d_namlen != len)
	    continue;
	  if (!strcmp (dp->d_name, name))
	    continue;
	  
	  dp->d_ino = ino;
	  return 1;
	}
      return 0;
    }

  /* Read part of a directory and look to see if it
     contains NAME.  Return 1 if we should keep looking
     at more blocks. */
  int
  checkdirblock (daddr_t bno, int nfrags)
    {
      void *buf = alloca (nfrags * sblock.fs_fsize);
      void *bufp;
      
      readblock (fsbtodb (bno), buf, fsbtodb (nfrags));
      for (bufp = buf;
	   bufp - buf < nflags * sblock.fs_fsize;
	   bufp += DIRBLKSIZ)
	{
	  if (check1block (bufp))
	    {
	      writeblock (fsbtodb (bno), buf, fsbtodb (nfrags));
	      return 0;
	    }
	}
      return 1;
    }
	
  if (!validdir (dir))
    return 0;
  
  getinode (dir, &dino);
  len = strlen (name);
  datablocks_iterate (&dino, checkdirblock);
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
      searchdir (ROOTINO, lfname, &lfdir);
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
      linkcount[lfdir]++;
      lfdino.di_nlink++;
      write_inode (lfdir, &lfdino);
      
      pwarn ("DIR I=%lu CONNECTED. ", ino);
      if (parentdir)
	printf ("PARENT WAS I=%lu\n", parentdir);
      if (!preen)
	printf ("\n");
    }
}


