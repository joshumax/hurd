/* Directory management routines

   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2002, 2007
     Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "ufs.h"
#include "dir.h"

#include <string.h>
#include <stdio.h>
#include <dirent.h>

#undef d_ino

enum slot_status
{
  /* This means we haven't yet found room for a new entry.  */
  LOOKING,

  /* This means that the specified entry is free and should be used. */
  TAKE,

  /* This means that the specified entry has enough room at the end
     to hold the new entry. */
  SHRINK,

  /* This means that there is enough space in the block, but not in
     any one single entry, so they all have to be shifted to make
     room.  */
  COMPRESS,

  /* This means that the directory will have to be grown to hold the
     entry. */
  EXTEND,

  /* For removal and rename, this means that this is the location
     of the entry found.  */
  HERE_TIS,
};

struct dirstat
{
  /* Type of followp operation expected */
  enum lookup_type type;

  /* One of the statuses above */
  enum slot_status stat;

  /* Mapped address and length of directory */
  vm_address_t mapbuf;
  vm_size_t mapextent;

  /* Index of this directory block. */
  int idx;

  /* For stat COMPRESS, this is the address (inside mapbuf)
     of the first direct in the directory block to be compressed. */
  /* For stat HERE_TIS, SHRINK, and TAKE, this is the entry referenced. */
  struct directory_entry *entry;

  /* For stat HERE_TIS, type REMOVE, this is the address of the immediately
     previous direct in this directory block, or zero if this is the first. */
  struct directory_entry *preventry;

  /* For stat COMPRESS, this is the number of bytes needed to be copied
     in order to undertake the compression. */
  size_t nbytes;
};

const size_t diskfs_dirstat_size = sizeof (struct dirstat);

/* Initialize DS such that diskfs_drop_dirstat will ignore it. */
void
diskfs_null_dirstat (struct dirstat *ds)
{
  ds->type = LOOKUP;
}

static error_t
dirscanblock (vm_address_t blockoff, struct node *dp, int idx,
	      const char *name, int namelen, enum lookup_type type,
	      struct dirstat *ds, ino_t *inum);

/* Implement the diskfs_lookup from the diskfs library.  See
   <hurd/diskfs.h> for the interface specification.  */
error_t
diskfs_lookup_hard (struct node *dp, const char *name, enum lookup_type type,
		    struct node **npp, struct dirstat *ds, struct protid *cred)
{
  error_t err;
  ino_t inum;
  int namelen;
  int spec_dotdot;
  struct node *np = 0;
  int retry_dotdot = 0;
  memory_object_t memobj;
  vm_prot_t prot =
    (type == LOOKUP) ? VM_PROT_READ : (VM_PROT_READ | VM_PROT_WRITE);
  vm_address_t buf = 0;
  vm_size_t buflen = 0;
  int blockaddr;
  int idx, lastidx;
  int looped;

  if ((type == REMOVE) || (type == RENAME))
    assert (npp);

  if (npp)
    *npp = 0;

  spec_dotdot = type & SPEC_DOTDOT;
  type &= ~SPEC_DOTDOT;

  namelen = strlen (name);

  if (namelen > MAXNAMLEN)
    {
      if (ds)
	diskfs_null_dirstat (ds);
      return ENAMETOOLONG;
    }

 try_again:
  if (ds)
    {
      ds->type = LOOKUP;
      ds->mapbuf = 0;
      ds->mapextent = 0;
    }
  if (buf)
    {
      munmap ((caddr_t) buf, buflen);
      buf = 0;
    }
  if (ds && (type == CREATE || type == RENAME))
    ds->stat = LOOKING;

  /* Map in the directory contents. */
  memobj = diskfs_get_filemap (dp, prot);

  if (memobj == MACH_PORT_NULL)
    return errno;

  buf = 0;
  /* We allow extra space in case we have to do an EXTEND. */
  buflen = round_page (dp->dn_stat.st_size + DIRBLKSIZ);
  err = vm_map (mach_task_self (),
		&buf, buflen, 0, 1, memobj, 0, 0, prot, prot, 0);
  mach_port_deallocate (mach_task_self (), memobj);

  inum = 0;

  diskfs_set_node_atime (dp);

  /* Start the lookup at DP->dn->dir_idx.  */
  idx = dp->dn->dir_idx;
  if (idx * DIRBLKSIZ > dp->dn_stat.st_size)
    idx = 0;			/* just in case */
  blockaddr = buf + idx * DIRBLKSIZ;
  looped = (idx == 0);
  lastidx = idx;
  if (lastidx == 0)
    lastidx = dp->dn_stat.st_size / DIRBLKSIZ;

  while (!looped || idx < lastidx)
    {
      err = dirscanblock (blockaddr, dp, idx, name, namelen, type, ds, &inum);
      if (!err)
	{
	  dp->dn->dir_idx = idx;
	  break;
	}
      if (err != ENOENT)
	{
	  munmap ((caddr_t) buf, buflen);
	  return err;
	}

      blockaddr += DIRBLKSIZ;
      idx++;
      if (blockaddr - buf >= dp->dn_stat.st_size && !looped)
	{
	  /* We've gotten to the end; start back at the beginning */
	  looped = 1;
	  blockaddr = buf;
	  idx = 0;
	}
    }

  diskfs_set_node_atime (dp);
  if (diskfs_synchronous)
    diskfs_node_update (dp, 1);

  /* If err is set here, it's ENOENT, and we don't want to
     think about that as an error yet. */
  err = 0;

  if (inum && npp)
    {
      if (namelen != 2 || name[0] != '.' || name[1] != '.')
	{
	  if (inum == dp->dn->number)
	    {
	      np = dp;
	      diskfs_nref (np);
	    }
	  else
	    {
	      err = diskfs_cached_lookup (inum, &np);
	      if (err)
		goto out;
	    }
	}

      /* We are looking up .. */
      /* Check to see if this is the root of the filesystem. */
      else if (dp->dn->number == 2)
	{
	  err = EAGAIN;
	  goto out;
	}

      /* We can't just do diskfs_cached_lookup, because we would then deadlock.
	 So we do this.  Ick.  */
      else if (retry_dotdot)
	{
	  /* Check to see that we got the same answer as last time. */
	  if (inum != retry_dotdot)
	    {
	      /* Drop what we *thought* was .. (but isn't any more) and
		 try *again*. */
	      diskfs_nput (np);
	      mutex_unlock (&dp->lock);
	      err = diskfs_cached_lookup (inum, &np);
	      mutex_lock (&dp->lock);
	      if (err)
		goto out;
	      retry_dotdot = inum;
	      goto try_again;
	    }
	  /* Otherwise, we got it fine and np is already set properly. */
	}
      else if (!spec_dotdot)
	{
	  /* Lock them in the proper order, and then
	     repeat the directory scan to see if this is still
	     right.  */
	  mutex_unlock (&dp->lock);
	  err = diskfs_cached_lookup (inum, &np);
	  mutex_lock (&dp->lock);
	  if (err)
	    goto out;
	  retry_dotdot = inum;
	  goto try_again;
	}

      /* Here below are the spec dotdot cases. */
      else if (type == RENAME || type == REMOVE)
	np = ifind (inum);

      else if (type == LOOKUP)
	{
	  diskfs_nput (dp);
	  err = diskfs_cached_lookup (inum, &np);
	  if (err)
	    goto out;
	}
      else
	assert (0);
    }

  if ((type == CREATE || type == RENAME) && !inum && ds && ds->stat == LOOKING)
    {
      /* We didn't find any room, so mark ds to extend the dir */
      ds->type = CREATE;
      ds->stat = EXTEND;
      ds->idx = dp->dn_stat.st_size / DIRBLKSIZ;
    }

  /* Return to the user; if we can't, release the reference
     (and lock) we acquired above.  */
 out:
  /* Deallocate or save the mapping. */
  if ((err && err != ENOENT)
      || !ds
      || ds->type == LOOKUP)
    {
      munmap ((caddr_t) buf, buflen);
      if (ds)
	ds->type = LOOKUP;	/* set to be ignored by drop_dirstat */
    }
  else
    {
      ds->mapbuf = buf;
      ds->mapextent = buflen;
    }

  if (np)
    {
      assert (npp);
      if (err)
	{
	  if (!spec_dotdot)
	    {
	      /* Normal case */
	      if (np == dp)
		diskfs_nrele (np);
	      else
		diskfs_nput (np);
	    }
	  else if (type == RENAME || type == REMOVE)
	    /* We just did ifind to get np; that allocates
	       no new references, so we don't have anything to do */
	    ;
	  else if (type == LOOKUP)
	    /* We did diskfs_cached_lookup */
	    diskfs_nput (np);
	}
      else
	*npp = np;
    }

  return err ? : inum ? 0 : ENOENT;
}

/* Scan block at address BLKADDR (of node DP; block index IDX), for
   name NAME of length NAMELEN.  Args TYPE, DS are as for
   diskfs_lookup.  If found, set *INUM to the inode number, else
   return ENOENT.  */
static error_t
dirscanblock (vm_address_t blockaddr, struct node *dp, int idx,
	      const char *name, int namelen, enum lookup_type type,
	      struct dirstat *ds, ino_t *inum)
{
  int nfree = 0;
  int needed = 0;
  vm_address_t currentoff, prevoff;
  struct directory_entry *entry = 0;
  int nentries = 0;
  size_t nbytes = 0;
  int looking = 0;
  int countcopies = 0;
  int consider_compress = 0;

  if (ds && (ds->stat == LOOKING
	     || ds->stat == COMPRESS))
    {
      looking = 1;
      countcopies = 1;
      needed = DIRSIZ (namelen);
    }

  for (currentoff = blockaddr, prevoff = 0;
       currentoff < blockaddr + DIRBLKSIZ;
       prevoff = currentoff, currentoff += read_disk_entry (entry->d_reclen))
    {
      entry = (struct directory_entry *)currentoff;

      if (!entry->d_reclen
	  || read_disk_entry (entry->d_reclen) % 4
	  || DIRECT_NAMLEN (entry) > MAXNAMLEN
	  || (currentoff + read_disk_entry (entry->d_reclen)
	      > blockaddr + DIRBLKSIZ)
	  || entry->d_name[DIRECT_NAMLEN (entry)]
	  || DIRSIZ (DIRECT_NAMLEN (entry)) > read_disk_entry (entry->d_reclen)
	  || memchr (entry->d_name, '\0', DIRECT_NAMLEN (entry)))
	{
	  fprintf (stderr, "Bad directory entry: inode: %Ld offset: %zd\n",
		  dp->dn->number, currentoff - blockaddr + idx * DIRBLKSIZ);
	  return ENOENT;
	}

      if (looking || countcopies)
	{
	  int thisfree;

	  /* Count how much free space this entry has in it. */
	  if (entry->d_ino == 0)
	    thisfree = read_disk_entry (entry->d_reclen);
	  else
	    thisfree = (read_disk_entry (entry->d_reclen)
			- DIRSIZ (DIRECT_NAMLEN (entry)));

	  /* If this isn't at the front of the block, then it will
	     have to be copied if we do a compression; count the
	     number of bytes there too. */
	  if (countcopies && currentoff != blockaddr)
	    nbytes += DIRSIZ (DIRECT_NAMLEN (entry));

	  if (ds->stat == COMPRESS && nbytes > ds->nbytes)
	    /* The previously found compress is better than
	       this one, so don't bother counting any more. */
	    countcopies = 0;

	  if (thisfree >= needed)
	    {
	      ds->type = CREATE;
	      ds->stat = read_disk_entry (entry->d_ino) == 0 ? TAKE : SHRINK;
	      ds->entry = entry;
	      ds->idx = idx;
	      looking = countcopies = 0;
	    }
	  else
	    {
	      nfree += thisfree;
	      if (nfree >= needed)
		consider_compress = 1;
	    }
	}

      if (entry->d_ino)
	nentries++;

      if (DIRECT_NAMLEN (entry) == namelen
	  && entry->d_name[0] == name[0]
	  && entry->d_ino
	  && !bcmp (entry->d_name, name, namelen))
	break;
    }

  if (consider_compress
      && (ds->type == LOOKING
	  || (ds->type == COMPRESS && ds->nbytes > nbytes)))
    {
      ds->type = CREATE;
      ds->stat = COMPRESS;
      ds->entry = (struct directory_entry *) blockaddr;
      ds->idx = idx;
      ds->nbytes = nbytes;
    }

  if (currentoff >= blockaddr + DIRBLKSIZ)
    {
      int i;
      /* The name is not in this block. */

      /* Because we scanned the entire block, we should write
	 down how many entries there were. */
      if (!dp->dn->dirents)
	{
	  dp->dn->dirents = malloc ((dp->dn_stat.st_size / DIRBLKSIZ)
				    * sizeof (int));
	  for (i = 0; i < dp->dn_stat.st_size/DIRBLKSIZ; i++)
	    dp->dn->dirents[i] = -1;
	}
      /* Make sure the count is correct if there is one now. */
      assert (dp->dn->dirents[idx] == -1
	      || dp->dn->dirents[idx] == nentries);
      dp->dn->dirents[idx] = nentries;

      return ENOENT;
    }

  /* We have found the required name. */

  if (ds && type == CREATE)
    ds->type = LOOKUP;		/* it's invalid now */
  else if (ds && (type == REMOVE || type == RENAME))
    {
      ds->type = type;
      ds->stat = HERE_TIS;
      ds->entry = entry;
      ds->idx = idx;
      ds->preventry = (struct directory_entry *) prevoff;
    }

  *inum = read_disk_entry (entry->d_ino);
  return 0;
}

/* Following a lookup call for CREATE, this adds a node to a directory.
   DP is the directory to be modified; NAME is the name to be entered;
   NP is the node being linked in; DS is the cached information returned
   by lookup; CRED describes the user making the call.  This call may
   only be made if the directory has been held locked continuously since
   the preceding lookup call, and only if that call returned ENOENT. */
error_t
diskfs_direnter_hard(struct node *dp,
		     const char *name,
		     struct node *np,
		     struct dirstat *ds,
		     struct protid *cred)
{
  struct directory_entry *new;
  int namelen = strlen (name);
  int needed = DIRSIZ (namelen);
  int oldneeded;
  vm_address_t fromoff, tooff;
  int totfreed;
  error_t err;
  size_t oldsize = 0;

  assert (ds->type == CREATE);

  dp->dn_set_mtime = 1;

  switch (ds->stat)
    {
    case TAKE:
      /* We are supposed to consume this slot. */
      assert (ds->entry->d_ino == 0
	      && read_disk_entry (ds->entry->d_reclen) >= needed);

      write_disk_entry (ds->entry->d_ino, np->dn->number);
      DIRECT_NAMLEN (ds->entry) = namelen;
      if (direct_symlink_extension)
	ds->entry->d_type = IFTODT (np->dn_stat.st_mode);
      bcopy (name, ds->entry->d_name, namelen + 1);

      break;

    case SHRINK:
      /* We are supposed to take the extra space at the end
	 of this slot. */
      oldneeded = DIRSIZ (DIRECT_NAMLEN (ds->entry));
      assert (read_disk_entry (ds->entry->d_reclen) - oldneeded >= needed);

      new = (struct directory_entry *) ((vm_address_t) ds->entry + oldneeded);

      write_disk_entry (new->d_ino, np->dn->number);
      write_disk_entry (new->d_reclen,
			read_disk_entry (ds->entry->d_reclen) - oldneeded);
      DIRECT_NAMLEN (new) = namelen;
      if (direct_symlink_extension)
	new->d_type = IFTODT (np->dn_stat.st_mode);
      bcopy (name, new->d_name, namelen + 1);

      write_disk_entry (ds->entry->d_reclen, oldneeded);

      break;

    case COMPRESS:
      /* We are supposed to move all the entries to the
	 front of the block, giving each the minimum
	 necessary room.  This should free up enough space
	 for the new entry. */
      fromoff = tooff = (vm_address_t) ds->entry;

      while (fromoff < (vm_address_t) ds->entry + DIRBLKSIZ)
	{
	  struct directory_entry *from = (struct directory_entry *)fromoff;
	  struct directory_entry *to = (struct directory_entry *) tooff;
	  int fromreclen = read_disk_entry (from->d_reclen);

	  if (from->d_ino != 0)
	    {
	      assert (fromoff >= tooff);

	      bcopy (from, to, fromreclen);
	      write_disk_entry (to->d_reclen, DIRSIZ (DIRECT_NAMLEN (to)));

	      tooff += read_disk_entry (to->d_reclen);
	    }
	  fromoff += fromreclen;
	}

      totfreed = (vm_address_t) ds->entry + DIRBLKSIZ - tooff;
      assert (totfreed >= needed);

      new = (struct directory_entry *) tooff;
      write_disk_entry (new->d_ino, np->dn->number);
      write_disk_entry (new->d_reclen, totfreed);
      DIRECT_NAMLEN (new) = namelen;
      if (direct_symlink_extension)
	new->d_type = IFTODT (np->dn_stat.st_mode);
      bcopy (name, new->d_name, namelen + 1);
      break;

    case EXTEND:
      /* Extend the file. */
      assert (needed <= DIRBLKSIZ);

      oldsize = dp->dn_stat.st_size;
      if ((off_t)(oldsize + DIRBLKSIZ) != dp->dn_stat.st_size + DIRBLKSIZ)
	{
	  /* We can't possibly map the whole directory in.  */
	  munmap ((caddr_t) ds->mapbuf, ds->mapextent);
	  return EOVERFLOW;
	}
      while (oldsize + DIRBLKSIZ > dp->allocsize)
	{
	  err = diskfs_grow (dp, oldsize + DIRBLKSIZ, cred);
	  if (err)
	    {
	      munmap ((caddr_t) ds->mapbuf, ds->mapextent);
	      return err;
	    }
	}

      new = (struct directory_entry *) (ds->mapbuf + oldsize);

      dp->dn_stat.st_size = oldsize + DIRBLKSIZ;
      dp->dn_set_ctime = 1;

      write_disk_entry (new->d_ino, np->dn->number);
      write_disk_entry (new->d_reclen, DIRBLKSIZ);
      DIRECT_NAMLEN (new) = namelen;
      if (direct_symlink_extension)
	new->d_type = IFTODT (np->dn_stat.st_mode);
      bcopy (name, new->d_name, namelen + 1);
      break;

    default:
      assert (0);
    }

  dp->dn_set_mtime = 1;

  munmap ((caddr_t) ds->mapbuf, ds->mapextent);

  if (ds->stat != EXTEND)
    {
      /* If we are keeping count of this block, then keep the count up
	 to date. */
      if (dp->dn->dirents && dp->dn->dirents[ds->idx] != -1)
	dp->dn->dirents[ds->idx]++;
    }
  else
    {
      int i;
      /* It's cheap, so start a count here even if we aren't counting
	 anything at all. */
      if (dp->dn->dirents)
	{
	  dp->dn->dirents = realloc (dp->dn->dirents,
				     (dp->dn_stat.st_size / DIRBLKSIZ
				      * sizeof (int)));
	  for (i = oldsize / DIRBLKSIZ;
	       i < dp->dn_stat.st_size / DIRBLKSIZ;
	       i++)
	    dp->dn->dirents[i] = -1;

	  dp->dn->dirents[ds->idx] = 1;
	}
      else
	{
	  dp->dn->dirents = malloc (dp->dn_stat.st_size / DIRBLKSIZ
				    * sizeof (int));
	  for (i = 0; i < dp->dn_stat.st_size / DIRBLKSIZ; i++)
	    dp->dn->dirents[i] = -1;
	  dp->dn->dirents[ds->idx] = 1;
	}
    }

  diskfs_file_update (dp, 1);

  return 0;
}

/* Following a lookup call for REMOVE, this removes the link from the
   directory.  DP is the directory being changed and DS is the cached
   information returned from lookup.  This call is only valid if the
   directory has been locked continuously since the call to lookup, and
   only if that call succeeded.  */
error_t
diskfs_dirremove_hard(struct node *dp,
		      struct dirstat *ds)
{
  assert (ds->type == REMOVE);
  assert (ds->stat == HERE_TIS);

  dp->dn_set_mtime = 1;

  if (ds->preventry == 0)
    ds->entry->d_ino = 0;
  else
    {
      assert ((vm_address_t) ds->entry - (vm_address_t) ds->preventry
	      == read_disk_entry (ds->preventry->d_reclen));
      write_disk_entry (ds->preventry->d_reclen,
			(read_disk_entry (ds->preventry->d_reclen)
			 + read_disk_entry (ds->entry->d_reclen)));
    }

  dp->dn_set_mtime = 1;

  munmap ((caddr_t) ds->mapbuf, ds->mapextent);

  /* If we are keeping count of this block, then keep the count up
     to date. */
  if (dp->dn->dirents && dp->dn->dirents[ds->idx] != -1)
    dp->dn->dirents[ds->idx]--;

  diskfs_file_update (dp, 1);

  return 0;
}


/* Following a lookup call for RENAME, this changes the inode number
   on a directory entry.  DP is the directory being changed; NP is
   the new node being linked in; DP is the cached information returned
   by lookup.  This call is only valid if the directory has been locked
   continuously since the call to lookup, and only if that call
   succeeded.  */
error_t
diskfs_dirrewrite_hard(struct node *dp,
		       struct node *np,
		       struct dirstat *ds)
{
  assert (ds->type == RENAME);
  assert (ds->stat == HERE_TIS);

  dp->dn_set_mtime = 1;
  write_disk_entry (ds->entry->d_ino, np->dn->number);
  if (direct_symlink_extension)
    ds->entry->d_type = IFTODT (np->dn_stat.st_mode);
  dp->dn_set_mtime = 1;

  munmap ((caddr_t) ds->mapbuf, ds->mapextent);

  diskfs_file_update (dp, 1);

  return 0;
}

/* Tell if DP is an empty directory (has only "." and ".." entries). */
/* This routine must be called from inside a catch_exception ().  */
int
diskfs_dirempty(struct node *dp,
		struct protid *cred)
{
  struct directory_entry *entry;
  vm_address_t buf, curoff;
  memory_object_t memobj;
  error_t err;

  memobj = diskfs_get_filemap (dp, VM_PROT_READ);

  if (memobj == MACH_PORT_NULL)
    /* XXX should reflect error properly */
    return 0;

  buf = 0;

  err = vm_map (mach_task_self (), &buf, dp->dn_stat.st_size, 0,
		1, memobj, 0, 0, VM_PROT_READ, VM_PROT_READ, 0);
  mach_port_deallocate (mach_task_self (), memobj);
  assert (!err);

  diskfs_set_node_atime (dp);

  for (curoff = buf;
       curoff < buf + dp->dn_stat.st_size;
       curoff += read_disk_entry (entry->d_reclen))
    {
      entry = (struct directory_entry *) curoff;

      if (entry->d_ino != 0
	  && (DIRECT_NAMLEN (entry) > 2
	      || entry->d_name[0] != '.'
	      || (entry->d_name[1] != '.'
		  && entry->d_name[1] != '\0')))
	{
	  munmap ((caddr_t) buf, dp->dn_stat.st_size);
	  diskfs_set_node_atime (dp);
	  if (diskfs_synchronous)
	    diskfs_node_update (dp, 1);
	  return 0;
	}
    }
  diskfs_set_node_atime (dp);
  if (diskfs_synchronous)
    diskfs_node_update (dp, 1);
  munmap ((caddr_t) buf, dp->dn_stat.st_size);
  return 1;
}

/* Make DS an invalid dirstat. */
error_t
diskfs_drop_dirstat (struct node *dp, struct dirstat *ds)
{
  if (ds->type != LOOKUP)
    {
      assert (ds->mapbuf);
      munmap ((caddr_t) ds->mapbuf, ds->mapextent);
      ds->type = LOOKUP;
    }
  return 0;
}


/* Count the entries in directory block NB for directory DP and
   write the answer down in its dirents array.  As a side affect
   fill BUF with the block.  */
static error_t
count_dirents (struct node *dp, int nb, char *buf)
{
  size_t amt;
  char *offinblk;
  struct directory_entry *entry;
  int count = 0;
  error_t err;

  assert (dp->dn->dirents);
  assert ((nb + 1) * DIRBLKSIZ <= dp->dn_stat.st_size);

  err = diskfs_node_rdwr (dp, buf, nb * DIRBLKSIZ, DIRBLKSIZ, 0, 0, &amt);
  if (err)
    return err;
  assert (amt == DIRBLKSIZ);

  for (offinblk = buf;
       offinblk < buf + DIRBLKSIZ;
       offinblk += read_disk_entry (entry->d_reclen))
    {
      entry = (struct directory_entry *) offinblk;
      if (entry->d_ino)
	count++;
    }

  assert (dp->dn->dirents[nb] == -1 || dp->dn->dirents[nb] == count);
  dp->dn->dirents[nb] = count;
  return 0;
}

/* Implement the disikfs_get_directs callback as described in
   <hurd/diskfs.h>. */
error_t
diskfs_get_directs (struct node *dp,
		    int entry,
		    int nentries,
		    char **data,
		    size_t *datacnt,
		    vm_size_t bufsiz,
		    int *amt)
{
  int blkno;
  int nblks;
  int curentry;
  char buf[DIRBLKSIZ];
  char *bufp;
  int bufvalid;
  error_t err;
  int i;
  char *datap;
  struct directory_entry *entryp;
  int allocsize;
  size_t checklen;
  struct dirent *userp;

  nblks = dp->dn_stat.st_size/DIRBLKSIZ;

  if (!dp->dn->dirents)
    {
      dp->dn->dirents = malloc (nblks * sizeof (int));
      for (i = 0; i < nblks; i++)
	dp->dn->dirents[i] = -1;
    }

  /* Scan through the entries to find ENTRY.  If we encounter
     a -1 in the process then stop to fill it.  When we run
     off the end, ENTRY is too big. */
  curentry = 0;
  bufvalid = 0;
  for (blkno = 0; blkno < nblks; blkno++)
    {
      if (dp->dn->dirents[blkno] == -1)
	{
	  err = count_dirents (dp, blkno, buf);
	  if (err)
	    return err;
	  bufvalid = 1;
	}

      if (curentry + dp->dn->dirents[blkno] > entry)
	/* ENTRY starts in this block. */
	break;

      curentry += dp->dn->dirents[blkno];

      bufvalid = 0;
    }

  if (blkno == nblks)
    {
      /* We reached the end of the directory without seeing ENTRY.
	 This is treated as an EOF condition, meaning we return
	 success with empty results.  */
      *datacnt = 0;
      *amt = 0;
      return 0;
    }

  /* Allocate enough space to hold the maximum we might return */
  if (!bufsiz || bufsiz > dp->dn_stat.st_size)
    allocsize = round_page (dp->dn_stat.st_size);
  else
    allocsize = round_page (bufsiz);

  if (allocsize > *datacnt)
    *data = mmap (0, allocsize, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);

  /* Set bufp appropriately */
  bufp = buf;
  if (curentry != entry)
    {
      /* Look through the block to find out where to start,
	 setting bufp appropriately.  */
      if (!bufvalid)
	{
	  err = diskfs_node_rdwr (dp, buf, blkno * DIRBLKSIZ, DIRBLKSIZ,
				  0, 0, &checklen);
	  if (err)
	    return err;
	  assert (checklen == DIRBLKSIZ);
	  bufvalid = 1;
	}
      for (i = 0, bufp = buf;
	   i < entry - curentry && bufp - buf < DIRBLKSIZ;
	   (bufp
	    += read_disk_entry (((struct directory_entry *)bufp)->d_reclen)),
	   i++)
	;
      /* Make sure we didn't run off the end. */
      assert (bufp - buf < DIRBLKSIZ);
    }

  i = 0;
  datap = *data;

  /* Copy the entries, one at a time. */
  while (((nentries == -1) || (i < nentries))
	 && (!bufsiz || (datap - *data < bufsiz) )
	 && blkno < nblks)
    {
      if (!bufvalid)
	{
	  err = diskfs_node_rdwr (dp, buf, blkno * DIRBLKSIZ, DIRBLKSIZ,
				  0, 0, &checklen);
	  if (err)
	    return err;
	  assert (checklen == DIRBLKSIZ);
	  bufvalid = 1;
	  bufp = buf;
	}

      entryp = (struct directory_entry *)bufp;

      if (entryp->d_ino)
	{
	  userp = (struct dirent *) datap;

	  userp->d_fileno = read_disk_entry (entryp->d_ino);
	  userp->d_reclen = DIRSIZ (DIRECT_NAMLEN (entryp));
	  userp->d_namlen = DIRECT_NAMLEN (entryp);
	  bcopy (entryp->d_name, userp->d_name, DIRECT_NAMLEN (entryp) + 1);
	  userp->d_type = DT_UNKNOWN; /* until fixed */
	  i++;
	  datap += DIRSIZ (DIRECT_NAMLEN (entryp));
	}

      bufp += read_disk_entry (entryp->d_reclen);
      if (bufp - buf == DIRBLKSIZ)
	{
	  blkno++;
	  bufvalid = 0;
	}
    }

  /* We've copied all we can.  If we allocated our own array
     but didn't fill all of it, then free whatever memory we didn't use. */
  if (allocsize > *datacnt)
    {
      if (round_page (datap - *data) < allocsize)
	munmap (*data + round_page (datap - *data),
		allocsize - round_page (datap - *data));
    }

  /* Set variables for return */
  *datacnt = datap - *data;
  *amt = i;
  return 0;
}
