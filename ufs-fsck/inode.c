/* Inode allocation, deallocation, etc.
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

static void
inode_iterate (struct dinode *dp, 
	       int (*fn) (daddr_t, int, off_t),
	       int doaddrblocks)
{
  mode_t mode = dp->di_model & IFMT;
  int nb, maxb;
  off_t totaloffset = 0;
  
  /* Call FN for iblock IBLOCK of level LEVEL and recurse down
     the indirect block pointers. */
  int
  scaniblock (daddr_t iblock, int level)
    {
      int cont;
      daddr_t ptrs[NINDIR(sblock)];
      int i;

      if (doaddrblocks)
	{
	  cont = (*fn)(iblock, sblock->fs_frag, totaloffset);
	  if (cont == RET_STOP)
	    return RET_STOP;
	  else if (cont == RET_BAD)
	    return RET_GOOD;
	}
      
      readblock (fsbtodb (sblock, iblock), ptrs, sblock->fs_bsize);
      for (i = 0; i < NINDIR (sblock); i++)
	{
	  if (!ptrs[i])
	    continue;
	  
	  if (level == 0)
	    {
	      cont = (*fn)(ptrs[i], sblock->fs_frag, totaloffset);
	      totaloffset += sblock->fs_bsize;
	    }
	  else 
	    cont = scaniblock (ptrs[i], level - 1);
	  if (cont == RET_STOP)
	    return RET_STOP;
	}
      return RET_GOOD;
    }

  if (mode == IFBLK || mode == IFCHR
      || (mode == IFLNK && sblock->fs_maxsymlinklen != -1
	  && (dp->di_size < sblock->fs_maxsymlinklen
	      || (sblock->fs_maxsymlinklen == 0 && dp->di_blocks == 0))))
    return;

  maxb = lblkno (sblock, dp->di_size - 1);
  totaloffset = 0;
  for (nb = 0; nb < NDADDR; nb++)
    {
      int offset;
      int nfrags;
      
      if (nb == maxb && (offset = blkoff (sblock, dp->di_size)))
	nfrags = numfrags (sblock, fragroundup (sblock, offset));
      else
	nfrags = sblock->fs_frag;
      
      if (dp->di_db[nb]
	  && (*fn)(dp->di_db[nb], nfrags, totaloffset) != RET_GOOD)
	return;
      totaloffset += nfrags * sizeof (sblock->fs_fsize);
    }
  
  for (nb = 0; nb < NIADDR; nb++)
    if (dp->di_ib[nb] && scaniblock (dp->di_ib[nb], nb) != RET_GOOD)
      return;

  if (doaddrblocks && dp->di_trans)
    (*fn)(dp->di_trans, sblock->fs_frag, totaloffset);
}

void
datablocks_iterate (struct dinode *dp, 
		    int (*fn) (daddr_t, int, off_t))
{
  inode_iterate (dp, fn, 0);
}

void
allblock_iterate (struct dinode *dp,
		  int (*fn) (daddr_t, int, off_t))
{
  inode_iterate (dp, fn, 1);
}

/* Allocate an inode.  If INUM is nonzero, then allocate that 
   node specifically, otherwise allocate any available inode.
   MODE is the mode of the new file.  Return the allocated 
   inode number (or 0 if the allocation failed). */
ino_t
allocino (ino_t request, mode_t mode)
{
  ino_t ino;
  struct dinode dino;
  struct timeval tv;
  
  if (request)
    {
      if (inodestate[request] != UNALLOC)
	return 0;
      ino = request;
    }
  else
    {
      for (ino = ROOTINO; ino < maxino; ino++)
	if (inodestate[ino] == UNALLOC)
	  break;
      if (ino == maxino)
	return 0;
    }
  
  if ((mode & IFMT) == IFDIR)
    inodestate[ino] = DIRECTORY | DIR_REF;
  else
    inodestate[ino] = REG;
  
  getinode (ino, &dino);
  dino.di_modeh = (mode & 0xffff0000) >> 16;
  dino.di_model = (mode & 0x0000ffff);
  gettimeofday (&tv, 0);
  dino.di_atime.ts_sec = tv.tv_sec;
  dino.di_atime.ts_nsec = tv.tv_usec * 1000;
  dino.di_mtime = dino.di_ctime = dino.di_atime;
  dino.di_size = 0;
  dino.di_blocks = 0;
  num_files++;
  write_inode (ino, &dino);
  typemap[ino] = IFTODT (mode);
  return ino;
}

/* Deallocate inode INUM.  */
void
freeino (ino_t inum)
{
  struct dinode dino;
  
  int
  clearblock (daddr_t bno, int nfrags, off_t offset)
    {
      int i;
      
      for (i = 0; i < nfrags; i++)
	{
	  if (check_range (bno + i, 1))
	    return RET_BAD;
	  if (testbmap (bno + i))
	    {
	      struct dups *dlp;
	      for (dlp = duplist; dlp; dlp = dlp->next)
		{
		  if (dlp->dup != bno + i)
		    continue;
		  dlp->dup = duplist->dup;
		  dlp = duplist;
		  duplist = duplist->next;
		  free (dlp);
		  break;
		}
	      if (dlp == 0)
		clrbmap (bno + i);
	    }
	}
      return RET_GOOD;
    }

  getinode (inum, &dino);
  allblock_iterate (&dino, clearblock);
  
  clear_inode (inum, &dino);
  inodestate[inum] = UNALLOC;

  num_files--;
}
