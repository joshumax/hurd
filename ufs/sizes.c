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
#include "fs.h"
#include "dinode.h"

#ifdef DONT_CACHE_MEMORY_OBJECTS
#define MAY_CACHE 0
#else
#define MAY_CACHE 1
#endif

static void dindir_drop (struct node *);
static void sindir_drop (struct node *, int, int);
static void poke_pages (memory_object_t, vm_offset_t, vm_offset_t);

/* Truncate node NP to be at most LENGTH bytes. */
/* The inode must be locked, and we must have the conch. */
/* This is a pain.  Sigh.  */
error_t
diskfs_truncate (struct node *np,
		 off_t length)
{
  daddr_t lastblock, olastblock, bn;
  off_t osize;
  int bsize, idx;
  mach_port_t obj;

  osize = np->dn_stat.st_size;
  if (length >= osize)
    return 0;

  /* Calculate block number of last block */
  lastblock = lblkno (length + sblock->fs_bsize - 1) - 1;
  olastblock = lblkno (osize + sblock->fs_bsize - 1) - 1;

  /* If the prune is not to a block boundary, zero the bit upto the
     next block boundary. */
  if (blkoff (length))
    diskfs_node_rdwr (np, (void *) zeroblock, length, 
		      blksize (np, lastblock) - blkoff (length), 1, 0, 0);

  /* We are going to throw away the block pointers for the blocks
     olastblock+1 through lastblock.  This will cause the underlying
     data to become zeroes, because of the behavior of pager_read_page
     (in ufs/pager.c).  Consequently, we have to take action to force
     the kernel to immediately undertake any delayed copies that
     implicitly depend on the data we are flushing.  We also have to
     prevent any new delayed copies from being undertaken until we
     have finished the flush. */
  if (np->dn->fileinfo)
    {
      pager_change_attributes (np->dn->fileinfo->p, MAY_CACHE, 
			       MEMORY_OBJECT_COPY_NONE, 1);
      obj = diskfs_get_filemap (np);
      mach_port_insert_right (mach_task_self (), obj, obj, 
			      MACH_MSG_TYPE_MAKE_SEND);
      poke_pages (obj, round_page (length), round_page (osize));
      mach_port_deallocate (mach_task_self (), obj);
    }
  
  rwlock_writer_lock (&np->dn->datalock, np->dn);

  /* Update the size now.  If we crash, fsck can finish freeing the
     blocks. */
  np->dn_stat.st_size = length;
  np->dn_stat_dirty = 1;

  /* Flush the old data. */
  if (np->dn->fileinfo)
    pager_flush_some (np->dn->fileinfo->p, 
		      (lastblock == -1 ? 0 : lastblock) * sblock->fs_bsize, 
		      (olastblock - lastblock) * sblock->fs_bsize, 1);

  /* Drop data blocks mapped by indirect blocks */
  if (olastblock >= NDADDR)
    {
      daddr_t first2free;

      mutex_lock (&sinmaplock);
      if (!np->dn->sinloc)
	sin_map (np);

      if (lastblock + 1 > NDADDR)
	first2free = lastblock + 1;
      else
	first2free = NDADDR;
      
      for (idx = first2free; idx <= olastblock; idx ++)
	{
	  if (np->dn->sinloc[idx - NDADDR])
	    {
	      blkfree (np->dn->sinloc[idx - NDADDR], sblock->fs_bsize);
	      np->dn->sinloc[idx - NDADDR] = 0;
	      np->dn_stat.st_blocks -= sblock->fs_bsize / DEV_BSIZE;
	      np->dn_stat_dirty = 1;
	    }
	}

      /* Prune the block pointers handled by the sindir pager.  This will
	 free all the indirect blocks and such as necessary.  */
      sindir_drop (np, lblkno((first2free - NDADDR) * sizeof (daddr_t)),
		   lblkno ((olastblock - NDADDR) * sizeof (daddr_t)));

      if (!np->dn->fileinfo)
	sin_unmap (np);
      mutex_unlock (&sinmaplock);
    }

  /* Prune the blocks mapped directly from the inode */
  for (idx = lastblock + 1; idx < NDADDR; idx++)
    {
      bn = dinodes[np->dn->number].di_db[idx];
      if (bn)
	{
	  dinodes[np->dn->number].di_db[idx] = 0;
	  assert (idx <= olastblock);
	  if (idx == olastblock)
	    bsize = blksize (np, idx);
	  else
	    bsize = sblock->fs_bsize;
	  blkfree (bn, bsize);
	  np->dn_stat.st_blocks -= bsize / DEV_BSIZE;
	  np->dn_stat_dirty = 1;
	}
    }
  
  if (lastblock >= 0 && lastblock < NDADDR)
    {
      /* Look for a change in the size of the last direct block */
      bn = dinodes[np->dn->number].di_db[lastblock];
      if (bn)
	{
	  off_t oldspace, newspace;
	  
	  oldspace = blksize (np, lastblock);
	  newspace = fragroundup (blkoff (length));;
	  assert (newspace);
	  if (oldspace - newspace)
	    {
	      bn += numfrags (newspace);
	      blkfree (bn, oldspace - newspace);
	      np->dn_stat.st_blocks -= (oldspace - newspace) / DEV_BSIZE;
	      np->dn_stat_dirty = 1;
	    }
	}
    }

  if (lastblock < NDADDR)
    np->allocsize = fragroundup (length);
  else
    np->allocsize = blkroundup (length);

  rwlock_writer_unlock (&np->dn->datalock, np->dn);

  /* Now we can allow delayed copies again */
  if (np->dn->fileinfo)
    pager_change_attributes (np->dn->fileinfo->p, MAY_CACHE,
			     MEMORY_OBJECT_COPY_DELAY, 0);

  diskfs_file_update (np, 1);
  return 0;
}  

/* Deallocate the double indirect block of the file NP. */
static void
dindir_drop (struct node *np)
{
  rwlock_writer_lock (&np->dn->dinlock, np->dn);
  
  pager_flush_some (dinpager->p, np->dn->number * sblock->fs_bsize,
		    sblock->fs_bsize, 1);

  if (dinodes[np->dn->number].di_ib[INDIR_DOUBLE])
    {
      blkfree (dinodes[np->dn->number].di_ib[INDIR_DOUBLE], sblock->fs_bsize);
      dinodes[np->dn->number].di_ib[INDIR_DOUBLE] = 0;
      np->dn_stat.st_blocks -= sblock->fs_bsize / DEV_BSIZE;
    }

  rwlock_writer_unlock (&np->dn->dinlock, np->dn);
}
  

/* Deallocate the single indirect blocks of file IP from
   FIRST through LAST inclusive. */
static void
sindir_drop (struct node *np,
	     int first,
	     int last)
{
  int idx;
  
  rwlock_writer_lock (&np->dn->sinlock, np->dn);
  
  pager_flush_some (np->dn->sininfo->p, first * sblock->fs_bsize,
		    (last - first + 1) * sblock->fs_bsize, 1);
  
  /* Drop indirect blocks found in the double indirect block */
  if (last > 1)
    {
      mutex_lock (&dinmaplock);
      if (!np->dn->dinloc)
	din_map (np);
      for (idx = first; idx = last; idx++)
	{
	  if (np->dn->dinloc[idx - 1])
	    {
	      blkfree (np->dn->dinloc[idx - 1], sblock->fs_bsize);
	      np->dn->dinloc[idx - 1] = 0;
	      np->dn_stat.st_blocks -= sblock->fs_bsize / DEV_BSIZE;
	    }
	}
      
      /* If we no longer need the double indirect block, drop it. */
      if (first <= 1)
	dindir_drop (np);

      mutex_lock (&dinmaplock);
      if (!np->dn->sininfo)
	din_unmap (np);
      mutex_unlock (&dinmaplock);
    }
  
  /* Drop the block from the inode if we don't need it any more */
  if (first == 0 && dinodes[np->dn->number].di_ib[INDIR_SINGLE])
    {
      blkfree (dinodes[np->dn->number].di_ib[INDIR_SINGLE], sblock->fs_bsize);
      dinodes[np->dn->number].di_ib[INDIR_SINGLE] = 0;
      np->dn_stat.st_blocks -= sblock->fs_bsize / DEV_BSIZE;
    }
  rwlock_writer_unlock (&np->dn->sinlock, np->dn);
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


/* Implement the diskfs_grow callback; see <hurd/diskfs.h> for the 
   interface description. */
error_t
diskfs_grow (struct node *np,
	     off_t end,
	     struct protid *cred)
{
  daddr_t lbn, pbn, nb;
  int osize, size;
  int err;
  volatile daddr_t dealloc_on_error = 0;
  volatile int dealloc_size = 0;
  volatile off_t zero_off1 = 0, zero_off2 = 0;
  volatile int zero_len1 = 0, zero_len2 = 0;
  volatile off_t poke_off1 = 0, poke_off2 = 0;
  volatile off_t poke_len1 = 0, poke_len2 = 0;
  vm_address_t zerobuf;

  if (end <= np->allocsize)
    return 0;
  
  rwlock_writer_lock (&np->dn->datalock, np->dn);

  /* This deallocation works for the calls to alloc, but not for
     realloccg.  I'm not sure how to prune the fragment down, especially if
     we grew a fragment and then couldn't allocate the piece later.
     freeing it all up is a royal pain, largely punted right now... -mib.
     */
  if (err = diskfs_catch_exception())
    {
      if (dealloc_on_error)
	blkfree (dealloc_on_error, dealloc_size);
      goto out;
    }

  /* This is the logical block number of what will be the last block. */
  lbn = lblkno (end + sblock->fs_bsize - 1) - 1;

  /* This is the size to be of that block if it is in the NDADDR array.  */
  size = fragroundup (blkoff (end));
  if (size == 0)
    size = sblock->fs_bsize;
  
  /* if we are writing a new block, then an old one may need to be
     reallocated into a full block. */

  nb = lblkno (np->allocsize + sblock->fs_bsize - 1) - 1;
  if (np->allocsize && nb < NDADDR && nb < lbn)
    {
      osize = blksize (np, nb);
      if (osize < sblock->fs_bsize && osize > 0)
	{
	  daddr_t old_pbn;
	  err = realloccg (np, nb,
			   blkpref (np, nb, (int)nb, 
				    dinodes[np->dn->number].di_db),
			   osize, sblock->fs_bsize, &pbn, cred);
	  if (err)
	    goto out;
	  np->allocsize = (nb + 1) * sblock->fs_bsize;
	  old_pbn = dinodes[np->dn->number].di_db[nb];
	  dinodes[np->dn->number].di_db[nb] = pbn;

	  /* The new disk blocks should be zeros but might not be.
	     This is a sanity measure that I'm not sure is necessary. */
	  zero_off1 = nb * sblock->fs_bsize + osize;
	  zero_len1 = nb * sblock->fs_bsize + sblock->fs_bsize - zero_off1;

	  if (pbn != old_pbn)
	    {
	      /* Make sure that the old contents get written out by
		 poking the pages. */
	      poke_off1 = nb * sblock->fs_bsize;
	      poke_len1 = osize;
	    }
	}
    }
  
  /* allocate this block */
  if (lbn < NDADDR)
    {
      nb = dinodes[np->dn->number].di_db[lbn];

      if (nb != 0)
	{
	  /* consider need to reallocate a fragment. */
	  osize = blkoff (np->allocsize);
	  if (osize == 0)
	    osize = sblock->fs_bsize;
	  if (size > osize)
	    {
	      err = realloccg (np, lbn, 
			       blkpref (np, lbn, lbn, 
					dinodes[np->dn->number].di_db),
			       osize, size, &pbn, cred);
	      if (err)
		goto out;
	      dinodes[np->dn->number].di_db[lbn] = pbn;

	      /* The new disk blocks should be zeros but might not be.
		 This is a sanity measure that I'm not sure is necessary. */
	      zero_off2 = lbn * sblock->fs_bsize + osize;
	      zero_len2 = lbn * sblock->fs_bsize + size - zero_off2;

	      if (pbn != nb)
		{
		  /* Make sure that the old contents get written out by
		     poking the pages. */
		  poke_off2 = lbn * sblock->fs_bsize;
		  poke_len2 = osize;
		}
	    }
	}
      else
	{
	  err = alloc (np, lbn,
		       blkpref (np, lbn, lbn, dinodes[np->dn->number].di_db),
		       size, &pbn, cred);
	  if (err)
	    goto out;
	  dealloc_on_error = pbn;
	  dealloc_size = size;
	  dinodes[np->dn->number].di_db[lbn] = pbn;
	}
      np->allocsize = fragroundup (end);
    }
  else
    {
      /* Make user the sindir area is mapped at the right size. */
      mutex_lock (&sinmaplock);
      if (np->dn->sinloc)
	{
	  sin_remap (np, end);
	  np->allocsize = blkroundup (end);
	}
      else
	{
	  np->allocsize = blkroundup (end);
	  sin_map (np);
	}
      
      lbn -= NDADDR;
      if (!np->dn->sinloc[lbn])
	{
	  err = alloc (np, lbn, blkpref (np, lbn + NDADDR, lbn, 
					 np->dn->sinloc),
		       sblock->fs_bsize, &pbn, cred);
	  if (err)
	    goto out;
	  dealloc_on_error = pbn;
	  dealloc_size = sblock->fs_bsize;
	  np->dn->sinloc[lbn] = pbn;
	}
      if (!np->dn->fileinfo)
	sin_unmap (np);
      mutex_unlock (&sinmaplock);
    }

  if (np->conch.holder)
    ioserver_put_shared_data (np->conch.holder);
  
 out:
  diskfs_end_catch_exception ();
  rwlock_writer_unlock (&np->dn->datalock, np->dn);

  /* Do the pokes and zeros that we requested before; they have to be
     done here because we can't cause a page while holding datalock. */
  if (zero_len1 || zero_len2)
    {
      vm_allocate (mach_task_self (), &zerobuf, 
		   zero_len1 > zero_len2 ? zero_len1 : zero_len2, 1);
      if (zero_len1)
	diskfs_node_rdwr (np, (char *) zerobuf, zero_off1,
			  zero_len1, 1, cred, 0);
      if (zero_len2)
	diskfs_node_rdwr (np, (char *) zerobuf, zero_off2,
			  zero_len2, 1, cred, 0);
      vm_deallocate (mach_task_self (), zerobuf, 
		     zero_len1 > zero_len2 ? zero_len1 : zero_len2);
    }
  if (poke_len1 || poke_len2)
    {
      mach_port_t obj;
      obj = diskfs_get_filemap (np);
      mach_port_insert_right (mach_task_self (), obj, obj, 
			      MACH_MSG_TYPE_MAKE_SEND);
      if (poke_len1)
	poke_pages (obj, trunc_page (poke_off1), 
		    round_page (poke_off1 + poke_len1));
      if (poke_len2)
	poke_pages (obj, trunc_page (poke_off2), 
		    round_page (poke_off2 + poke_len2));
      mach_port_deallocate (mach_task_self (), obj);
    }
  
  diskfs_file_update (np, 0);

  return err;
}  
