/* File growth and truncation
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

/* Written by Michael I. Bushnell.  */

#include "ufs.h"
#include <string.h>

#ifdef DONT_CACHE_MEMORY_OBJECTS
#define MAY_CACHE 0
#else
#define MAY_CACHE 1
#endif

static int free_indir (struct node *np, daddr_t bno, int level);
static void poke_pages (memory_object_t, vm_offset_t, vm_offset_t);

/* Implement the diskfs_truncate callback; sse <hurd/diskfs.h> for the
   interface description. */
error_t
diskfs_truncate (struct node *np,
		 off_t length)
{
  int offset;
  daddr_t lastiblock[NIADDR], lastblock, bn;
  struct dinode *di = dino (np->dn->number);
  int blocksfreed = 0;
  error_t err;
  int level;
  int i;

  if (length >= np->dn_stat.st_size)
    return 0;

  assert (!diskfs_readonly);

  /* First check to see if this is a kludged symlink; if so
     this is special. */
  if (direct_symlink_extension && S_ISLNK (np->dn_stat.st_mode)
      && np->dn_stat.st_size < sblock->fs_maxsymlinklen)
    {
      error_t err;

      if (err = diskfs_catch_exception ())
	return err;
      bzero (di->di_shortlink + length, np->dn_stat.st_size - length);
      diskfs_end_catch_exception ();
      np->dn_stat.st_size = length;
      np->dn_set_ctime = np->dn_set_mtime = 1;
      return 0;
    }
  
  /* If the file is not being trucated to a block boundary,
     the zero the partial bit in the new last block. */
  offset = blkoff (sblock, length);
  if (offset)
    {
      int bsize;			/* size of new last block */
      int savesize = np->allocsize;
      
      np->allocsize = length;	/* temporary */
      bsize = blksize (sblock, np, lblkno (sblock, length));
      np->allocsize = savesize;
      diskfs_node_rdwr (np, (void *) zeroblock, length, 
			bsize - offset, 1, 0, 0);
      diskfs_file_update (np, 1);
    }

  rwlock_writer_lock (&np->dn->allocptrlock);

  /* Now flush all the data past the new size from the kernel.
     Also force any delayed copies of this data to take place
     immediately.  (We are changing the data implicitly to zeros
     and doing it without the kernels immediate knowledge;
     this forces us to help out the kernel thusly.) */
  if (np->dn->fileinfo)
    {
      mach_port_t obj;
      
      pager_change_attributes (np->dn->fileinfo->p, MAY_CACHE,
			       MEMORY_OBJECT_COPY_NONE, 1);
      obj = diskfs_get_filemap (np);
      mach_port_insert_right (mach_task_self (), obj, obj,
			      MACH_MSG_TYPE_MAKE_SEND);
      poke_pages (obj, round_page (length), round_page (np->allocsize));
      mach_port_deallocate (mach_task_self (), obj);
      pager_flush_some (np->dn->fileinfo->p, round_page (length),
			np->allocsize - length, 1);
    }

  /* Calculate index into node's block list of direct
     and indirect blocks which we want to keep.  Lastblock
     is -1 when the file is truncated to 0. */
  lastblock = lblkno (sblock, length - 1);
  lastiblock[INDIR_SINGLE] = lastblock - NDADDR;
  lastiblock[INDIR_DOUBLE] = lastiblock[INDIR_SINGLE] - NINDIR (sblock);
  lastiblock[INDIR_TRIPLE] = (lastiblock[INDIR_DOUBLE]
			      - NINDIR (sblock) * NINDIR (sblock));
  
  /* lastiblock will now be negative for elements that we should free. */

  /* Update the size on disk; fsck will finish freeing blocks if necessary
     should we crash. */
  np->dn_stat.st_size = length;
  np->dn_set_mtime = 1;
  np->dn_set_ctime = 1;
  diskfs_node_update (np, 1);

  /* Free the blocks. */
  
  err = diskfs_catch_exception ();
  if (err)
    {
      rwlock_writer_unlock (&np->dn->allocptrlock);
      return err;
    }

  /* Indirect blocks first. */
  for (level = INDIR_TRIPLE; level >= INDIR_SINGLE; level--)
    if (lastiblock[level] < 0 && di->di_ib[level])
      {
	int count;
	count = free_indir (np, di->di_ib[level], level);
	blocksfreed += count;
	di->di_ib[level] = 0;
      }

  /* Whole direct blocks or frags */
  for (i = NDADDR - 1; i > lastblock; i--)
    {
      long bsize;

      bn = di->di_db[i];
      if (bn == 0)
	continue;

      bsize = blksize (sblock, np, i);
      ffs_blkfree (np, bn, bsize);
      blocksfreed += btodb (bsize);

      di->di_db[i] = 0;
    }

  /* Finally, check to see if the new last direct block is 
     changing size; if so release any frags necessary. */
  if (lastblock >= 0
      && di->di_db[lastblock])
    {
      long oldspace, newspace;
      
      bn = di->di_db[lastblock];
      oldspace = blksize (sblock, np, lastblock);
      np->allocsize = fragroundup (sblock, length);
      newspace = blksize (sblock, np, lastblock);
      
      assert (newspace);
      
      if (oldspace - newspace)
	{
	  bn += numfrags (sblock, newspace);
	  ffs_blkfree (np, bn, oldspace - newspace);
	  blocksfreed += btodb (oldspace - newspace);
	}
    }
  else
    np->allocsize = fragroundup (sblock, length);
  
  diskfs_end_catch_exception ();

  np->dn_set_ctime = 1;
  diskfs_node_update (np, 1);

  rwlock_writer_unlock (&np->dn->allocptrlock);

  /* Now we can permit delayed copies again. */
  if (np->dn->fileinfo)
    pager_change_attributes (np->dn->fileinfo->p, MAY_CACHE,
			     MEMORY_OBJECT_COPY_DELAY, 0);
  
  return 0;
}

/* Free indirect block BNO of level LEVEL; recursing if necessary
   to free other indirect blocks.  Return the number of disk
   blocks freed. */
static int
free_indir (struct node *np, daddr_t bno, int level)
{
  int count = 0;
  daddr_t *addrs;
  int i;
  struct dirty_indir *d, *prev, *next;
  
  assert (bno);
  
  addrs = indir_block (bno);
  for (i = 0; i < NINDIR (sblock); i++)
    if (addrs[i])
      {
	if (level == INDIR_SINGLE)
	  {
	    ffs_blkfree (np, addrs[i], sblock->fs_bsize);
	    count += btodb (sblock->fs_bsize);
	  }
	else
	  count += free_indir (np, addrs[i], level - 1);
      }
  
  /* Subtlety: this block is no longer necessary; the information
     the kernel has cached corresponding to ADDRS is now unimportant.
     Consider that if this block is allocated to a file, it will then
     be double cached and the kernel might decide to write out
     the disk_image version of the block.  So we have to flush
     the block from the kernel's memory, making sure we do it
     synchronously--and BEFORE we attach it to the free list
     with ffs_blkfree.  */
  pager_flush_some (diskpager->p, fsaddr (sblock, bno), sblock->fs_bsize, 1);

  /* We should also take this block off the inode's list of 
     dirty indirect blocks if it's there. */
  prev = 0;
  d = np->dn->dirty;
  while (d)
    {
      next = d->next;
      if (d->bno == bno)
	{
	  if (prev)
	    prev->next = next;
	  else
	    np->dn->dirty = next;
	  free (d);
	}
      else
	{
	  prev = d;
	  next = d->next;
	}
      d = next;
    }

  /* Free designated block */
  ffs_blkfree (np, bno, sblock->fs_bsize);
  count += btodb (sblock->fs_bsize);

  return count;
}



/* Implement the diskfs_grow callback; see <hurd/diskfs.h> for the 
   interface description. */
error_t
diskfs_grow (struct node *np,
	     off_t end,
	     struct protid *cred)
{
  daddr_t lbn, olbn;
  int size, osize;
  error_t err;
  struct dinode *di = dino (np->dn->number);
  off_t poke_off;
  size_t poke_len = 0;
  
  /* Zero an sblock->fs_bsize piece of disk starting at BNO, 
     synchronously.  We do this on newly allocated indirect
     blocks before setting the pointer to them to ensure that an
     indirect block absolutely never points to garbage. */
  void zero_disk_block (int bno)
    {
      bzero (indir_block (bno), sblock->fs_bsize);
      sync_disk_blocks (bno, sblock->fs_bsize, 1);
    };

  /* Check to see if we don't actually have to do anything */
  if (end <= np->allocsize)
    return 0;

  assert (!diskfs_readonly);

  /* The new last block of the file. */
  lbn = lblkno (sblock, end - 1);

  /* This is the size of that block if it is in the NDADDR array. */
  size = fragroundup (sblock, blkoff (sblock, end));
  if (size == 0)
    size = sblock->fs_bsize;
  
  rwlock_writer_lock (&np->dn->allocptrlock);

  /* The old last block of the file. */
  olbn = lblkno (sblock, np->allocsize - 1);
  
  /* This is the size of that block if it is in the NDADDR array. */
  osize = fragroundup (sblock, blkoff (sblock, np->allocsize));
  if (osize == 0)
    osize = sblock->fs_bsize;

  /* If this end point is a new block and the file currently
     has a fragment, then expand the fragment to a full block. */
  if (np->allocsize && olbn < NDADDR && olbn < lbn)
    {
      if (osize < sblock->fs_bsize)
	{
	  daddr_t old_pbn, bno;
	  err = ffs_realloccg (np, olbn,
				 ffs_blkpref (np, lbn, lbn, di->di_db),
				 osize, sblock->fs_bsize, &bno, cred);
	  if (err)
	    goto out;
	  old_pbn = di->di_db[olbn];
	  di->di_db[olbn] = bno;
	  np->dn_set_ctime = 1;
	  
	  dev_write_sync (fsbtodb (sblock, bno) + btodb (osize),
			  zeroblock, sblock->fs_bsize - osize);

	  if (bno != old_pbn)
	    {
	      /* Make sure the old contents get written out
		 to the new address by poking the pages. */
	      poke_off = olbn * sblock->fs_bsize;
	      poke_len = osize;
	    }
	}
    }

  if (lbn < NDADDR)
    {
      daddr_t bno, old_pbn = di->di_db[lbn];
	 
      if (old_pbn != 0)
	{
	  /* The last block is already allocated.  Therefore we
	     must be expanding the fragment.  Make sure that's really
	     what we're up to. */
	  assert (size > osize);
	  assert (lbn == olbn);
	  
	  err = ffs_realloccg (np, lbn, 
			       ffs_blkpref (np, lbn, lbn, di->di_db),
			       osize, size, &bno, cred);
	  if (err)
	    goto out;
	  
	  di->di_db[lbn] = bno;
	  np->dn_set_ctime = 1;
	  
	  dev_write_sync (fsbtodb (sblock, bno) + btodb (osize),
			  zeroblock, size - osize);

	  if (bno != old_pbn)
	    {
	      assert (!poke_len);
	      
	      /* Make sure the old contents get written out to
		 the new address by poking the pages. */
	      poke_off = lbn * sblock->fs_bsize;
	      poke_len = osize;
	    }
	}
      else
	{
	  /* Allocate a new last block. */
	  err = ffs_alloc (np, lbn, 
			   ffs_blkpref (np, lbn, lbn, di->di_db),
			   size, &bno, cred);
	  if (err)
	    goto out;
	  
	  di->di_db[lbn] = bno;
	  np->dn_set_ctime = 1;

	  dev_write_sync (fsbtodb (sblock, bno), zeroblock, size);
	}
    }
  else
    {
      struct iblock_spec indirs[NIADDR + 1];
      daddr_t *siblock;
      daddr_t bno;
      
      /* Count the number of levels of indirection. */
      err = fetch_indir_spec (np, lbn, indirs);
      if (err)
	goto out;
      
      /* Make sure we didn't miss the NDADDR case
	 above somehow. */
      assert (indirs[0].offset != -1);
      
      /* See if we need a triple indirect block; fail if so. */
      assert (indirs[1].offset == -1 || indirs[2].offset == -1);
      
      /* Check to see if this block is allocated.  If it is
	 that's an error. */
      assert (indirs[0].bno == 0);
      
      /* We need to set SIBLOCK to the single indirect block
	 array; see if the single indirect block is allocated. */
      if (indirs[1].bno == 0)
	{
	  /* Allocate it. */
	  if (indirs[1].offset == -1)
	    {
	      err = ffs_alloc (np, lbn,
			       ffs_blkpref (np, lbn, INDIR_SINGLE, di->di_ib),
			       sblock->fs_bsize, &bno, 0);
	      if (err)
		goto out;
	      zero_disk_block (bno);
	      indirs[1].bno = di->di_ib[INDIR_SINGLE] = bno;
	    }
	  else
	    {
	      daddr_t *diblock;
	      
	      /* We need to set diblock to the double indirect block
		 array; see if the double indirect block is allocated. */
	      if (indirs[2].bno == 0)
		{
		  /* This assert because triple indirection is not
		     supported. */
		  assert (indirs[2].offset == -1);
		  err = ffs_alloc (np, lbn,
				   ffs_blkpref (np, lbn, 
						INDIR_DOUBLE, di->di_ib),
				   sblock->fs_bsize, &bno, 0);
		  if (err)
		    goto out;
		  zero_disk_block (bno);
		  indirs[2].bno = di->di_ib[INDIR_DOUBLE] = bno;
		}
	      
	      diblock = indir_block (indirs[2].bno);
	      mark_indir_dirty (np, indirs[2].bno);
	      
	      /* Now we can allocate the single indirect block */
	      err = ffs_alloc (np, lbn, 
			       ffs_blkpref (np, lbn, 
					    indirs[1].offset, diblock),
			       sblock->fs_bsize, &bno, 0);
	      if (err)
		goto out;
	      zero_disk_block (bno);
	      indirs[1].bno = diblock[indirs[1].offset] = bno;
	    }
	}
      
      siblock = indir_block (indirs[1].bno);
      mark_indir_dirty (np, indirs[1].bno);

      /* Now we can allocate the data block. */
      err = ffs_alloc (np, lbn, 
		       ffs_blkpref (np, lbn, indirs[0].offset, siblock),
		       sblock->fs_bsize, &bno, 0);
      if (err)
	goto out;
      indirs[0].bno = siblock[indirs[0].offset] = bno;
      dev_write_sync (fsbtodb (sblock, bno), zeroblock, sblock->fs_bsize);
    }
  
 out:
  if (!err)
    {
      int newallocsize;
      if (lbn < NDADDR)
	newallocsize = (lbn - 1) * sblock->fs_bsize + size;
      else
	newallocsize = lbn * sblock->fs_bsize;
      assert (newallocsize > np->allocsize);
      np->allocsize = newallocsize;
    }

  rwlock_writer_unlock (&np->dn->allocptrlock);

  /* If we expanded a fragment, then POKE_LEN will be set.
     We need to poke the requested amount of the memory object
     so that the kernel will write out the data to the new location
     at a suitable time. */
  if (poke_len)
    {
      mach_port_t obj;
      
      obj = diskfs_get_filemap (np);
      mach_port_insert_right (mach_task_self (), obj, obj,
			      MACH_MSG_TYPE_MAKE_SEND);
      poke_pages (obj, trunc_page (poke_off),
		  round_page (poke_off + poke_len));
      mach_port_deallocate (mach_task_self (), obj);
    }

  return err;
}

/* Write something to each page from START to END inclusive of memory
   object OBJ, but make sure the data doesns't actually change. */
static void
poke_pages (memory_object_t obj,
	    vm_offset_t start,
	    vm_offset_t end)
{
  vm_address_t addr, poke;
  vm_size_t len;
  error_t err;
  
  while (start < end)
    {
      len = 8 * vm_page_size;
      if (len > end - start)
	len = end - start;
      addr = 0;
      err = vm_map (mach_task_self (), &addr, len, 0, 1, obj, start, 0,
		    VM_PROT_WRITE|VM_PROT_READ, VM_PROT_READ|VM_PROT_WRITE, 0);
      if (!err)
	{
	  for (poke = addr; poke < addr + len; poke += vm_page_size)
	    *(volatile int *)poke = *(volatile int *)poke;
	  vm_deallocate (mach_task_self (), addr, len);
	}
      start += len;
    }
}

