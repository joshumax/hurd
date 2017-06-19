/* dir.c - FAT filesystem.

   Copyright (C) 1997, 1998, 1999, 2002, 2003, 2007
     Free Software Foundation, Inc.

   Written by Thomas Bushnell, n/BSG and Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <ctype.h>
#include <string.h>
#include <dirent.h>
#include <hurd/fsys.h>

#include "fatfs.h"

/* The size of a directory block is usually just the cluster size.
   However, the root directory of FAT12/16 file systems is stored in
   sectors in a special region, so we settle on the greatest common
   divisor here.  */
#define DIRBLKSIZ bytes_per_sector
#define LOG2_DIRBLKSIZ log2_bytes_per_sector

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
  /* Type of followp operation expected.  */
  enum lookup_type type;

  /* One of the statuses above.  */
  enum slot_status stat;

  /* Mapped address and length of directory.  */
  vm_address_t mapbuf;
  vm_size_t mapextent;

  /* Index of this directory block.  */
  int idx;

  /* For stat COMPRESS, this is the address (inside mapbuf)
     of the first direct in the directory block to be compressed.  */
  /* For stat HERE_TIS, SHRINK, and TAKE, this is the entry referenced.  */
  struct dirrect *entry;

  /* For stat HERE_TIS, type REMOVE, this is the address of the immediately
     previous direct in this directory block, or zero if this is the first.  */
  struct dirrect *preventry;

  /* For stat COMPRESS, this is the number of bytes needed to be copied
     in order to undertake the compression.  */
  size_t nbytes;
};

const size_t diskfs_dirstat_size = sizeof (struct dirstat);

/* Initialize DS such that diskfs_drop_dirstat will ignore it.  */
void
diskfs_null_dirstat (struct dirstat *ds)
{
  ds->type = LOOKUP;
}

/* Forward declaration.  */
static error_t
dirscanblock (vm_address_t blockoff, struct node *dp, int idx,
              const char *name, int namelen, enum lookup_type type,
              struct dirstat *ds, ino_t *inum);

static int
fatnamematch (const char *dirname, const char *username, size_t unamelen)
{
  char *dn = strdup(dirname);
  int dpos = 0;
  int upos = 0;
  int ext = 0;

  /* Deleted files. */
  if (dn[0] == FAT_DIR_NAME_DELETED || dn[0] == FAT_DIR_NAME_LAST)
    return 0;
  if (dn[0] == FAT_DIR_NAME_REPLACE_DELETED)
    dn[0] = FAT_DIR_NAME_DELETED;

  /* Special representations for `.' and `..'.  */
  if (!memcmp(dn, FAT_DIR_NAME_DOT, 11))
    return unamelen == 1 && username[0] == '.';

  if (!memcmp (dn, FAT_DIR_NAME_DOTDOT, 11))
    return unamelen == 2 && username[0] == '.' && username[1] == '.';

  if (unamelen > 12)
    return 0;

  do
    {
      /* First check if we have reached the extension without coming
	 across blanks. */
      if (dpos == 8 && !ext)
	{
	  if (username[upos] == '.')
	    {
	      upos++;
	      ext = 1;
	    }
	  else
	    break;
	}
      /* Second, skip blanks in base part.  */
      if (dn[dpos] == ' ')
	{
	  if (ext)
	    break;
	  while (dpos < 8 && dn[++dpos] == ' ');
	  if (username[upos] == '.')
	    upos++;
	  ext = 1;
	}
      else
	{
	  if (tolower(dn[dpos]) == tolower(username[upos]))
	    {
	      dpos++;
	      upos++;
	    }
	  else
	    break;
	}
    } while (upos < unamelen && dpos < 11);
  while (dpos < 11 && dn[dpos] == ' ')
    dpos++;
  return (upos == unamelen && dpos == 11);
}

/* Implement the diskfs_lookup callback from the diskfs library.  See
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
  vm_prot_t prot =
    (type == LOOKUP) ? VM_PROT_READ : (VM_PROT_READ | VM_PROT_WRITE);
  memory_object_t memobj;
  vm_address_t buf = 0;
  vm_size_t buflen = 0;
  int blockaddr;
  int idx, lastidx;
  int looped;

  if ((type == REMOVE) || (type == RENAME))
    assert_backtrace (npp);

  if (npp)
    *npp = 0;

  spec_dotdot = type & SPEC_DOTDOT;
  type &= ~SPEC_DOTDOT;

  namelen = strlen (name);

  if (namelen > FAT_NAME_MAX)
    return ENAMETOOLONG;
  
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
  /* We allow extra space in case we have to do an EXTEND.  */
  buflen = round_page (dp->dn_stat.st_size + DIRBLKSIZ);
  err = vm_map (mach_task_self (),
                &buf, buflen, 0, 1, memobj, 0, 0, prot, prot, 0);
  mach_port_deallocate (mach_task_self (), memobj);
  if (err)
    return err;

  inum = 0;

  diskfs_set_node_atime (dp);

  /* Start the lookup at DP->dn->dir_idx.  */
  idx = dp->dn->dir_idx;
  if (idx << LOG2_DIRBLKSIZ > dp->dn_stat.st_size)
    idx = 0;                    /* just in case */
  blockaddr = buf + (idx << LOG2_DIRBLKSIZ);
  looped = (idx == 0);
  lastidx = idx;
  if (lastidx == 0)
    lastidx = dp->dn_stat.st_size >> LOG2_DIRBLKSIZ;

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
          /* We've gotten to the end; start back at the beginning.  */
          looped = 1;
          blockaddr = buf;
          idx = 0;
        }
    }

  diskfs_set_node_atime (dp);
  if (diskfs_synchronous)
    diskfs_node_update (dp, 1);

  /* If err is set here, it's ENOENT, and we don't want to
     think about that as an error yet.  */
  err = 0;

  if (inum && npp)
    {
      if (namelen != 2 || name[0] != '.' || name[1] != '.')
        {
          if (inum == dp->cache_id)
            {
              np = dp;
              diskfs_nref (np);
            }
          else
            {
              err = diskfs_cached_lookup_in_dirbuf (inum, &np, buf);
              if (err)
                goto out;
            }
        }

      /* We are looking up "..".  */
      /* Check to see if this is the root of the filesystem.  */
      else if (dp == diskfs_root_node)
        {
          err = EAGAIN;
          goto out;
        }

      /* We can't just do diskfs_cached_lookup, because we would then
         deadlock.  So we do this.  Ick.  */
      else if (retry_dotdot)
        {
          /* Check to see that we got the same answer as last time.  */
          if (inum != retry_dotdot)
            {
              /* Drop what we *thought* was .. (but isn't any more) and
                 try *again*.  */
              diskfs_nput (np);
              pthread_mutex_unlock (&dp->lock);
              err = diskfs_cached_lookup_in_dirbuf (inum, &np, buf);
              pthread_mutex_lock (&dp->lock);
              if (err)
                goto out;
              retry_dotdot = inum;
              goto try_again;
            }
          /* Otherwise, we got it fine and np is already set properly.  */
        }
      else if (!spec_dotdot)
        {
          /* Lock them in the proper order, and then
             repeat the directory scan to see if this is still
             right.  */
          pthread_mutex_unlock (&dp->lock);
          err = diskfs_cached_lookup_in_dirbuf (inum, &np, buf);
          pthread_mutex_lock (&dp->lock);
          if (err)
            goto out;
          retry_dotdot = inum;
          goto try_again;
        }

      /* Here below are the spec dotdot cases.  */
      else if (type == RENAME || type == REMOVE)
        np = diskfs_cached_ifind (inum);

      else if (type == LOOKUP)
        {
          diskfs_nput (dp);
          err = diskfs_cached_lookup_in_dirbuf (inum, &np, buf);
          if (err)
            goto out;
        }
      else
        assert_backtrace (0);
    }

  if ((type == CREATE || type == RENAME) && !inum && ds && ds->stat == LOOKING)
    {
      /* We didn't find any room, so mark ds to extend the dir.  */
      ds->type = CREATE;
      ds->stat = EXTEND;
      ds->idx = dp->dn_stat.st_size >> LOG2_DIRBLKSIZ;
    }

  /* Return to the user; if we can't, release the reference
     (and lock) we acquired above.  */
 out:
  /* Deallocate or save the mapping.  */
  if ((err && err != ENOENT)
      || !ds
      || ds->type == LOOKUP)
    {
      munmap ((caddr_t) buf, buflen);
      if (ds)
        ds->type = LOOKUP;      /* Set to be ignored by drop_dirstat.  */
    }
  else
    {
      ds->mapbuf = buf;
      ds->mapextent = buflen;
    }

  if (np)
    {
      assert_backtrace (npp);
      if (err)
        {
          if (!spec_dotdot)
            {
              /* Normal case.  */
              if (np == dp)
                diskfs_nrele (np);
              else
                diskfs_nput (np);
            }
          else if (type == RENAME || type == REMOVE)
            /* We just did diskfs_cached_ifind to get np; that allocates
               no new references, so we don't have anything to do.  */
            ;
          else if (type == LOOKUP)
            /* We did diskfs_cached_lookup.  */
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
  vm_address_t currentoff, prevoff = 0;
  struct dirrect *entry = 0;
  size_t nbytes = 0;
  int looking = 0;
  int countcopies = 0;
  int consider_compress = 0;
  inode_t inode;
  vi_key_t entry_key = vi_zero_key;

  /* FAT lacks the "." and ".." directory record in the root directory,
     so we emulate them here.  */
  if (idx == 0 && dp == diskfs_root_node
      && (fatnamematch (FAT_DIR_NAME_DOT, name, namelen)
	  || fatnamematch (FAT_DIR_NAME_DOTDOT, name, namelen)))
    {
      entry_key.dir_inode = diskfs_root_node->cache_id;
      currentoff = blockaddr;
    }
  else
    {
      if (ds && (ds->stat == LOOKING
		 || ds->stat == COMPRESS))
	{
	  looking = 1;
	  countcopies = 1;
	  needed = FAT_DIR_RECORDS (namelen);
	}
      
      for (currentoff = blockaddr, prevoff = 0;
	   currentoff < blockaddr + DIRBLKSIZ;
	   prevoff = currentoff, currentoff += FAT_DIR_REC_LEN)
	{
	  entry = (struct dirrect *)currentoff;
	  
	  if (looking || countcopies)
	    {
	      int thisfree;
	      
	      /* Count how much free space this entry has in it.  */
	      if ((char) entry->name[0] == FAT_DIR_NAME_LAST ||
		  (char) entry->name[0] == FAT_DIR_NAME_DELETED)
		thisfree = FAT_DIR_REC_LEN;
	      else
		thisfree = 0;
	      
	      /* If this isn't at the front of the block, then it will
		 have to be copied if we do a compression; count the
		 number of bytes there too.  */
	      if (countcopies && currentoff != blockaddr)
		nbytes += FAT_DIR_REC_LEN;
	      
	      if (ds->stat == COMPRESS && nbytes > ds->nbytes)
		/* The previously found compress is better than this
		   one, so don't bother counting any more.  */
		countcopies = 0;
	      
	      if (thisfree >= needed)
		{
		  ds->type = CREATE;
		  ds->stat = TAKE;
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
	  
	  if (entry->attribute & FAT_DIR_ATTR_LABEL)
	    /* Either the volume label in root dir or a long filename
	       component.  */
	    continue;
	  
	  if (fatnamematch ((const char *) entry->name, name, namelen))
	    break;
	}

      if (consider_compress
	  && ((enum slot_status) ds->type == LOOKING
	      || ((enum slot_status) ds->type == COMPRESS &&
                  ds->nbytes > nbytes)))
	{
	  ds->type = CREATE;
	  ds->stat = COMPRESS;
	  ds->entry = (struct dirrect *) blockaddr;
	  ds->idx = idx;
	  ds->nbytes = nbytes;
	}
    }
  
  if (currentoff >= blockaddr + DIRBLKSIZ)
    {
      /* The name is not in this block.  */

      return ENOENT;
    }

  /* We have found the required name.  */

  if (ds && type == CREATE)
    ds->type = LOOKUP;          /* It's invalid now.  */
  else if (ds && (type == REMOVE || type == RENAME))
    {
      ds->type = type;
      ds->stat = HERE_TIS;
      ds->entry = entry;
      ds->idx = idx;
      ds->preventry = (struct dirrect *) prevoff;
    }

  if (entry_key.dir_inode)
    {
      /* The required name is "." or ".." in the root dir.  */
      *inum = entry_key.dir_inode;
    }
  else if ((entry->attribute & FAT_DIR_ATTR_DIR)
	   && !memcmp (entry->name, FAT_DIR_NAME_DOT, 11))
    {
      /* "." and ".." have to be treated special. We don't want their
	 directory records, but the records of the directories they
	 point to.  */
      
      *inum = dp->cache_id;
    }
  else if ((entry->attribute & FAT_DIR_ATTR_DIR)
	   && !memcmp (entry->name, FAT_DIR_NAME_DOTDOT, 11))
    {
      if (entry->first_cluster_low[0] == 0
	  && entry->first_cluster_low[1] == 0
	  && entry->first_cluster_high[0] == 0
	  && entry->first_cluster_high[1] == 0)
	{
	  *inum = diskfs_root_node->cache_id;
	}
      else
	{
	  struct vi_key vk = vi_key (dp->dn->inode);
	  *inum = vk.dir_inode;
	}
    }
  else
    {
      entry_key.dir_inode = dp->cache_id;
      entry_key.dir_offset = (currentoff - blockaddr) + (idx << LOG2_DIRBLKSIZ);
      return vi_rlookup(entry_key, inum, &inode, 1);
    }
  return 0;
}

/* Following a lookup call for CREATE, this adds a node to a
   directory.  DP is the directory to be modified; NAME is the name to
   be entered; NP is the node being linked in; DS is the cached
   information returned by lookup; CRED describes the user making the
   call.  This call may only be made if the directory has been held
   locked continuously since the preceding lookup call, and only if
   that call returned ENOENT.  */
error_t
diskfs_direnter_hard (struct node *dp, const char *name, struct node *np,
                      struct dirstat *ds, struct protid *cred)
{
  struct dirrect *new;
  int namelen = strlen (name);
  int needed = FAT_DIR_RECORDS (namelen);
  error_t err;
  loff_t oldsize = 0;

  assert_backtrace (ds->type == CREATE);

  assert_backtrace (!diskfs_readonly);

  dp->dn_set_mtime = 1;

  /* Select a location for the new directory entry.  Each branch of
     this switch is responsible for setting NEW to point to the
     on-disk directory entry being written.  */

  switch (ds->stat)
    {
    case TAKE:
      /* We are supposed to consume this slot.  */
      assert_backtrace ((char)ds->entry->name[0] == FAT_DIR_NAME_LAST
	      || (char)ds->entry->name[0] == FAT_DIR_NAME_DELETED);

      new = ds->entry;
      break;

    case EXTEND:
      /* Extend the file.  */
      assert_backtrace (needed <= bytes_per_cluster);

      oldsize = dp->dn_stat.st_size;
      while (oldsize + bytes_per_cluster > dp->allocsize)
        {
          err = diskfs_grow (dp, oldsize + bytes_per_cluster, cred);
          if (err)
            {
              munmap ((caddr_t) ds->mapbuf, ds->mapextent);
              return err;
            }
	  memset ((caddr_t) ds->mapbuf + oldsize, 0, bytes_per_cluster);
	}

      new = (struct dirrect *) ((char *) ds->mapbuf + oldsize);

      dp->dn_stat.st_size = oldsize + bytes_per_cluster;
      dp->dn_set_ctime = 1;

      break;

    case SHRINK:
    case COMPRESS:
    default:
      assert_backtrace (0);

      /* COMPRESS will be used later, with long filenames, but shrink
	 does not make sense on fat, as all entries have fixed
	 size.  */
    }

  /* NEW points to the directory entry being written.  Now fill in the
     data.  */

  memcpy (new->name, "           ", 11);
  memcpy (new->name, name, namelen % 11); /* XXX */

  write_word (new->first_cluster_low, np->dn->start_cluster & 0xffff);
  write_word (new->first_cluster_high, np->dn->start_cluster >> 16);
  write_dword (new->file_size, np->dn_stat.st_size);
  
  if (!(name[0] == '.' && (name[1] == '\0' 
			   || (name[1] == '.'  && name[2] =='\0'))))
    {
      vi_key_t entry_key;
      
      entry_key.dir_inode = dp->cache_id;
      entry_key.dir_offset = ((int) ds->entry) - ((int) ds->mapbuf);
      
      /* Set the key for this inode now because it wasn't know when
	 the inode was initialized.  */
      vi_change (vi_lookup (np->cache_id), entry_key);
      
      if (np->dn_stat.st_mode & S_IFDIR)
	new->attribute = FAT_DIR_ATTR_DIR;
    }
  else
    new->attribute = FAT_DIR_ATTR_DIR;

  /* Mark the directory inode has having been written.  */
  dp->dn_set_mtime = 1;

  munmap ((caddr_t) ds->mapbuf, ds->mapextent);

  diskfs_file_update (dp, 1);

  return 0;
}

/* Following a lookup call for REMOVE, this removes the link from the
   directory.  DP is the directory being changed and DS is the cached
   information returned from lookup.  This call is only valid if the
   directory has been locked continuously since the call to lookup, and
   only if that call succeeded.  */
error_t
diskfs_dirremove_hard (struct node *dp, struct dirstat *ds)
{
  assert_backtrace (ds->type == REMOVE);
  assert_backtrace (ds->stat == HERE_TIS);

  assert_backtrace (!diskfs_readonly);

  dp->dn_set_mtime = 1;

  ds->entry->name[0] = FAT_DIR_NAME_DELETED;

  /* XXX Do something with dirrect? inode?  */

  dp->dn_set_mtime = 1;

  munmap ((caddr_t) ds->mapbuf, ds->mapextent);

  diskfs_file_update (dp, 1);

  return 0;
}

/* Following a lookup call for RENAME, this changes the inode number
   on a directory entry.  DP is the directory being changed; NP is the
   new node being linked in; DP is the cached information returned by
   lookup.  This call is only valid if the directory has been locked
   continuously since the call to lookup, and only if that call
   succeeded.  */
error_t
diskfs_dirrewrite_hard (struct node *dp, struct node *np, struct dirstat *ds)
{
  error_t err;
  vi_key_t entry_key;
  mach_port_t control = MACH_PORT_NULL;
  struct node *oldnp;
  ino_t inode;
  inode_t vinode;
  
  /*  We need the inode and vinode of the old node.  */
  entry_key.dir_inode = dp->cache_id;
  entry_key.dir_offset = ((int) ds->entry) - ((int) ds->mapbuf);
  err = vi_rlookup (entry_key, &inode, &vinode, 0);
  assert_backtrace (err != EINVAL);
  if (err)
    return err;

  /*  Lookup the node, we already have a reference.  */
  oldnp = diskfs_cached_ifind (inode);

  assert_backtrace (ds->type == RENAME);
  assert_backtrace (ds->stat == HERE_TIS);

  assert_backtrace (!diskfs_readonly);

  /*  The link count must be 0 so the file will be removed and
      the node will be dropped.  */
  oldnp->dn_stat.st_nlink--;
  assert_backtrace (!oldnp->dn_stat.st_nlink);
  
  /* Close the file, free the referenced held by clients.  */
  fshelp_fetch_control (&oldnp->transbox, &control);
  
  if (control)
    {
      fsys_goaway (control, FSYS_GOAWAY_UNLINK);
      mach_port_deallocate (mach_task_self (), control);
    }
  
  /*  Put the new key in the vinode.  */
  vi_change (vi_lookup (np->cache_id), entry_key);
   
  munmap ((caddr_t) ds->mapbuf, ds->mapextent);

  dp->dn_set_mtime = 1;
  diskfs_file_update (dp, 1);

  return 0;
}

/* Tell if DP is an empty directory (has only "." and ".." entries).
   This routine must be called from inside a catch_exception ().  */
int
diskfs_dirempty (struct node *dp, struct protid *cred)
{
  error_t err;
  vm_address_t buf = 0, curoff;
  struct dirrect *entry;
  int hit = 0;                  /* Found something in the directory.  */
  memory_object_t memobj = diskfs_get_filemap (dp, VM_PROT_READ);

  if (memobj == MACH_PORT_NULL)
    /* XXX should reflect error properly.  */
    return 0;

  err = vm_map (mach_task_self (), &buf, dp->dn_stat.st_size, 0,
                1, memobj, 0, 0, VM_PROT_READ, VM_PROT_READ, 0);
  mach_port_deallocate (mach_task_self (), memobj);
  assert_backtrace (!err);

  diskfs_set_node_atime (dp);

  for (curoff = buf;
       !hit && curoff < buf + dp->dn_stat.st_size;
       curoff += FAT_DIR_REC_LEN)
    {
      entry = (struct dirrect *) curoff;

      if (entry->name[0] == FAT_DIR_NAME_LAST)
	break;
      if ((char) entry->name[0] != FAT_DIR_NAME_DELETED
	  && memcmp (entry->name, FAT_DIR_NAME_DOT, 11)
	  && memcmp (entry->name, FAT_DIR_NAME_DOTDOT, 11))
	hit = 1;
    }
  
  diskfs_set_node_atime (dp);
  if (diskfs_synchronous)
    diskfs_node_update (dp, 1);

  munmap ((caddr_t) buf, dp->dn_stat.st_size);

  return !hit;
}

/* Make DS an invalid dirstat.  */
error_t
diskfs_drop_dirstat (struct node *dp, struct dirstat *ds)
{
  if (ds->type != LOOKUP)
    {
      assert_backtrace (ds->mapbuf);
      munmap ((caddr_t) ds->mapbuf, ds->mapextent);
      ds->type = LOOKUP;
    }
  return 0;
}


/* Implement the diskfs_get_directs callback as described in
   <hurd/diskfs.h>.  */
error_t
diskfs_get_directs (struct node *dp,
		    int entry,
		    int nentries,
		    char **data,
		    u_int *datacnt,
		    vm_size_t bufsiz,
		    int *amt)
{
  volatile vm_size_t allocsize;
  struct dirrect *ep;
  struct dirent *userp;
  int i;
  char *datap;
  volatile int ouralloc = 0;
  error_t err;
  vm_prot_t prot = VM_PROT_READ;
  memory_object_t memobj;
  vm_address_t buf = 0, bufp;
  vm_size_t buflen = 0;

  /* Allocate some space to hold the returned data.  */
  allocsize = bufsiz ? round_page (bufsiz) : vm_page_size * 4;
  if (allocsize > *datacnt)
    {
      *data = mmap (0, allocsize, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      ouralloc = 1;
    }

  /* Map in the directory contents.  */
  memobj = diskfs_get_filemap (dp, prot);

  if (memobj == MACH_PORT_NULL)
    return errno;

  /* We allow extra space in case we have to do an EXTEND.  */
  buflen = round_page (dp->dn_stat.st_size);
  err = vm_map (mach_task_self (),
                &buf, buflen, 0, 1, memobj, 0, 0, prot, prot, 0);
  mach_port_deallocate (mach_task_self (), memobj);
  if (err)
    return err;

  bufp = buf;
  for (i = 0; i < entry; i ++)
    {
      /* The root directory in FAT file systems doesn't contain
	 entries for DOT and DOTDOT, they are special cased below.  */
      if (dp == diskfs_root_node && i < 2)
	continue;

      ep = (struct dirrect *) bufp;

      if (bufp >= buf + buflen || (char)ep->name[0] == FAT_DIR_NAME_LAST)
	{
	  /* Not that many entries in the directory; return nothing.  */
	  if (allocsize > *datacnt)
	    munmap (data, allocsize);
	  munmap ((caddr_t) buf, buflen);
	  *datacnt = 0;
	  *amt = 0;
	  return 0;
	}

      /* Ignore and skip deleted and label entries (catches also long
	 filenames).  */
      if ((char)ep->name[0] == FAT_DIR_NAME_DELETED
	  || (ep->attribute & FAT_DIR_ATTR_LABEL))
	i--;
      bufp = bufp + FAT_DIR_REC_LEN;
    }

  /* Now copy entries one at a time.  */
  i = 0;
  datap = *data;
  while (((nentries == -1) || (i < nentries))
	 && (!bufsiz || datap - *data < bufsiz)
	 && bufp < buf + buflen)
    {
      char name[13];
      size_t namlen, reclen;
      struct dirrect dot = { FAT_DIR_NAME_DOT, FAT_DIR_ATTR_DIR };
      struct dirrect dotdot = { FAT_DIR_NAME_DOTDOT, FAT_DIR_ATTR_DIR };

      /* The root directory in FAT file systems doesn't contain
	 entries for DOT and DOTDOT, they are special cased below.  */
      if (dp == diskfs_root_node && (i + entry == 0))
        ep = &dot;
      else if (dp == diskfs_root_node && (i + entry == 1))
        ep = &dotdot;
      else
	ep = (struct dirrect *) bufp;

      if ((char)ep->name[0] == FAT_DIR_NAME_LAST)
	{
	  /* Last entry.  */
	  bufp = buf + buflen;
	  continue;
	}

      if ((char)ep->name[0] == FAT_DIR_NAME_DELETED || (ep->attribute & FAT_DIR_ATTR_LABEL))
	{
	  bufp = bufp + FAT_DIR_REC_LEN;
  	  continue;
	}

      /* See if there's room to hold this one.  */
      
      fat_to_unix_filename ((const char *) ep->name, name);
      namlen = strlen(name);

      /* Perhaps downcase it?  */

      reclen = sizeof (struct dirent) + namlen;
      reclen = (reclen + 3) & ~3;

      /* Expand buffer if necessary.  */
      if (datap - *data + reclen > allocsize)
	{
	  vm_address_t newdata;

	  vm_allocate (mach_task_self (), &newdata,
		       (ouralloc
			? (allocsize *= 2)
			: (allocsize = vm_page_size * 2)), 1);
	  memcpy ((void *) newdata, (void *) *data, datap - *data);

	  if (ouralloc)
	    munmap (*data, allocsize / 2);

 	  datap = (char *) newdata + (datap - *data);
	  *data = (char *) newdata;
	  ouralloc = 1;
	}

      userp = (struct dirent *) datap;

      /* Fill in entry.  */
      {
        ino_t inode;
	inode_t v_inode;
	vi_key_t entry_key;

	entry_key.dir_inode = dp->cache_id;
	entry_key.dir_offset = bufp - buf;

	vi_rlookup (entry_key, &inode, &v_inode, 1);
	userp->d_fileno = inode;
      }
      userp->d_type = DT_UNKNOWN;
      userp->d_reclen = reclen;
      userp->d_namlen = namlen;
      memcpy (userp->d_name, name, namlen);
      userp->d_name[namlen] = '\0';

      /* And move along.  */
      datap = datap + reclen;
      if (!(dp == diskfs_root_node && i + entry < 2))
	bufp = bufp + FAT_DIR_REC_LEN;
      i++;
    }

  /* If we didn't use all the pages of a buffer we allocated, free
     the excess.  */
  if (ouralloc
      && round_page (datap - *data) < round_page (allocsize))
    munmap ((caddr_t) round_page (datap),
	    round_page (allocsize) - round_page (datap - *data));

  munmap ((caddr_t) buf, buflen);

  /* Return.  */
  *amt = i;
  *datacnt = datap - *data;
  return 0;
}
