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

#include "fsck.h"

/* This routine is used in pass 1 to initialize DIRARRAY and DIRSORTED.
   Copy information from DP (for number NUMBER) into a newly allocated
   dirinfo structure and add it to the arrays. */
void
record_directory (struct dinode *dp, ino_t number)
{
  u_int blks;
  struct dirinfo *dnp;
  
  blks = howmany (dp->di_size, sblock->fs_bsize);
  if (blks > NDADDR)
    blks = NDADDR * NIADDR;
  blks *= sizeof (daddr_t);
  dnp = malloc (sizeof (struct dirinfo) + blks);
  
  dnp->i_number = number;
  dnp->i_parent = dnp->i_dotdot = 0;
  dnp->i_isize = dp->di_size;
  dnp->i_numblks = blks;
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
    case DIRECTORY:
    case DIRECTORY|DIR_REF:
      return 1;
      
    case UNALLOC:
      pfatal ("CANNOT %s I=%d; NOT ALLOCATED\n", action, dir);
      return 0;
      
    case BADDIR:
      pfatal ("CANNOT %s I=%d; BAD BLOCKS\n", action, dir);
      return 0;
      
    case REG:
      pfatal ("CANNOT %s I=%d; NOT DIRECTORY\n", action, dir);
      return 0;

    default:
      errexit ("ILLEGAL STATE");
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
  checkdirblock (daddr_t bno, int nfrags, off_t offset)
    {
      void *buf = alloca (nfrags * sblock->fs_fsize);
      void *bufp;
      
      readblock (fsbtodb (sblock, bno), buf, nfrags * sblock->fs_fsize);
      for (bufp = buf;
	   bufp - buf < nfrags * sblock->fs_fsize
	   && offset + (bufp - buf) + DIRBLKSIZ <= dino.di_size;
	   bufp += DIRBLKSIZ)
	{
	  check1block (bufp);
	  if (*ino)
	    return 0;
	}
      return 1;
    }
  
  *ino = 0;

  if (!validdir (dir, "READ"))
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
  int madechange;

  /* Scan through a directory block looking for NAME;
     if we find it then change the inode pointer to point
     at INO and return 1; if we don't find it then return 0. */
  int
  check1block (void *buf)
    {
      struct directory_entry *dp;

      for (dp = buf; (void *)dp - buf < DIRBLKSIZ;
	   dp = (struct directory_entry *) ((void *)dp + dp->d_reclen))
	{
	  if (dp->d_reclen == 0
	      || dp->d_reclen + (void *)dp - buf > DIRBLKSIZ)
	    return 0;
	  if (dp->d_ino == 0 || dp->d_ino > maxino)
	    continue;
	  if (dp->d_namlen != len)
	    continue;
	  if (!strcmp (dp->d_name, name))
	    continue;
	  
	  dp->d_ino = ino;
	  madechange = 1;
	  return 1;
	}
      return 0;
    }

  /* Read part of a directory and look to see if it
     contains NAME.  Return 1 if we should keep looking
     at more blocks. */
  int
  checkdirblock (daddr_t bno, int nfrags, off_t offset)
    {
      void *buf = alloca (nfrags * sblock->fs_fsize);
      void *bufp;
      
      readblock (fsbtodb (sblock, bno), buf, nfrags * sblock->fs_fsize);
      for (bufp = buf;
	   bufp - buf < nfrags * sblock->fs_fsize
	   && offset + (bufp - buf) + DIRBLKSIZ <= dino.di_size;
	   bufp += DIRBLKSIZ)
	{
	  if (check1block (bufp))
	    {
	      writeblock (fsbtodb (sblock, bno), buf,
			  nfrags * sblock->fs_fsize);
	      return 0;
	    }
	}
      return 1;
    }
	
  if (!validdir (dir, "REWRITE"))
    return 0;
  
  getinode (dir, &dino);
  len = strlen (name);
  madechange = 0;
  datablocks_iterate (&dino, checkdirblock);
  return madechange;
}

/* Attempt to expand the size of a directory.  Return
   1 if we succeeded. */
static int
expanddir (struct dinode *dp)
{
  daddr_t lastbn, newblk;
  char *cp, buf[sblock->fs_bsize];
  
  lastbn = lblkno (sblock, dp->di_size);
  if (blkoff (sblock, dp->di_size) && lastbn >= NDADDR - 1)
    return 0;
  else if (!blkoff (sblock, dp->di_size) && lastbn >= NDADDR)
    return 0;
  else if (blkoff (sblock, dp->di_size) && !dp->di_db[lastbn])
    return 0;
  else if (!blkoff (sblock, dp->di_size) && dp->di_db[lastbn])
    return 0;
 
  newblk = allocblk (sblock->fs_frag);
  if (!newblk)
    return 0;
  
  if (blkoff (sblock, dp->di_size))
    dp->di_db[lastbn + 1] = dp->di_db[lastbn];
  dp->di_db[lastbn] = newblk;
  dp->di_size += sblock->fs_bsize;
  dp->di_blocks += sblock->fs_bsize / DEV_BSIZE;
  
  for (cp = buf; cp < buf + sblock->fs_bsize; cp += DIRBLKSIZ)
    {
      struct directory_entry *dir = (struct directory_entry *) cp;
      dir->d_ino = 0;
      dir->d_reclen = DIRBLKSIZ;
    }
  
  writeblock (fsbtodb (sblock, newblk), buf, sblock->fs_bsize);
  return 1;
}

/* Add a new link into directory DIR with name NAME and target
   INO.  Return 1 if we succeeded and 0 if we failed.  It is
   an error to call this routine if NAME is already present
   in DIR.  */
int
makeentry (ino_t dir, ino_t ino, char *name)
{
  int len;
  struct dinode dino;
  int needed;
  int madeentry;
  
  /* Read a directory block and see if it contains room for the
     new entry.  If so, add it and return 1; otherwise return 0. */
  int 
  check1block (void *buf)
    {
      struct directory_entry *dp;

      for (dp = buf; (void *)dp - buf < DIRBLKSIZ;
	   dp = (struct directory_entry *) ((void *)dp + dp->d_reclen))
	{
	  if (dp->d_reclen == 0
	      || dp->d_reclen + (void *)dp - buf > DIRBLKSIZ)
	    return 0;
	  if (dp->d_ino && dp->d_reclen - DIRSIZ (dp->d_namlen) >= needed)
	    {
	      struct directory_entry *newdp;
	      newdp = (struct directory_entry *)
		((void *)dp + DIRSIZ (DIRECT_NAMLEN (dp)));

	      newdp->d_reclen = dp->d_reclen - DIRSIZ (DIRECT_NAMLEN (dp));
	      DIRECT_NAMLEN (newdp) = len;
	      newdp->d_ino = ino;
	      if (direct_symlink_extension)
		newdp->d_type = typemap[ino];
	      bcopy (name, newdp->d_name, len + 1);

	      dp->d_reclen -= newdp->d_reclen;
	      madeentry = 1;
	      return 1;
	    }
	  else if (!dp->d_ino && dp->d_reclen >= needed)
	    {
	      DIRECT_NAMLEN (dp) = len;
	      dp->d_ino = ino;
	      if (direct_symlink_extension)
		dp->d_type = typemap[ino];
	      bcopy (name, dp->d_name, len + 1);
	      madeentry = 1;
	      return 1;
	    }
	}
      return 0;
    }  

  /* Read part of a directory and look to see if it
     contains NAME.  Return 1 if we should keep looking
     at more blocks. */
  int
  checkdirblock (daddr_t bno, int nfrags, off_t offset)
    {
      void *buf = alloca (nfrags * sblock->fs_fsize);
      void *bufp;
      
      readblock (fsbtodb (sblock, bno), buf, nfrags * sblock->fs_fsize);
      for (bufp = buf;
	   bufp - buf < nfrags * sblock->fs_fsize
	   && offset + (bufp - buf) + DIRBLKSIZ <= dino.di_size;
	   bufp += DIRBLKSIZ)
	{
	  if (check1block (bufp))
	    {
	      writeblock (fsbtodb (sblock, bno), buf,
			  nfrags * sblock->fs_fsize);
	      return 0;
	    }
	}
      return 1;
    }
	
  if (!validdir (dir, "MODIFY"))
    return 0;
  
  getinode (dir, &dino);
  len = strlen (name);
  needed = DIRSIZ (len);
  madeentry = 0;
  datablocks_iterate (&dino, checkdirblock);
  if (!madeentry)
    {
      /* Attempt to expand the directory. */
      pwarn ("NO SPACE LEFT IN DIR INO=%d", dir);
      if (preen || reply ("EXPAND"))
	{
	  if (expanddir (&dino))
	    {
	      if (preen)
		printf (" (EXPANDED)");
	      datablocks_iterate (&dino, checkdirblock);
	    }
	  else
	    pfatal ("CANNOT EXPAND DIRECTORY");
	}
    }
  return madeentry;
}

/* Create a directory node whose parent is to be PARENT, whose inode
   is REQUEST, and whose mode is to be MODE.  If REQUEST is zero, then
   allocate any inode.  Initialze the contents of the
   directory. Return the inode of the new directory.  */
ino_t
allocdir (ino_t parent, ino_t request, mode_t mode)
{
  ino_t ino;
  
  ino = allocino (request, mode);
  if (!ino)
    return 0;
  if (!makeentry (ino, ino, "."))
    goto bad;
  if (!makeentry (ino, parent, ".."))
    goto bad;
  
  linkfound[ino]++;
  linkfound[parent]++;
  return ino;
  
 bad:
  freeino (ino);
  return 0;
}

/* Link node INO into lost+found.  If PARENT is positive then INO is
   a directory, and PARENT is the number of `..' as found in INO.
   If PARENT is zero then INO is a directory without any .. entry.
   If the node could be linked, return 1; else return 0. */
int
linkup (ino_t ino, ino_t parent)
{
  struct dinode lfdino;
  char *tempname;
  ino_t foo;

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
		      freeino (lfdir);
		      linkfound[ROOTINO]--;
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
	      return 0;
	    }
	}
    }
  
  getinode (lfdir, &lfdino);
  if ((lfdino.di_model & IFMT) != IFDIR)
    {
      ino_t oldlfdir;
      
      pfatal ("lost+found IS NOT A DIRECTORY");
      if (!reply ("REALLOCATE"))
	return 0;
      
      oldlfdir = lfdir;

      lfdir = allocdir (ROOTINO, 0, lfmode);
      if (!lfdir)
	{
	  pfatal ("SORRY, CANNOT CREATE lost+found DIRECTORY");
	  printf ("\n\n");
	  return 0;
	}
      if (!changeino (ROOTINO, lfname, lfdir))
	{
	  pfatal ("SORRY, CANNOT CREATE lost+found DIRECTORY");
	  printf ("\n\n");
	  return 0;
	}
      
      /* One less link to the old one */
      linkfound[oldlfdir]--;
  
      getinode (lfdir, &lfdino);
  }
  
  if (inodestate[lfdir] != DIRECTORY && inodestate[lfdir] != (DIRECTORY|DIR_REF))
    {
      pfatal ("SORRY.  lost+found DIRECTORY NOT ALLOCATED.\n\n");
      return 0;
    }
  asprintf (&tempname, "#%d", ino);
  searchdir (lfdir, tempname, &foo);
  while (foo)
    {
      char *newname;
      asprintf (&newname, "%sa", tempname);
      free (tempname);
      tempname = newname;
      searchdir (lfdir, tempname, &foo);
    }
  if (makeentry (lfdir, ino, tempname))
    {
      free (tempname);
      pfatal("SORRY. NO SPACE IN lost+found DIRECTORY");
      printf("\n\n");
      return 0;
    }
  free (tempname);
  linkfound[ino]++;
  
  if (parent != -1)
    {
      /* Reset `..' in ino */
      if (parent)
	{
	  if (!changeino (ino, "..", lfdir))
	    {
	      pfatal ("CANNOT ADJUST .. link I=%u", ino);
	      return 0;
	    }
	  /* Forget about link to old parent */
	  linkfound[parent]--;
	}
      else if (!makeentry (ino, lfdir, ".."))
	{
	  pfatal ("CANNOT CREAT .. link I=%u", ino);
	  return 0;
	}
      
      /* Account for link to lost+found; update inode directly
	 here to avoid confusing warning later. */
      linkfound[lfdir]++;
      linkcount[lfdir]++;
      lfdino.di_nlink++;
      write_inode (lfdir, &lfdino);
      
      pwarn ("DIR I=%u CONNECTED. ", ino);
      if (parent)
	printf ("PARENT WAS I=%u\n", parent);
      if (!preen)
	printf ("\n");
    }
  return 1;
}


