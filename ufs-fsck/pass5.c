/* Pass 5 of GNU fsck -- check allocation maps and summaries
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

/* From ../ufs/subr.c: */

/*
 * Update the frsum fields to reflect addition or deletion 
 * of some frags.
 */
static void
ffs_fragacct(fs, fragmap, fraglist, cnt)
        struct fs *fs;
	int fragmap;
	long fraglist[];
	int cnt;
{
	int inblk;
	register int field, subfield;
	register int siz, pos;

	inblk = (int)(fragtbl[fs->fs_frag][fragmap]) << 1;
	fragmap <<= 1;
	for (siz = 1; siz < fs->fs_frag; siz++) {
		if ((inblk & (1 << (siz + (fs->fs_frag % NBBY)))) == 0)
			continue;
		field = around[siz];
		subfield = inside[siz];
		for (pos = siz; pos <= fs->fs_frag; pos++) {
			if ((fragmap & field) == subfield) {
				fraglist[siz] += cnt;
				pos += siz;
				field <<= siz;
				subfield <<= siz;
			}
			field <<= 1;
			subfield <<= 1;
		}
	}
}

void
pass5 ()
{
  struct cg *newcg, *cg;
  struct ocg *newocg;
  int savednrpos = 0;
  struct csum cstotal;
  int i, j;
  int c;
  daddr_t d;
  struct csum *sbcsums;

  int basesize;			/* size of cg not counting flexibly sized */
  int sumsize;			/* size of block totals and pos tbl */
  int mapsize;			/* size of inode map + block map */

  int writesb;
  int writecg;
  int writecsum;

  writesb = 0;
  writecsum = 0;

  cg = alloca (sblock->fs_cgsize);

  newcg = alloca (sblock->fs_cgsize);
  newocg = (struct ocg *)newcg;

  sbcsums = alloca (fragroundup (sblock, sblock->fs_cssize));

  readblock (fsbtodb (sblock, sblock->fs_csaddr), sbcsums, 
	     fragroundup (sblock, sblock->fs_cssize));

  /* Construct a CG structure; initialize everything that's the same
     in each cylinder group. */
  bzero (newcg, sblock->fs_cgsize);
  newcg->cg_niblk = sblock->fs_ipg;
  switch (sblock->fs_postblformat)
    {
    case FS_42POSTBLFMT:
      /* Initialize size information */
      basesize = (char *)(&newocg->cg_btot[0]) - (char *)(&newocg->cg_link);
      sumsize = &newocg->cg_iused[0] - (char *)(&newocg->cg_btot[0]);
      mapsize = (&newocg->cg_free[howmany(sblock->fs_fpg, NBBY)]
		 - (u_char *)&newocg->cg_iused[0]);
      savednrpos = sblock->fs_nrpos;
      sblock->fs_nrpos = 8;
      break;
      
    case FS_DYNAMICPOSTBLFMT:
      /* Set fields unique to new cg structure */
      newcg->cg_btotoff = &newcg->cg_space[0] - (u_char *)(&newcg->cg_link);
      newcg->cg_boff = newcg->cg_btotoff + sblock->fs_cpg * sizeof (long);
      newcg->cg_iusedoff = newcg->cg_boff + (sblock->fs_cpg
					     * sblock->fs_nrpos 
					     * sizeof (short));
      newcg->cg_freeoff = newcg->cg_iusedoff + howmany (sblock->fs_ipg, NBBY);

      if (sblock->fs_contigsumsize <= 0)
	{
	  newcg->cg_nextfreeoff = 
	    (newcg->cg_freeoff
	     + howmany (sblock->fs_cpg * sblock->fs_spc / NSPF (sblock),
			NBBY));
	}
      else
	{
	  newcg->cg_clustersumoff = 
	    (newcg->cg_freeoff
	     + howmany (sblock->fs_cpg * sblock->fs_spc / NSPF (sblock), NBBY)
	     - sizeof (long));
	  newcg->cg_clustersumoff = 
	    roundup (newcg->cg_clustersumoff, sizeof (long));
	  newcg->cg_clusteroff = 
	    (newcg->cg_clustersumoff 
	     + (sblock->fs_contigsumsize + 1) * sizeof (long));
	  newcg->cg_nextfreeoff = 
	    (newcg->cg_clusteroff 
	     + howmany (sblock->fs_cpg * sblock->fs_spc / NSPB (sblock), 
			NBBY));
	}

      newcg->cg_magic = CG_MAGIC;

      /* Set map sizes */
      basesize = &newcg->cg_space[0] - (u_char *)(&newcg->cg_link);
      sumsize = newcg->cg_iusedoff - newcg->cg_btotoff;
      mapsize = newcg->cg_nextfreeoff - newcg->cg_iusedoff;
      break;

    default:
      errexit ("UNKNOWN POSTBL FORMAT");
    }
  
  bzero (&cstotal, sizeof (struct csum));

  /* Mark fragments past the end of the filesystem as used. */
  j = blknum (sblock, sblock->fs_size + sblock->fs_frag - 1);
  for (i = sblock->fs_size; i < j; i++)
    setbmap (i);
  
  /* Now walk through the cylinder groups, checking each one. */
  for (c = 0; c < sblock->fs_ncg; c++)
    {
      int dbase, dmax;
      
      /* Read the cylinder group structure */
      readblock (fsbtodb (sblock, cgtod (sblock, c)), cg, sblock->fs_cgsize);
      writecg = 0;
      
      if (!cg_chkmagic (cg))
	warning (1, "CG %d: BAD MAGIC NUMBER", c);
      
      /* Compute first and last data block addresses in this group */
      dbase = cgbase (sblock, c);
      dmax = dbase + sblock->fs_fpg;
      if (dmax > sblock->fs_size)
	dmax = sblock->fs_size;
      
      /* Initialize newcg fully; values from cg for those
	 we can't check. */
      newcg->cg_time = cg->cg_time;
      newcg->cg_cgx = c;
      if (c == sblock->fs_ncg - 1)
	newcg->cg_ncyl = sblock->fs_ncyl % sblock->fs_cpg;
      else
	newcg->cg_ncyl = sblock->fs_cpg;
      newcg->cg_ndblk = dmax - dbase;
      if (sblock->fs_contigsumsize > 0)
	newcg->cg_nclusterblks = newcg->cg_ndblk / sblock->fs_frag;
      newcg->cg_cs.cs_ndir = 0;
      newcg->cg_cs.cs_nffree = 0;
      newcg->cg_cs.cs_nbfree = 0;
      newcg->cg_cs.cs_nifree = sblock->fs_ipg;

      /* Check these for basic viability; if they are wrong
	 then clear them. */
      newcg->cg_rotor = cg->cg_rotor;
      newcg->cg_frotor = cg->cg_frotor;
      newcg->cg_irotor = cg->cg_irotor;
      if (newcg->cg_rotor > newcg->cg_ndblk)
	{
	  problem (0, "ILLEGAL ROTOR VALUE IN CG %d", c);
	  if (preen || reply ("FIX"))
	    {
	      newcg->cg_rotor = 0;
	      cg->cg_rotor = 0;
	      writecg = 1;
	      pfix ("FIXED");
	    }
	}
      if (newcg->cg_frotor > newcg->cg_ndblk)
	{
	  problem (0, "ILLEGAL FROTOR VALUE IN CG %d", c);
	  if (preen || reply ("FIX"))
	    {
	      newcg->cg_frotor = 0;
	      cg->cg_frotor = 0;
	      writecg = 1;
	      pfix ("FIXED");
	    }
	}
      if (newcg->cg_irotor > newcg->cg_niblk)
	{
	  problem (0, "ILLEGAL IROTOR VALUE IN CG %d", c);
	  if (preen || reply ("FIX"))
	    {
	      newcg->cg_irotor = 0;
	      cg->cg_irotor = 0;
	      writecg = 1;
	      pfix ("FIXED");
	    }
	}
      
      /* Zero the block maps and summary areas */
      bzero (&newcg->cg_frsum[0], sizeof newcg->cg_frsum);
      bzero (&cg_blktot (newcg)[0], sumsize + mapsize);
      if (sblock->fs_postblformat == FS_42POSTBLFMT)
	newocg->cg_magic = CG_MAGIC;

      /* Walk through each inode, accounting for it in
	 the inode map and in newcg->cg_cs. */
      /* In this loop, J is the inode number, and I is the
	 inode number relative to this CG. */
      j = sblock->fs_ipg * c;
      for (i = 0; i < sblock->fs_ipg; j++, i++)
	switch (inodestate[j])
	  {
	  case DIRECTORY:
	  case DIRECTORY | DIR_REF:
	  case BADDIR:
	    newcg->cg_cs.cs_ndir++;
	    /* Fall through... */
	  case REG:
	    newcg->cg_cs.cs_nifree--;
	    setbit (cg_inosused (newcg), i);
	    /* Fall through... */
	  case UNALLOC:
	    break;

	  default:
	    errexit ("UNKNOWN STATE I=%d", j);
	  }
      /* Account for inodes 0 and 1 */
      if (c == 0)
	for (i = 0; i < ROOTINO; i++)
	  {
	    setbit (cg_inosused (newcg), i);
	    newcg->cg_cs.cs_nifree--;
	  }
      
      /* Walk through each data block, accounting for it in 
	 the block map and in newcg->cg_cs. */
      /* In this look, D is the block number and I is the
	 block number relative to this CG. */
      for (i = 0, d = dbase;
	   d < dmax;
	   d += sblock->fs_frag, i += sblock->fs_frag)
	{
	  int frags = 0;
	  
	  /* Set each free frag of this block in the block map;
	     count how many frags were free. */
	  for (j = 0; j < sblock->fs_frag; j++)
	    {
	      if (testbmap (d + j))
		continue;
	      setbit (cg_blksfree (newcg), i + j);
	      frags++;
	    }
	  
	  /* If all the frags were free, then count this as 
	     a free block too. */
	  if (frags == sblock->fs_frag)
	    {
	      newcg->cg_cs.cs_nbfree++;
	      j = cbtocylno (sblock, i);
	      cg_blktot(newcg)[j]++;
	      cg_blks(sblock, newcg, j)[cbtorpos(sblock, i)]++;
	      if (sblock->fs_contigsumsize > 0)
		setbit (cg_clustersfree (newcg), i / sblock->fs_frag);
	    }
	  else if (frags)
	    {
	      /* Partial; account for the frags. */
	      int blk;
	      newcg->cg_cs.cs_nffree += frags;
	      blk = blkmap (sblock, cg_blksfree (newcg), i);
	      ffs_fragacct (sblock, blk, newcg->cg_frsum, 1);
	    }
	}
      
      if (sblock->fs_contigsumsize > 0)
	{
	  long *sump = cg_clustersum (newcg);
	  u_char *mapp = cg_clustersfree (newcg);
	  int map = *mapp++;
	  int bit = 1;
	  int run = 0;
	  
	  for (i = 0; i < newcg->cg_nclusterblks; i++)
	    {
	      if ((map & bit) != 0)
		run++;
	      else if (run)
		{
		  if (run > sblock->fs_contigsumsize)
		    run = sblock->fs_contigsumsize;
		  sump[run]++;
		  run = 0;
		}

	      if ((i & (NBBY - 1)) != (NBBY - 1))
		bit <<= 1;
	      else
		{
		  map = *mapp++;
		  bit = 1;
		}
	    }
	  if (run != 0)
	    {
	      if (run > sblock->fs_contigsumsize)
		run = sblock->fs_contigsumsize;
	      sump[run]++;
	    }
	}

      /* Add this cylinder group's totals into the superblock's
	 totals. */
      cstotal.cs_nffree += newcg->cg_cs.cs_nffree;
      cstotal.cs_nbfree += newcg->cg_cs.cs_nbfree;
      cstotal.cs_nifree += newcg->cg_cs.cs_nifree;
      cstotal.cs_ndir += newcg->cg_cs.cs_ndir;

      /* Check counts in superblock */
      if (bcmp (&newcg->cg_cs, &sbcsums[c], sizeof (struct csum)))
	{
	  problem (0, "FREE BLK COUNTS FOR CG %d WRONG IN SUPERBLOCK", c);
	  if (preen || reply ("FIX"))
	    {
	      bcopy (&newcg->cg_cs, &sbcsums[c], sizeof (struct csum));
	      writecsum = 1;
	      pfix ("FIXED");
	    }
	}
      
      /* Check inode and block maps */
      if (bcmp (cg_inosused (newcg), cg_inosused (cg), mapsize))
	{
	  problem (0, "BLKS OR INOS MISSING IN CG %d BIT MAPS", c);
	  if (preen || reply ("FIX"))
	    {
	      bcopy (cg_inosused (newcg), cg_inosused (cg), mapsize);
	      writecg = 1;
	      pfix ("FIXED");
	    }
	}
      
      if (bcmp (&cg_blktot(newcg)[0], &cg_blktot(cg)[0], sumsize))
	{
	  problem (0, "SUMMARY INFORMATION FOR CG %d BAD", c);
	  if (preen || reply ("FIX"))
	    {
	      bcopy (&cg_blktot(newcg)[0], &cg_blktot(cg)[0], sumsize);
	      writecg = 1;
	      pfix ("FIXED");
	    }
	}
      
      if (bcmp (newcg, cg, basesize))
	{
	  problem (0, "CYLINDER GROUP %d BAD", c);
	  if (preen || reply ("FIX"))
	    {
	      bcopy (newcg, cg, basesize);
	      writecg = 1;
	      pfix ("FIXED");
	    }
	}

      if (writecg)
	writeblock (fsbtodb (sblock, cgtod (sblock, c)), 
		    cg, sblock->fs_cgsize);
    }
  
  /* Restore nrpos */
  if (sblock->fs_postblformat == FS_42POSTBLFMT)
    sblock->fs_nrpos = savednrpos;
  
  if (bcmp (&cstotal, &sblock->fs_cstotal, sizeof (struct csum)))
    {
      problem (0, "TOTAL FREE BLK COUNTS WRONG IN SUPERBLOCK");
      if (preen || reply ("FIX"))
	{
	  bcopy (&cstotal, &sblock->fs_cstotal, sizeof (struct csum));
	  sblock->fs_ronly = 0;
	  sblock->fs_fmod = 0;
	  writesb = 1;
	  pfix ("FIXED");
	}
    }

  if (sblock->fs_clean == 0 && !fix_denied)
    {
      problem (0, fsmodified ? "FILESYSTEM MODIFIED" : "FILESYSTEM UNCLEAN");
      if (preen || reply ("MARK CLEAN"))
	{
	  sblock->fs_clean = 1;
	  writesb = 1;
	  pfix ("MARKED CLEAN");
	}
    }
  
  if (writesb)
    writeblock (SBLOCK, sblock, SBSIZE);

  if (writecsum)
    {
      struct csum *test;
      
      writeblock (fsbtodb (sblock, sblock->fs_csaddr), sbcsums, 
		  fragroundup (sblock, sblock->fs_cssize));

      test = alloca (fragroundup (sblock, sblock->fs_cssize));
      readblock (fsbtodb (sblock, sblock->fs_csaddr), test,
		 fragroundup (sblock, sblock->fs_cssize));
      if (bcmp (test, sbcsums, fragroundup (sblock, sblock->fs_cssize)))
	warning (0, "CSUM WRITE INCONSISTENT");
    }
}
