/* File growth and truncation
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1999 Free Software Foundation

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

static int indir_release (struct node *np, daddr_t bno, int level);
static void poke_pages (memory_object_t, vm_offset_t, vm_offset_t);

/* Implement the diskfs_truncate callback; sse <hurd/diskfs.h> for the
   interface description. */
error_t
diskfs_truncate (struct node *np,
		 off_t length)
{
  int offset;
  struct dinode *di = dino (np->dn->number);
  volatile int blocksfreed = 0;
  error_t err;
  int i;
  struct iblock_spec indirs[NIADDR + 1];
  volatile daddr_t lbn;
  struct user_pager_info *upi;

  if (length >= np->dn_stat.st_size)
    return 0;

  diskfs_check_readonly ();
  assert (!diskfs_readonly);

  /* First check to see if this is a kludged symlink; if so
     this is special. */
  if (direct_symlink_extension && S_ISLNK (np->dn_stat.st_mode)
      && np->dn_stat.st_size < sblock->fs_maxsymlinklen)
    {
      error_t err;

      err = diskfs_catch_exception ();
      if (err)
	return err;
      bzero ((char *)di->di_shortlink + length, np->dn_stat.st_size - length);
      record_poke (di, sizeof (struct dinode));
      diskfs_end_catch_exception ();
      np->dn_stat.st_size = length;
      np->dn_set_ctime = np->dn_set_mtime = 1;
      diskfs_node_update (np, 1);
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
      diskfs_node_rdwr (np, zeroblock, length, bsize - offset, 1, 0, 0);
      diskfs_file_update (np, 1);
    }

  /* Now flush all the data past the new size from the kernel.
     Also force any delayed copies of this data to take place
     immediately.  (We are implicitly changing the data to zeros
     and doing it without the kernel's immediate knowledge;
     accordingl we must help out the kernel thusly.) */
  pthread_spin_lock (&node2pagelock);
  upi = np->dn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  pthread_spin_unlock (&node2pagelock);

  if (upi)
    {
      mach_port_t obj;

      pager_change_attributes (upi->p, MAY_CACHE,
			       MEMORY_OBJECT_COPY_NONE, 1);
      obj = diskfs_get_filemap (np, VM_PROT_READ | VM_PROT_WRITE);
      if (obj != MACH_PORT_NULL)
	{
	  /* XXX should cope with errors from diskfs_get_filemap */
	  poke_pages (obj, round_page (length), round_page (np->allocsize));
	  mach_port_deallocate (mach_task_self (), obj);
	  pager_flush_some (upi->p, round_page (length),
			    np->allocsize - length, 1);
	}
      ports_port_deref (upi->p);
    }

  pthread_rwlock_wrlock (&np->dn->allocptrlock);

  /* Update the size on disk; fsck will finish freeing blocks if necessary
     should we crash. */
  np->dn_stat.st_size = length;
  np->dn_set_mtime = 1;
  np->dn_set_ctime = 1;
  diskfs_node_update (np, 1);

  /* Find out the location information for the last block to
     be retained */
  lbn = lblkno (sblock, length - 1);
  err = fetch_indir_spec (np, lbn, indirs);
  /* err XXX */

  /* We don't support triple indirs */
  assert (indirs[3].offset == -2);

  err = diskfs_catch_exception ();
  /* err XXX */

  /* BSD carefully finds out how far to clear; it's vastly simpler
     to just clear everything after the new last block. */

  /* Free direct blocks */
  if (indirs[0].offset < 0)
    {
      /* ...mapped from the inode. */
      for (i = lbn + 1; i < NDADDR; i++)
	if (di->di_db[i])
	  {
	    long bsize = blksize (sblock, np, i);
	    ffs_blkfree (np, read_disk_entry (di->di_db[i]), bsize);
	    di->di_db[i] = 0;
	    blocksfreed += btodb (bsize);
	  }
    }
  else
    {
      /* ... or mapped from sindir */
      if (indirs[1].bno)
	{
	  daddr_t *sindir = indir_block (indirs[1].bno);
	  for (i = indirs[0].offset + 1; i < NINDIR (sblock); i++)
	    if (sindir[i])
	      {
		ffs_blkfree (np, read_disk_entry (sindir[i]), 
			     sblock->fs_bsize);
		sindir[i] = 0;
		blocksfreed += btodb (sblock->fs_bsize);
	      }
	  record_poke (sindir, sblock->fs_bsize);
	}
    }

  /* Free single indirect blocks */
  if (indirs[1].offset < 0)
    {
      /* ...mapped from the inode */
      if (di->di_ib[INDIR_SINGLE] && indirs[1].offset == -2)
	{
	  blocksfreed += indir_release (np, 
					read_disk_entry (di->di_ib
							 [INDIR_SINGLE]),
					INDIR_SINGLE);
	  di->di_ib[INDIR_SINGLE] = 0;
	}
    }
  else
    {
      /* ...or mapped from dindir */
      if (indirs[2].bno)
	{
	  daddr_t *dindir = indir_block (indirs[2].bno);
	  for (i = indirs[1].offset + 1; i < NINDIR (sblock); i++)
	    if (dindir[i])
	      {
		blocksfreed += indir_release (np, 
					      read_disk_entry (dindir[i]),
					      INDIR_SINGLE);
		dindir[i] = 0;
	      }
	  record_poke (dindir, sblock->fs_bsize);
	}
    }

  /* Free double indirect block */
  assert (indirs[2].offset < 0); /* which must be mapped from the inode */
  if (indirs[2].offset == -2)
    {
      if (di->di_ib[INDIR_DOUBLE])
	{
	  blocksfreed += indir_release (np, 
					read_disk_entry (di->di_ib
							 [INDIR_DOUBLE]),
					INDIR_DOUBLE);
	  di->di_ib[INDIR_DOUBLE] = 0;
	}
    }

  /* Finally, check to see if the new last direct block is
     changing size; if so release any frags necessary. */
  if (lbn >= 0 && lbn < NDADDR && di->di_db[lbn])
    {
      long oldspace, newspace;
      daddr_t bn;

      bn = read_disk_entry (di->di_db[lbn]);
      oldspace = blksize (sblock, np, lbn);
      np->allocsize = fragroundup (sblock, length);
      newspace = blksize (sblock, np, lbn);

      assert (newspace);

      if (oldspace - newspace)
	{
	  bn += numfrags (sblock, newspace);
	  ffs_blkfree (np, bn, oldspace - newspace);
	  blocksfreed += btodb (oldspace - newspace);
	}
    }
  else
    {
      if (lbn > NDADDR)
	np->allocsize = blkroundup (sblock, length);
      else
	np->allocsize = fragroundup (sblock, length);
    }

  record_poke (di, sizeof (struct dinode));

  np->dn_stat.st_blocks -= blocksfreed;
  np->dn_set_ctime = 1;
  diskfs_node_update (np, 1);

  pthread_rwlock_unlock (&np->dn->allocptrlock);
  /* Wake up any remaining sleeping readers.
     This sequence of three calls is now necessary whenever we acquire a write
     lock on allocptrlock. If we do not, we may leak some readers. */
  pthread_mutex_lock (&np->dn->waitlock);
  pthread_cond_broadcast (&np->dn->waitcond);
  pthread_mutex_unlock (&np->dn->waitlock);

  /* At this point the last block (as defined by np->allocsize)
     might not be allocated.  We need to allocate it to maintain
     the rule that the last block of a file is always allocated. */

  if (np->allocsize && indirs[0].bno == 0)
    {
      /* The strategy is to reduce LBN until we get one that's allocated;
	 then reduce allocsize accordingly, then call diskfs_grow. */

      do
	err = fetch_indir_spec (np, --lbn, indirs);
      /* err XXX */
      while (indirs[0].bno == 0 && lbn >= 0);

      assert ((lbn + 1) * sblock->fs_bsize < np->allocsize);
      np->allocsize = (lbn + 1) * sblock->fs_bsize;

      diskfs_grow (np, length, 0);
    }

  diskfs_end_catch_exception ();

  /* Now we can permit delayed copies again. */
  pthread_spin_lock (&node2pagelock);
  upi = np->dn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  pthread_spin_unlock (&node2pagelock);
  if (upi)
    {
      pager_change_attributes (upi->p, MAY_CACHE,
			       MEMORY_OBJECT_COPY_DELAY, 0);
      ports_port_deref (upi->p);
    }

  return err;
}

/* Free indirect block BNO of level LEVEL; recursing if necessary
   to free other indirect blocks.  Return the number of disk
   blocks freed. */
static int
indir_release (struct node *np, daddr_t bno, int level)
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
	    ffs_blkfree (np, read_disk_entry (addrs[i]), sblock->fs_bsize);
	    count += btodb (sblock->fs_bsize);
	  }
	else
	  count += indir_release (np, read_disk_entry (addrs[i]), level - 1);
      }

  /* Subtlety: this block is no longer necessary; the information
     the kernel has cached corresponding to ADDRS is now unimportant.
     Consider that if this block is allocated to a file, it will then
     be double cached and the kernel might decide to write out
     the disk_image version of the block.  So we have to flush
     the block from the kernel's memory, making sure we do it
     synchronously--and BEFORE we attach it to the free list
     with ffs_blkfree.  */
  pager_flush_some (diskfs_disk_pager, fsaddr (sblock, bno), sblock->fs_bsize, 1);

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


/* Offer data at BUF from START of LEN bytes of file NP. */
void
offer_data (struct node *np,
	    off_t start,
	    size_t len,
	    vm_address_t buf)
{
  vm_address_t addr;
  
  len = round_page (len);
  
  assert (start % vm_page_size == 0);
  
  assert (np->dn->fileinfo);
  for (addr = start; addr < start + len; addr += vm_page_size)
    pager_offer_page (np->dn->fileinfo->p, 1, 0, addr, buf + (addr - start));
}

/* Logical block LBN of node NP has been extended with ffs_realloccg.
   It used to be allocated at OLD_PBN and is now at NEW_PBN.  The old
   size was OLD_SIZE; it is now NEW_SIZE bytes long.  Arrange for the data
   on disk to be kept consistent, and free the old block if it has moved.
   Return one iff we've actually moved data around on disk.  */
int
block_extended (struct node *np, 
		daddr_t lbn,
		daddr_t old_pbn,
		daddr_t new_pbn,
		size_t old_size,
		size_t new_size)
{
  /* Make sure that any pages of this block which just became allocated
     don't get paged in from disk. */
  if (round_page (old_size) < round_page (new_size))
    offer_data (np, lbn * sblock->fs_bsize + round_page (old_size), 
		round_page (new_size) - round_page (old_size),
		(vm_address_t)zeroblock);

  if (old_pbn != new_pbn)
    {
      memory_object_t mapobj;
      error_t err;
      vm_address_t mapaddr;
      volatile int *pokeaddr;

      /* Map in this part of the file */
      mapobj = diskfs_get_filemap (np, VM_PROT_WRITE | VM_PROT_READ);

      /* XXX Should cope with errors from diskfs_get_filemap and back
         out the operation here. */
      assert (mapobj);

      err = vm_map (mach_task_self (), &mapaddr, round_page (old_size), 0, 1,
		    mapobj, lbn * sblock->fs_bsize, 0, 
		    VM_PROT_READ|VM_PROT_WRITE, VM_PROT_READ|VM_PROT_WRITE, 0);
      assert_perror (err);
      
      /* Allow these pageins to occur even though we're holding the lock */
      pthread_spin_lock (&unlocked_pagein_lock);
      np->dn->fileinfo->allow_unlocked_pagein = lbn * sblock->fs_bsize;
      np->dn->fileinfo->unlocked_pagein_length = round_page (old_size);
      pthread_spin_unlock (&unlocked_pagein_lock);

      /* Make sure all waiting pageins see this change. */
      /* BDD - Is this sane? */
      /* TD - No... no it wasn't. But, it looked right. */
       /*
	 This new code should, SHOULD, behave as the original code did.
	 This will wake up all readers waiting on the lock. This code favors
	 strongly writers, but, as of making this change, pthreads favors
	 writers, and cthreads did favor writers.
       */
      pthread_mutex_lock (&np->dn->waitlock);
      pthread_cond_broadcast (&np->dn->waitcond);
      pthread_mutex_unlock (&np->dn->waitlock);

      /* Force the pages in core and make sure they are dirty */
      for (pokeaddr = (int *)mapaddr; 
	   pokeaddr < (int *) (mapaddr + round_page (old_size));
	   pokeaddr += vm_page_size / sizeof (*pokeaddr))
	*pokeaddr = *pokeaddr;

      /* Turn off the special pagein permission */
      pthread_spin_lock (&unlocked_pagein_lock);
      np->dn->fileinfo->allow_unlocked_pagein = 0;
      np->dn->fileinfo->unlocked_pagein_length = 0;
      pthread_spin_unlock (&unlocked_pagein_lock);

      /* Undo mapping */
      mach_port_deallocate (mach_task_self (), mapobj);
      munmap ((caddr_t) mapaddr, round_page (old_size));

      /* Now it's OK to free the old block */
      ffs_blkfree (np, old_pbn, old_size);

      /* Tell caller that we've moved data */
      return 1;
    }
  else
    return 0;
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
  mach_port_t pagerpt;
  int need_sync = 0;

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

  diskfs_check_readonly ();
  assert (!diskfs_readonly);

  /* This reference will ensure that NP->dn->fileinfo stays allocated. */
  pagerpt = diskfs_get_filemap (np, VM_PROT_WRITE|VM_PROT_READ);

  if (pagerpt == MACH_PORT_NULL)
    return errno;

  /* The new last block of the file. */
  lbn = lblkno (sblock, end - 1);

  /* This is the size of that block if it is in the NDADDR array. */
  size = fragroundup (sblock, blkoff (sblock, end));
  if (size == 0)
    size = sblock->fs_bsize;

  pthread_rwlock_wrlock (&np->dn->allocptrlock);

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

	  old_pbn = read_disk_entry (di->di_db[olbn]);

	  need_sync = block_extended (np, olbn, old_pbn, bno,
				      osize, sblock->fs_bsize);

	  write_disk_entry (di->di_db[olbn], bno);
	  record_poke (di, sizeof (struct dinode));
	  np->dn_set_ctime = 1;
	}
    }

  if (lbn < NDADDR)
    {
      daddr_t bno, old_pbn = read_disk_entry (di->di_db[lbn]);

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

	  need_sync = block_extended (np, lbn, old_pbn, bno, osize, size);

	  write_disk_entry (di->di_db[lbn], bno);
	  record_poke (di, sizeof (struct dinode));
	  np->dn_set_ctime = 1;
	}
      else
	{
	  /* Allocate a new last block. */
	  err = ffs_alloc (np, lbn,
			   ffs_blkpref (np, lbn, lbn, di->di_db),
			   size, &bno, cred);
	  if (err)
	    goto out;

	  
	  offer_data (np, lbn * sblock->fs_bsize, size,
		      (vm_address_t)zeroblock);
	  write_disk_entry (di->di_db[lbn], bno);
	  record_poke (di, sizeof (struct dinode));
	  np->dn_set_ctime = 1;
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
	      indirs[1].bno = bno;
	      write_disk_entry (di->di_ib[INDIR_SINGLE], bno);
	      record_poke (di, sizeof (struct dinode));
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
		  indirs[2].bno = bno;
		  write_disk_entry (di->di_ib[INDIR_DOUBLE], bno);
		  record_poke (di, sizeof (struct dinode));
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
	      indirs[1].bno = bno;
	      write_disk_entry (diblock[indirs[1].offset], bno);
	      record_poke (diblock, sblock->fs_bsize);
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
      offer_data (np, lbn * sblock->fs_bsize, sblock->fs_bsize,
		  (vm_address_t)zeroblock);
      indirs[0].bno = bno;
      write_disk_entry (siblock[indirs[0].offset], bno);
      record_poke (siblock, sblock->fs_bsize);
    }

 out:
  mach_port_deallocate (mach_task_self (), pagerpt);
  if (!err)
    {
      int newallocsize;
      if (lbn < NDADDR)
	newallocsize = lbn * sblock->fs_bsize + size;
      else
	newallocsize = (lbn + 1) * sblock->fs_bsize;
      assert (newallocsize > np->allocsize);
      np->allocsize = newallocsize;
    }

  pthread_rwlock_unlock (&np->dn->allocptrlock);
  pthread_mutex_lock (&np->dn->waitlock);
  pthread_cond_broadcast (&np->dn->waitcond);
  pthread_mutex_unlock (&np->dn->waitlock);

  if (need_sync)
    diskfs_file_update (np, 1);

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
	  munmap ((caddr_t) addr, len);
	}
      start += len;
    }
}

