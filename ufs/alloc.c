/* Disk allocation routines
   Copyright (C) 1993, 1994 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Modified from UCB by Michael I. Bushnell.  */

/*
 * Copyright (c) 1982, 1986, 1989 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution is only permitted until one year after the first shipment
 * of 4.4BSD by the Regents.  Otherwise, redistribution and use in source and
 * binary forms are permitted provided that: (1) source distributions retain
 * this entire copyright notice and comment, and (2) distributions including
 * binaries display the following acknowledgement:  This product includes
 * software developed by the University of California, Berkeley and its
 * contributors'' in the documentation or other materials provided with the
 * distribution and in all advertising materials mentioning features or use
 * of this software.  Neither the name of the University nor the names of
 * its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)ufs_alloc.c	7.20 (Berkeley) 6/28/90
 */

#include "ufs.h"
#include "fs.h"
#include "dinode.h"

#include <stdio.h>

static u_long alloccg (int, daddr_t, int);
static u_long ialloccg (int, daddr_t, int);
static u_long hashalloc (int, long, int, u_long(*)(int, daddr_t, int));
static daddr_t fragextend (int, long, int, int);
static daddr_t alloccgblk (struct cg *, daddr_t);
static daddr_t mapsearch (struct cg *, daddr_t, int);
static ino_t dirpref ();

/* These are in tables.c.  */
extern int inside[], around[];     
extern unsigned char	*fragtbl[];

static spin_lock_t alloclock = SPIN_LOCK_INITIALIZER;

/*
 * Allocate a block in the file system.
 * 
 * The size of the requested block is given, which must be some
 * multiple of fs_fsize and <= fs_bsize.
 * A preference may be optionally specified. If a preference is given
 * the following hierarchy is used to allocate a block:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate a block in the same cylinder group.
 *   4) quadradically rehash into other cylinder groups, until an
 *      available block is located.
 * If no block preference is given the following heirarchy is used
 * to allocate a block:
 *   1) allocate a block in the cylinder group that contains the
 *      inode for the file.
 *   2) quadradically rehash into other cylinder groups, until an
 *      available block is located.
 */
error_t
alloc(struct node *np,
      daddr_t lbn, 
      daddr_t bpref,
      int size,
      daddr_t *bnp,
      struct protid *cred)
{
  int cg;
  daddr_t bno;

  *bnp = 0;
  assert ("Alloc of bad sized block" && (unsigned) size <= sblock->fs_bsize
	  && !fragoff(size) && size != 0);

  spin_lock (&alloclock);
  
  if (size == sblock->fs_bsize && sblock->fs_cstotal.cs_nbfree == 0)
    goto nospace;
  if (cred && !diskfs_isuid (0, cred) && freespace(sblock->fs_minfree) <= 0)
    goto nospace;
  
  if (bpref >= sblock->fs_size)
    bpref = 0;
  if (bpref == 0)
    cg = itog(np->dn->number);
  else
    cg = dtog(bpref);
  bno = (daddr_t)hashalloc(cg, (long)bpref, size, alloccg);

  spin_unlock (&alloclock);

  if (bno > 0)
    {
      np->dn_stat.st_blocks += btodb(size);
      np->dn_set_mtime = 1;
      np->dn_set_ctime = 1;
      *bnp = bno;
      return 0;
    }

 nospace:
  spin_unlock (&alloclock);
  printf("file system full\n");
  return (ENOSPC);
}

/*
 * Reallocate a fragment to a bigger size
 *
 * The number and size of the old block is given, and a preference
 * and new size is also specified. The allocator attempts to extend
 * the original block. Failing that, the regular block allocator is
 * invoked to get an appropriate block.
 */
error_t
realloccg(struct node *np,
	  daddr_t lbprev,
	  volatile daddr_t bpref,
	  int osize, 
	  int nsize,
	  daddr_t *pbn,
	  struct protid *cred)
{
  volatile int cg, request;
  daddr_t bprev, bno;
  error_t error;
  
  *pbn = 0;
  assert ("bad old size" && (unsigned) osize <= sblock->fs_bsize
	  && !fragoff (osize) && osize != 0 );
  assert ("bad new size" && (unsigned) nsize <= sblock->fs_bsize
	  && !fragoff (nsize) && nsize != 0);

  spin_lock (&alloclock);

  if (cred && !diskfs_isuid (0, cred) && freespace(sblock->fs_minfree) <= 0)
    {
      spin_unlock (&alloclock);
      goto nospace;
    }

  if (error = diskfs_catch_exception ())
    return error;
  bprev = dinodes[np->dn->number].di_db[lbprev];
  diskfs_end_catch_exception ();
  assert ("old block not allocated" && bprev);

  /*
   * Check for extension in the existing location.
   */
  cg = dtog(bprev);
  if (bno = fragextend(cg, (long)bprev, osize, nsize))
    {
      spin_unlock (&alloclock);
      assert ("fragextend behaved incorrectly" && bprev == bno);
      np->dn_stat.st_blocks += btodb(nsize - osize);
      np->dn_set_mtime = 1;
      np->dn_set_ctime = 1;
      *pbn = bno;
      return (0);
    }
  /*
   * Allocate a new disk location.
   */
  if (bpref >= sblock->fs_size)
    bpref = 0;
  switch ((int)sblock->fs_optim)
    {
    case FS_OPTSPACE:
      /*
       * Allocate an exact sized fragment. Although this makes 
       * best use of space, we will waste time relocating it if 
       * the file continues to grow. If the fragmentation is
       * less than half of the minimum free reserve, we choose
       * to begin optimizing for time.
       */
      request = nsize;
      if (sblock->fs_minfree < 5 ||
	  sblock->fs_cstotal.cs_nffree >
	  sblock->fs_dsize * sblock->fs_minfree / (2 * 100))
	break;
      printf("optimization changed from SPACE to TIME\n");
      sblock->fs_optim = FS_OPTTIME; 
      break;
    case FS_OPTTIME:
      /*
       * At this point we have discovered a file that is trying
       * to grow a small fragment to a larger fragment. To save
       * time, we allocate a full sized block, then free the 
       * unused portion. If the file continues to grow, the 
       * `fragextend' call above will be able to grow it in place
       * without further copying. If aberrant programs cause
       * disk fragmentation to grow within 2% of the free reserve,
       * we choose to begin optimizing for space.
       */
      request = sblock->fs_bsize;
      if (sblock->fs_cstotal.cs_nffree <
	  sblock->fs_dsize * (sblock->fs_minfree - 2) / 100)
	break;
      printf("%s: optimization changed from TIME to SPACE\n",
	     sblock->fs_fsmnt);
      sblock->fs_optim = FS_OPTSPACE;
      break;
    default:
      assert ("filesystem opitimazation bad value" && 0);
    }
  
  bno = (daddr_t)hashalloc(cg, (long)bpref, request, 
			   (u_long (*)())alloccg);

  spin_unlock (&alloclock);

  if (bno > 0) 
    {
      blkfree(bprev, (off_t)osize);
      if (nsize < request)
	blkfree(bno + numfrags(nsize), (off_t)(request - nsize));
      np->dn_stat.st_blocks += btodb (nsize - osize);
      np->dn_set_mtime = 1;
      np->dn_set_ctime = 1;
      *pbn = bno;
      return (0);
    }
 nospace:
  /*
   * no space available
   */
  printf("file system full\n");
  return (ENOSPC);
}


/* Implement the diskfs_alloc_node callback from the diskfs library.
   See <hurd/diskfs.h> for the interface description. */
error_t
diskfs_alloc_node(struct node *dir,
		  mode_t mode,
		  struct node **npp)
{
  int ino;
  struct node *np;
  int cg;
  error_t error;
  int ipref;

  if (S_ISDIR (mode))
    ipref = dirpref ();
  else
    ipref = dir->dn->number;
  
  *npp = 0;

  spin_lock (&alloclock);
  if (sblock->fs_cstotal.cs_nifree == 0)
    {
      spin_unlock (&alloclock);
      goto noinodes;
    }
  if (ipref >= sblock->fs_ncg * sblock->fs_ipg)
    ipref = 0;
  cg = itog(ipref);
  ino = (int)hashalloc(cg, (long)ipref, mode, ialloccg);
  spin_unlock (&alloclock);
  if (ino == 0)
    goto noinodes;
  if (error = iget(ino, &np))
    return error;
  *npp = np;
  assert ("duplicate allocation" && !np->dn_stat.st_mode);
  if (np->dn_stat.st_blocks)
    {
      printf("free inode %d had %d blocks\n", ino, np->dn_stat.st_blocks);
      np->dn_stat.st_blocks = 0;
      np->dn_set_ctime = 1;
    }
  /*
   * Set up a new generation number for this inode.
   */
  spin_lock (&gennumberlock);
  if (++nextgennumber < (u_long)diskfs_mtime->seconds)
    nextgennumber = diskfs_mtime->seconds;
  np->dn_stat.st_gen = nextgennumber;
  spin_unlock (&gennumberlock);
  return (0);
 noinodes:
  printf("out of inodes\n");
  return (ENOSPC);
}

/*
 * Find a cylinder to place a directory.
 *
 * The policy implemented by this algorithm is to select from
 * among those cylinder groups with above the average number of
 * free inodes, the one with the smallest number of directories.
 */
static ino_t
dirpref()
{
  int cg, minndir, mincg, avgifree;
  
  spin_lock (&alloclock);
  avgifree = sblock->fs_cstotal.cs_nifree / sblock->fs_ncg;
  minndir = sblock->fs_ipg;
  mincg = 0;
  for (cg = 0; cg < sblock->fs_ncg; cg++)
    if (csum[cg].cs_ndir < minndir && csum[cg].cs_nifree >= avgifree)
      {
	mincg = cg;
	minndir = csum[cg].cs_ndir;
      }
  spin_unlock (&alloclock);
  return ((int)(sblock->fs_ipg * mincg));
}

/*
 * Select the desired position for the next block in a file.  The file is
 * logically divided into sections. The first section is composed of the
 * direct blocks. Each additional section contains fs_maxbpg blocks.
 * 
 * If no blocks have been allocated in the first section, the policy is to
 * request a block in the same cylinder group as the inode that describes
 * the file. If no blocks have been allocated in any other section, the
 * policy is to place the section in a cylinder group with a greater than
 * average number of free blocks.  An appropriate cylinder group is found
 * by using a rotor that sweeps the cylinder groups. When a new group of
 * blocks is needed, the sweep begins in the cylinder group following the
 * cylinder group from which the previous allocation was made. The sweep
 * continues until a cylinder group with greater than the average number
 * of free blocks is found. If the allocation is for the first block in an
 * indirect block, the information on the previous allocation is unavailable;
 * here a best guess is made based upon the logical block number being
 * allocated.
 * 
 * If a section is already partially allocated, the policy is to
 * contiguously allocate fs_maxcontig blocks.  The end of one of these
 * contiguous blocks and the beginning of the next is physically separated
 * so that the disk head will be in transit between them for at least
 * fs_rotdelay milliseconds.  This is to allow time for the processor to
 * schedule another I/O transfer.
 */
daddr_t
blkpref(struct node *np,
	daddr_t lbn,
	int indx,
	daddr_t *bap)
{
  int cg;
  int avgbfree, startcg;
  daddr_t nextblk;
  
  spin_lock (&alloclock);
  if (indx % sblock->fs_maxbpg == 0 || bap[indx - 1] == 0) 
    {
      if (lbn < NDADDR) 
	{
	  spin_unlock (&alloclock);
	  cg = itog(np->dn->number);
	  return (sblock->fs_fpg * cg + sblock->fs_frag);
	}
      /*
       * Find a cylinder with greater than average number of
       * unused data blocks.
       */
      if (indx == 0 || bap[indx - 1] == 0)
	startcg = itog(np->dn->number) + lbn / sblock->fs_maxbpg;
      else
	startcg = dtog(bap[indx - 1]) + 1;
      startcg %= sblock->fs_ncg;
      avgbfree = sblock->fs_cstotal.cs_nbfree / sblock->fs_ncg;
      for (cg = startcg; cg < sblock->fs_ncg; cg++)
	if (csum[cg].cs_nbfree >= avgbfree) 
	  {
	    spin_unlock (&alloclock);
	    sblock->fs_cgrotor = cg;
	    return (sblock->fs_fpg * cg + sblock->fs_frag);
	  }
      for (cg = 0; cg <= startcg; cg++)
	if (csum[cg].cs_nbfree >= avgbfree) 
	  {
	    spin_unlock (&alloclock);
	    sblock->fs_cgrotor = cg;
	    return (sblock->fs_fpg * cg + sblock->fs_frag);
	  }
      spin_unlock (&alloclock);
      return 0;
    }

  spin_unlock (&alloclock);

  /*
   * One or more previous blocks have been laid out. If less
   * than fs_maxcontig previous blocks are contiguous, the
   * next block is requested contiguously, otherwise it is
   * requested rotationally delayed by fs_rotdelay milliseconds.
   */
  nextblk = bap[indx - 1] + sblock->fs_frag;
  if (indx > sblock->fs_maxcontig &&
      bap[indx - sblock->fs_maxcontig] + blkstofrags(sblock->fs_maxcontig)
      != nextblk)
    return (nextblk);
  if (sblock->fs_rotdelay != 0)
    /*
     * Here we convert ms of delay to frags as:
     * (frags) = (ms) * (rev/sec) * (sect/rev) /
     *	((sect/frag) * (ms/sec))
     * then round up to the next block.
     */
    nextblk += roundup(sblock->fs_rotdelay * sblock->fs_rps 
		       * sblock->fs_nsect / (NSPF * 1000), sblock->fs_frag);
  return (nextblk);
}

/*
 * Implement the cylinder overflow algorithm.
 *
 * The policy implemented by this algorithm is:
 *   1) allocate the block in its requested cylinder group.
 *   2) quadradically rehash on the cylinder group number.
 *   3) brute force search for a free block.
 */
/*VARARGS5*/
static u_long
hashalloc(int cg,
	  long pref,
	  int size,		/* size for data blocks, mode for inodes */
	  u_long (*allocator)(int, daddr_t, int))
{
  long result;
  int i, icg = cg;
  
  /*
   * 1: preferred cylinder group
   */
  result = (*allocator)(cg, pref, size);
  if (result)
    return (result);
  /*
   * 2: quadratic rehash
   */
  for (i = 1; i < sblock->fs_ncg; i *= 2) 
    {
      cg += i;
      if (cg >= sblock->fs_ncg)
	cg -= sblock->fs_ncg;
      result = (*allocator)(cg, 0, size);
      if (result)
	return (result);
    }
  /*
   * 3: brute force search
   * Note that we start at i == 2, since 0 was checked initially,
   * and 1 is always checked in the quadratic rehash.
   */
  cg = (icg + 2) % sblock->fs_ncg;
  for (i = 2; i < sblock->fs_ncg; i++)
    {
      result = (*allocator)(cg, 0, size);
      if (result)
	return (result);
      cg++;
      if (cg == sblock->fs_ncg)
	cg = 0;
    }
  return 0;
}

/*
 * Determine whether a fragment can be extended.
 *
 * Check to see if the necessary fragments are available, and 
 * if they are, allocate them.
 */
static daddr_t
fragextend(int cg,
	   long bprev,
	   int osize, 
	   int nsize)
{
  struct cg *cgp;
  long bno;
  int frags, bbase;
  int i;
  
  if (csum[cg].cs_nffree < numfrags(nsize - osize))
    return 0;
  frags = numfrags(nsize);
  bbase = fragnum(bprev);
  if (bbase > fragnum((bprev + frags - 1)))
    /* cannot extend across a block boundary */
    return 0;

  cgp = (struct cg *) (cgs + sblock->fs_bsize * cg);

  if (diskfs_catch_exception ())
    return 0;			/* bogus, but that's what BSD does... */
  
  if (!cg_chkmagic(cgp))
    {
      printf ("Cylinder group %d bad magic number: %ld/%ld\n",
	      cg, cgp->cg_magic, ((struct ocg *)(cgp))->cg_magic);
      diskfs_end_catch_exception ();
      return 0;
    }
  cgp->cg_time = diskfs_mtime->seconds;
  bno = dtogd(bprev);
  for (i = numfrags(osize); i < frags; i++)
    if (isclr(cg_blksfree(cgp), bno + i))
      {
	diskfs_end_catch_exception ();
	return 0;
      }

  /*
   * the current fragment can be extended
   * deduct the count on fragment being extended into
   * increase the count on the remaining fragment (if any)
   * allocate the extended piece
   */
  for (i = frags; i < sblock->fs_frag - bbase; i++)
    if (isclr(cg_blksfree(cgp), bno + i))
      break;
  cgp->cg_frsum[i - numfrags(osize)]--;
  if (i != frags)
    cgp->cg_frsum[i - frags]++;
  for (i = numfrags(osize); i < frags; i++)
    {
      clrbit(cg_blksfree(cgp), bno + i);
      cgp->cg_cs.cs_nffree--;
      sblock->fs_cstotal.cs_nffree--;
      csum[cg].cs_nffree--;
    }
  diskfs_end_catch_exception ();
  return (bprev);
}

/*
 * Determine whether a block can be allocated.
 *
 * Check to see if a block of the apprpriate size is available,
 * and if it is, allocate it.
 */
static u_long
alloccg(int cg,
	volatile daddr_t bpref,
	int size)
{
  struct cg *cgp;
  int i;
  int bno, frags, allocsiz;
  
  if (csum[cg].cs_nbfree == 0 && size == sblock->fs_bsize)
    return 0;
  cgp = (struct cg *) (cgs + sblock->fs_bsize * cg);

  if (diskfs_catch_exception ())
    return 0;
  
  if (!cg_chkmagic(cgp) ||
      (cgp->cg_cs.cs_nbfree == 0 && size == sblock->fs_bsize))
    {
      printf ("Cylinder group %d bad magic number: %ld/%ld\n",
	      cg, cgp->cg_magic, ((struct ocg *)(cgp))->cg_magic);
      diskfs_end_catch_exception ();
      return 0;
    }
  cgp->cg_time = diskfs_mtime->seconds;
  if (size == sblock->fs_bsize)
    {
      bno = alloccgblk(cgp, bpref);
      diskfs_end_catch_exception ();
      return (u_long) (bno);
    }
  /*
   * check to see if any fragments are already available
   * allocsiz is the size which will be allocated, hacking
   * it down to a smaller size if necessary
   */
  frags = numfrags(size);
  for (allocsiz = frags; allocsiz < sblock->fs_frag; allocsiz++)
    if (cgp->cg_frsum[allocsiz] != 0)
      break;
  if (allocsiz == sblock->fs_frag)
    {
      /*
       * no fragments were available, so a block will be 
       * allocated, and hacked up
       */
      if (cgp->cg_cs.cs_nbfree == 0)
	{
	  diskfs_end_catch_exception ();
	  return 0;
	}

      bno = alloccgblk(cgp, bpref);
      bpref = dtogd(bno);
      for (i = frags; i < sblock->fs_frag; i++)
	setbit(cg_blksfree(cgp), bpref + i);
      i = sblock->fs_frag - frags;
      cgp->cg_cs.cs_nffree += i;
      sblock->fs_cstotal.cs_nffree += i;
      csum[cg].cs_nffree += i;
      cgp->cg_frsum[i]++;
      return (u_long)(bno);
    }
  bno = mapsearch(cgp, bpref, allocsiz);
  if (bno < 0)
    {
      diskfs_end_catch_exception ();
      return 0;
    }

  for (i = 0; i < frags; i++)
    clrbit(cg_blksfree(cgp), bno + i);
  cgp->cg_cs.cs_nffree -= frags;
  sblock->fs_cstotal.cs_nffree -= frags;
  csum[cg].cs_nffree -= frags;
  cgp->cg_frsum[allocsiz]--;
  if (frags != allocsiz)
    cgp->cg_frsum[allocsiz - frags]++;
  diskfs_end_catch_exception ();
  return (u_long) (cg * sblock->fs_fpg + bno);
}

/*
 * Allocate a block in a cylinder group.
 *
 * This algorithm implements the following policy:
 *   1) allocate the requested block.
 *   2) allocate a rotationally optimal block in the same cylinder.
 *   3) allocate the next available block on the block rotor for the
 *      specified cylinder group.
 * Note that this routine only allocates fs_bsize blocks; these
 * blocks may be fragmented by the routine that allocates them.
 */
static daddr_t
alloccgblk(struct cg *cgp,
	   volatile daddr_t bpref)
{
  daddr_t bno;
  int cylno, pos, delta;
  short *cylbp;
  int i;
  daddr_t ret;
  
  if (diskfs_catch_exception ())
    return 0;
  
  if (bpref == 0) 
    {
      bpref = cgp->cg_rotor;
      goto norot;
    }
  bpref = blknum(bpref);
  bpref = dtogd(bpref);
  /*
   * if the requested block is available, use it
   */
  if (isblock(cg_blksfree(cgp), fragstoblks(bpref)))
    {
      bno = bpref;
      goto gotit;
    }
  /*
   * check for a block available on the same cylinder
   */
  cylno = cbtocylno(bpref);
  if (cg_blktot(cgp)[cylno] == 0)
    goto norot;
  if (sblock->fs_cpc == 0)
    {
      /*
       * block layout info is not available, so just have
       * to take any block in this cylinder.
       */
      bpref = howmany(sblock->fs_spc * cylno, NSPF);
      goto norot;
    }
  /*
   * check the summary information to see if a block is 
   * available in the requested cylinder starting at the
   * requested rotational position and proceeding around.
   */
  cylbp = cg_blks(cgp, cylno);
  pos = cbtorpos(bpref);
  for (i = pos; i < sblock->fs_nrpos; i++)
    if (cylbp[i] > 0)
      break;
  if (i == sblock->fs_nrpos)
    for (i = 0; i < pos; i++)
      if (cylbp[i] > 0)
	break;
  if (cylbp[i] > 0)
    {
      /*
       * found a rotational position, now find the actual
       * block. A panic if none is actually there.
       */
      pos = cylno % sblock->fs_cpc;
      bno = (cylno - pos) * sblock->fs_spc / NSPB;
      assert ("postbl table bad" &&fs_postbl(pos)[i] != -1);
      for (i = fs_postbl(pos)[i];; )
	{
	  if (isblock(cg_blksfree(cgp), bno + i))
	    {
	      bno = blkstofrags(bno + i);
	      goto gotit;
	    }
	  delta = fs_rotbl[i];
	  if (delta <= 0 ||
	      delta + i > fragstoblks(sblock->fs_fpg))
	    break;
	  i += delta;
	}
      assert ("Inconsistent rotbl table" && 0);
    }
 norot:
  /*
   * no blocks in the requested cylinder, so take next
   * available one in this cylinder group.
   */
  bno = mapsearch(cgp, bpref, (int)sblock->fs_frag);
  if (bno < 0)
    {
      diskfs_end_catch_exception ();
      return 0;
    }
  cgp->cg_rotor = bno;
 gotit:
  clrblock(cg_blksfree(cgp), (long)fragstoblks(bno));
  cgp->cg_cs.cs_nbfree--;
  sblock->fs_cstotal.cs_nbfree--;
  csum[cgp->cg_cgx].cs_nbfree--;
  cylno = cbtocylno(bno);
  cg_blks(cgp, cylno)[cbtorpos(bno)]--;
  cg_blktot(cgp)[cylno]--;
  ret = cgp->cg_cgx * sblock->fs_fpg + bno;
  diskfs_end_catch_exception ();
  return ret;
}

/*
 * Determine whether an inode can be allocated.
 *
 * Check to see if an inode is available, and if it is,
 * allocate it using the following policy:
 *   1) allocate the requested inode.
 *   2) allocate the next available inode after the requested
 *      inode in the specified cylinder group.
 */
static u_long
ialloccg(int cg,
	 volatile daddr_t ipref,
	 int modein)
{
  struct cg *cgp;
  int start, len, loc, map, i;
  mode_t mode = (mode_t) modein;
  
  if (csum[cg].cs_nifree == 0)
    return 0;

  cgp = (struct cg *)(cgs + sblock->fs_bsize * cg);

  if (diskfs_catch_exception ())
    return 0;
  
  if (!cg_chkmagic(cgp) || cgp->cg_cs.cs_nifree == 0)
    {
      printf ("Cylinder group %d bad magic number: %ld/%ld\n",
	      cg, cgp->cg_magic, ((struct ocg *)(cgp))->cg_magic);
      diskfs_end_catch_exception ();
      return 0;
    }
  cgp->cg_time = diskfs_mtime->seconds;
  if (ipref)
    {
      ipref %= sblock->fs_ipg;
      if (isclr(cg_inosused(cgp), ipref))
	goto gotit;
    }
  start = cgp->cg_irotor / NBBY;
  len = howmany(sblock->fs_ipg - cgp->cg_irotor, NBBY);
  loc = skpc(0xff, len, (u_char *) &cg_inosused(cgp)[start]);
  if (loc == 0)
    {
      len = start + 1;
      start = 0;
      loc = skpc(0xff, len, (u_char *) &cg_inosused(cgp)[0]);
      assert ("inconsistent cg_inosused table" && loc);
    }
  i = start + len - loc;
  map = cg_inosused(cgp)[i];
  ipref = i * NBBY;
  for (i = 1; i < (1 << NBBY); i <<= 1, ipref++)
    {
      if ((map & i) == 0)
	{
	  cgp->cg_irotor = ipref;
	  goto gotit;
	}
    }
  assert ("inconsistent cg_inosused table" && 0);
 gotit:
  setbit(cg_inosused(cgp), ipref);
  cgp->cg_cs.cs_nifree--;
  sblock->fs_cstotal.cs_nifree--;
  csum[cg].cs_nifree--;
  if ((mode & IFMT) == IFDIR)
    {
      cgp->cg_cs.cs_ndir++;
      sblock->fs_cstotal.cs_ndir++;
      csum[cg].cs_ndir++;
    }
  diskfs_end_catch_exception ();
  return (u_long)(cg * sblock->fs_ipg + ipref);
}

/*
 * Free a block or fragment.
 *
 * The specified block or fragment is placed back in the
 * free map. If a fragment is deallocated, a possible 
 * block reassembly is checked.
 */
void
blkfree(volatile daddr_t bno,
	int size)
{
  struct cg *cgp;
  int cg, blk, frags, bbase;
  int i;
  
  assert ("free of bad sized block" &&(unsigned) size <= sblock->fs_bsize
	  && !fragoff (size) && size != 0);
  cg = dtog(bno);
  if ((unsigned)bno >= sblock->fs_size)
    {
      printf("bad block %ld\n", bno);
      return;
    }

  cgp = (struct cg *)(cgs + sblock->fs_bsize * cg);

  spin_lock (&alloclock);

  if (diskfs_catch_exception ())
    {
      spin_unlock (&alloclock);
      return;
    }

  if (!cg_chkmagic(cgp))
    {
      spin_unlock (&alloclock);
      printf ("Cylinder group %d bad magic number: %ld/%ld\n",
	      cg, cgp->cg_magic, ((struct ocg *)(cgp))->cg_magic);
      diskfs_end_catch_exception ();
      return;
    }
  cgp->cg_time = diskfs_mtime->seconds;
  bno = dtogd(bno);
  if (size == sblock->fs_bsize)
    {
      assert ("inconsistent cg_blskfree table"
	      && !isblock (cg_blksfree (cgp), fragstoblks (bno)));
      setblock(cg_blksfree(cgp), fragstoblks(bno));
      cgp->cg_cs.cs_nbfree++;
      sblock->fs_cstotal.cs_nbfree++;
      csum[cg].cs_nbfree++;
      i = cbtocylno(bno);
      cg_blks(cgp, i)[cbtorpos(bno)]++;
      cg_blktot(cgp)[i]++;
    } 
  else
    {
      bbase = bno - fragnum(bno);
      /*
       * decrement the counts associated with the old frags
       */
      blk = blkmap(cg_blksfree(cgp), bbase);
      fragacct(blk, cgp->cg_frsum, -1);
      /*
       * deallocate the fragment
       */
      frags = numfrags(size);
      for (i = 0; i < frags; i++)
	{
	  assert ("inconsistent cg_blksfree table"
		  && !isset (cg_blksfree (cgp), bno + i));
	  setbit(cg_blksfree(cgp), bno + i);
	}
      cgp->cg_cs.cs_nffree += i;
      sblock->fs_cstotal.cs_nffree += i;
      csum[cg].cs_nffree += i;
      /*
       * add back in counts associated with the new frags
       */
      blk = blkmap(cg_blksfree(cgp), bbase);
      fragacct(blk, cgp->cg_frsum, 1);
      /*
       * if a complete block has been reassembled, account for it
       */
      if (isblock(cg_blksfree(cgp), (daddr_t)fragstoblks(bbase)))
	{
	  cgp->cg_cs.cs_nffree -= sblock->fs_frag;
	  sblock->fs_cstotal.cs_nffree -= sblock->fs_frag;
	  csum[cg].cs_nffree -= sblock->fs_frag;
	  cgp->cg_cs.cs_nbfree++;
	  sblock->fs_cstotal.cs_nbfree++;
	  csum[cg].cs_nbfree++;
	  i = cbtocylno(bbase);
	  cg_blks(cgp, i)[cbtorpos(bbase)]++;
	  cg_blktot(cgp)[i]++;
	}
    }
  spin_unlock (&alloclock);
  diskfs_end_catch_exception ();
}

/*
 * Free an inode.
 *
 * The specified inode is placed back in the free map.
 */
void
diskfs_free_node(struct node *np, mode_t mode)
{
  struct cg *cgp;
  int cg;
  volatile int ino = np->dn->number;
  
  assert ("invalid inode number" && ino < sblock->fs_ipg * sblock->fs_ncg);

  cg = itog(ino);
  cgp = (struct cg *)(cgs + sblock->fs_bsize * cg);

  spin_lock (&alloclock);
  if (diskfs_catch_exception ())
    {
      spin_unlock (&alloclock);
      return;
    }
  
  if (!cg_chkmagic(cgp))
    {
      spin_unlock (&alloclock);
      printf ("Cylinder group %d bad magic number: %ld/%ld\n",
	      cg, cgp->cg_magic, ((struct ocg *)(cgp))->cg_magic);
      diskfs_end_catch_exception ();
      return;
    }
  cgp->cg_time = diskfs_mtime->seconds;
  ino %= sblock->fs_ipg;
  assert ("inconsistent cg_inosused table" && !isclr (cg_inosused (cgp), ino));
  clrbit(cg_inosused(cgp), ino);
  if (ino < cgp->cg_irotor)
    cgp->cg_irotor = ino;
  cgp->cg_cs.cs_nifree++;
  sblock->fs_cstotal.cs_nifree++;
  csum[cg].cs_nifree++;
  if ((mode & IFMT) == IFDIR)
    {
      cgp->cg_cs.cs_ndir--;
      sblock->fs_cstotal.cs_ndir--;
      csum[cg].cs_ndir--;
    }
  spin_unlock (&alloclock);
  diskfs_end_catch_exception ();
}


/*
 * Find a block of the specified size in the specified cylinder group.
 *
 * It is a panic if a request is made to find a block if none are
 * available.
 */
/* This routine expects to be called from inside a diskfs_catch_exception */
static daddr_t
mapsearch(struct cg *cgp,
	  daddr_t bpref,
	  int allocsiz)
{
  daddr_t bno;
  int start, len, loc, i;
  int blk, field, subfield, pos;
  
  /*
   * find the fragment by searching through the free block
   * map for an appropriate bit pattern
   */
  if (bpref)
    start = dtogd(bpref) / NBBY;
  else
    start = cgp->cg_frotor / NBBY;
  len = howmany(sblock->fs_fpg, NBBY) - start;
  loc = scanc((unsigned)len, (u_char *)&cg_blksfree(cgp)[start],
	      (u_char *)fragtbl[sblock->fs_frag],
	      (u_char)(1 << (allocsiz - 1 + (sblock->fs_frag % NBBY))));
  if (loc == 0)
    {
      len = start + 1;
      start = 0;
      loc = scanc((unsigned)len, (u_char *)&cg_blksfree(cgp)[0],
		  (u_char *)fragtbl[sblock->fs_frag],
		  (u_char)(1 << (allocsiz - 1 + (sblock->fs_frag % NBBY))));
      assert ("incosistent cg_blksfree table" && loc);
    }
  bno = (start + len - loc) * NBBY;
  cgp->cg_frotor = bno;
  /*
   * found the byte in the map
   * sift through the bits to find the selected frag
   */
  for (i = bno + NBBY; bno < i; bno += sblock->fs_frag)
    {
      blk = blkmap(cg_blksfree(cgp), bno);
      blk <<= 1;
      field = around[allocsiz];
      subfield = inside[allocsiz];
      for (pos = 0; pos <= sblock->fs_frag - allocsiz; pos++)
	{
	  if ((blk & field) == subfield)
	    return (bno + pos);
	  field <<= 1;
	  subfield <<= 1;
	}
    }
  assert ("inconsistent cg_blksfree table" && 0);
}


