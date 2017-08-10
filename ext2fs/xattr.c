 /* Ext2 support for extended attributes

   Copyright (C) 2006, 2016 Free Software Foundation, Inc.

   Written by Thadeu Lima de Souza Cascardo <cascardo@dcc.ufmg.br>
   and Shengyu Zhang <lastavengers@outlook.com>

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

#include "ext2fs.h"
#include "xattr.h"
#include <stdlib.h>
#include <string.h>
#include <sys/xattr.h>

struct _xattr_prefix
{
  int index;
  char *prefix;
  ssize_t size;
};

/* Prefixes are represented as numbers when stored in ext2 filesystems. */
struct _xattr_prefix
xattr_prefixes[] =
{
  {
  1, "user.", sizeof "user." - 1},
  {
  7, "gnu.", sizeof "gnu." - 1},
  {
  0, NULL, 0}
};

/*
 * Given an attribute name in full_name, the ext2 number (index) and
 * suffix name (name) are given.  Returns the index in the array
 * indicating whether a corresponding prefix was found or not.
 */
static int
xattr_name_prefix (const char *full_name, int *index, const char **name)
{
  int i;

  for (i = 0; xattr_prefixes[i].prefix != NULL; i++)
    {
      if (!strncmp (xattr_prefixes[i].prefix, full_name,
		    xattr_prefixes[i].size))
	{
	  *name = full_name + xattr_prefixes[i].size;
	  *index = xattr_prefixes[i].index;
	  break;
	}
    }
  return i;
}

#define NAME_HASH_SHIFT 5
#define VALUE_HASH_SHIFT 16

/* Given a xattr block header and a entry, compute the hash of this
 * entry.
 */
static void
xattr_entry_hash (struct ext2_xattr_header *header,
		  struct ext2_xattr_entry *entry)
{

  __u32 hash = 0;
  char *name = entry->e_name;
  int n;

  for (n = 0; n < entry->e_name_len; n++)
    {
      hash = (hash << NAME_HASH_SHIFT)
        ^ (hash >> (8 * sizeof (hash) - NAME_HASH_SHIFT))
	    ^ *name++;
    }

  if (entry->e_value_block == 0 && entry->e_value_size != 0)
    {
      __u32 *value = (__u32 *) ((char *) header + entry->e_value_offs);
      for (n = (entry->e_value_size + EXT2_XATTR_ROUND) >>
	      EXT2_XATTR_PAD_BITS; n; n--)
	{
	  hash = (hash << VALUE_HASH_SHIFT)
	      ^ (hash >> (8 * sizeof (hash) - VALUE_HASH_SHIFT))
	      ^ *value++;
	}
    }

  entry->e_hash = hash;

}

#undef NAME_HASH_SHIFT
#undef VALUE_HASH_SHIFT

#define BLOCK_HASH_SHIFT 16

/* Given a xattr block header and a entry, re-compute the
 * hash of the entry after it has changed, and computer the hash
 * of the header.
 */
static void
xattr_entry_rehash (struct ext2_xattr_header *header,
		    struct ext2_xattr_entry *entry)
{

  __u32 hash = 0;
  struct ext2_xattr_entry *position;

  xattr_entry_hash (header, entry);

  position = EXT2_XATTR_ENTRY_FIRST (header);
  while (!EXT2_XATTR_ENTRY_LAST (position))
    {
      if (position->e_hash == 0)
	{
	  /* Block is not shared if an entry's hash value == 0 */
	  hash = 0;
	  break;
	}

      hash = (hash << BLOCK_HASH_SHIFT)
	  ^ (hash >> (8 * sizeof (hash) - BLOCK_HASH_SHIFT))
	  ^ position->e_hash;

      position = EXT2_XATTR_ENTRY_NEXT (position);
    }

  header->h_hash = hash;

}

#undef BLOCK_HASH_SHIFT

/*
 * Given an entry, appends its name to a buffer.  The provided buffer
 * length is reduced by the name size, even if the buffer is NULL (for
 * computing the list size).  Returns EOPNOTSUPP (operation not
 * supported) if the entry index cannot be found on the array of
 * supported prefixes.  If a buffer is provided (not NULL) and its
 * length is not enough for name, ERANGE is returned.
 */
static error_t
xattr_entry_list (struct ext2_xattr_entry *entry, char *buffer, size_t *len)
{

  int i;
  size_t size;

  for (i = 0; xattr_prefixes[i].prefix != NULL; i++)
    {
      if (entry->e_name_index == xattr_prefixes[i].index)
	break;
    }

  if (xattr_prefixes[i].prefix == NULL)
    return EOPNOTSUPP;

  size = xattr_prefixes[i].size + entry->e_name_len + 1;

  if (buffer)
    {
      if (size <= *len)
	{
	  memcpy (buffer, xattr_prefixes[i].prefix, xattr_prefixes[i].size);
	  buffer += xattr_prefixes[i].size;
	  memcpy (buffer, entry->e_name, entry->e_name_len);
	  buffer += entry->e_name_len;
	  *buffer++ = 0;
	}
      else
	{
	  return ERANGE;
	}
    }

  *len -= size;
  return 0;

}

/*
 * Given the xattr block, an entry and a attribute name, retrieves its
 * value. The value length is also returned through parameter len.  In
 * case the name prefix cannot be found in the prefix array,
 * EOPNOTSUPP is returned, indicating the prefix is not supported.  In
 * case there is not enough space in the buffer provided, ERANGE is
 * returned.  If the value buffer was NULL, the length is returned
 * through len parameter and the function is successfull (returns 0).
 * If the entry does not match the name, ENODATA is returned, and
 * parameter cmp is set to the comparison value (less than 0 if a
 * entry with name full_name should be before the current entry,
 * more than 0 otherwise.
 */
static error_t
xattr_entry_get (void *block, struct ext2_xattr_entry *entry,
		 const char *full_name, char *value, size_t *len, int *cmp)
{

  int i;
  int index;
  int tmp_cmp;
  const char *name;

  i = xattr_name_prefix (full_name, &index, &name);

  if (xattr_prefixes[i].prefix == NULL)
    return EOPNOTSUPP;

  tmp_cmp = index - entry->e_name_index;
  if (!tmp_cmp)
    tmp_cmp = strlen (name) - entry->e_name_len;
  if (!tmp_cmp)
    tmp_cmp = strncmp (name, entry->e_name, entry->e_name_len);

  if (tmp_cmp)
    {
      if (cmp)
	*cmp = tmp_cmp;
      return ENODATA;
    }

  if (value)
    {
      if (*len < entry->e_value_size)
	{
	  return ERANGE;
	}
      memcpy (value, block + entry->e_value_offs, entry->e_value_size);
    }

  *len = entry->e_value_size;
  return 0;

}

/*
 * Creates an entry in the xattr block, giving its header, the last
 * entry, the position where this new one should be inserted, the name
 * of the attribute, its value and the value length, and, the
 * remaining space in the block (parameter rest).  If no space is
 * available for the required size of the entry, ERANGE is returned.
 */
static error_t
xattr_entry_create (struct ext2_xattr_header *header,
		    struct ext2_xattr_entry *last,
		    struct ext2_xattr_entry *position,
		    const char *full_name, const char *value,
		    size_t len, size_t rest)
{

  int i;
  size_t name_len;
  off_t start;
  off_t end;
  size_t entry_size;
  size_t value_size;
  int index;
  const char *name;

  i = xattr_name_prefix (full_name, &index, &name);

  if (xattr_prefixes[i].prefix == NULL)
    return EOPNOTSUPP;

  name_len = strlen (name);
  entry_size = EXT2_XATTR_ENTRY_SIZE (name_len);
  value_size = EXT2_XATTR_ALIGN (len);

  if (rest < 4 || entry_size + value_size > rest - 4)
    {
      return ERANGE;
    }

  start = EXT2_XATTR_ENTRY_OFFSET (header, position);
  end = EXT2_XATTR_ENTRY_OFFSET (header, last);

  /* Leave room for new entry */
  memmove ((char *) position + entry_size, position, end - start);

  position->e_name_len = name_len;
  position->e_name_index = index;
  position->e_value_offs = end + rest - value_size;
  position->e_value_block = 0;
  position->e_value_size = len;
  strncpy (position->e_name, name, name_len);

  memcpy ((char *) header + position->e_value_offs, value, len);
  memset ((char *) header + position->e_value_offs + len, 0,
	  value_size - len);

  return 0;

}

/*
 * Removes an entry from the xattr block, giving a pointer to the
 * block header, the last attribute entry, the position of the entry
 * to be removed and the remaining space in the block.
 */
static error_t
xattr_entry_remove (struct ext2_xattr_header *header,
		    struct ext2_xattr_entry *last,
		    struct ext2_xattr_entry *position, size_t rest)
{

  size_t size;
  off_t start;
  off_t end;
  struct ext2_xattr_entry *entry;

  /* Remove the value */
  size = EXT2_XATTR_ALIGN (position->e_value_size);
  start = EXT2_XATTR_ENTRY_OFFSET (header, last) + rest;
  end = position->e_value_offs;

  memmove ((char *) header + start + size, (char *) header + start,
	   end - start);
  memset ((char *) header + start, 0, size);

  /* Adjust all value offsets */
  entry = EXT2_XATTR_ENTRY_FIRST (header);
  while (!EXT2_XATTR_ENTRY_LAST (entry))
    {
      if (entry->e_value_offs < end)
	entry->e_value_offs += size;
      entry = EXT2_XATTR_ENTRY_NEXT (entry);
    }

  /* Remove the name */
  size = EXT2_XATTR_ENTRY_SIZE (position->e_name_len);
  start = EXT2_XATTR_ENTRY_OFFSET (header, position);
  end = EXT2_XATTR_ENTRY_OFFSET (header, last);

  memmove ((char *) header + start , (char *) header + start + size,
	   end - (start + size));
  memset ((char *) header + end - size, 0, size);

  return 0;

}

/*
 * Replaces the value of an existing attribute entry, given the block
 * header, the last entry, the entry whose value should be replaced,
 * the new value, its length, and the remaining space in the block.
 * Returns ERANGE if there is not enough space (when the new value is
 * bigger than the old one).
 */
static error_t
xattr_entry_replace (struct ext2_xattr_header *header,
		     struct ext2_xattr_entry *last,
		     struct ext2_xattr_entry *position,
		     const char *value, size_t len, size_t rest)
{

  size_t old_size;
  size_t new_size;

  old_size = EXT2_XATTR_ALIGN (position->e_value_size);
  new_size = EXT2_XATTR_ALIGN (len);

  if (rest < 4 || new_size - old_size > rest - 4)
    return ERANGE;

  if (new_size != old_size)
    {
      off_t start;
      off_t end;
      struct ext2_xattr_entry *entry;

      start = EXT2_XATTR_ENTRY_OFFSET (header, last) + rest;
      end = position->e_value_offs;

      /* Remove the old value */
      memmove ((char *) header + start + old_size, (char *) header + start,
	       end - start);

      /* Adjust all value offsets */
      entry = EXT2_XATTR_ENTRY_FIRST (header);
      while (!EXT2_XATTR_ENTRY_LAST (entry))
	{
	  if (entry->e_value_offs < end)
	    entry->e_value_offs += old_size;
	  entry = EXT2_XATTR_ENTRY_NEXT (entry);
	}

      position->e_value_offs = start - (new_size - old_size);
    }

  position->e_value_size = len;

  /* Write the new value */
  memcpy ((char *) header + position->e_value_offs, value, len);
  memset ((char *) header + position->e_value_offs + len, 0, new_size - len);

  return 0;

}


/*
 * Given a node, free extended attributes block associated with
 * this node.
 */
error_t
ext2_free_xattr_block (struct node *np)
{
  error_t err;
  block_t blkno;
  void *block;
  struct ext2_inode *ei;
  struct ext2_xattr_header *header;

  if (!EXT2_HAS_COMPAT_FEATURE (sblock, EXT2_FEATURE_COMPAT_EXT_ATTR))
    {
      ext2_debug ("Filesystem has no support for extended attributes.");
      return EOPNOTSUPP;
    }

  err = 0;
  block = NULL;

  ei = dino_ref (np->cache_id);
  blkno = ei->i_file_acl;

  if (blkno == 0)
    {
      err = 0;
      goto cleanup;
    }

  assert_backtrace (!diskfs_readonly);

  block = disk_cache_block_ref (blkno);
  header = EXT2_XATTR_HEADER (block);

  if (header->h_magic != EXT2_XATTR_BLOCK_MAGIC || header->h_blocks != 1)
    {
      ext2_warning ("Invalid extended attribute block.");
      err = EIO;
      goto cleanup;
    }

  if (header->h_refcount == 1)
    {
       ext2_debug("free block %d", blkno);

       disk_cache_block_deref (block);
       ext2_free_blocks(blkno, 1);

       np->dn_stat.st_blocks -= 1 << log2_stat_blocks_per_fs_block;
       np->dn_stat.st_mode &= ~S_IPTRANS;
       np->dn_set_ctime = 1;
    }
  else
    {
       ext2_debug("h_refcount: %d", header->h_refcount);

       header->h_refcount--;
       record_global_poke (block);
    }


  ei->i_file_acl = 0;
  record_global_poke (ei);

  return err;

cleanup:
  if (block)
    disk_cache_block_deref (block);

  dino_deref (ei);

  return err;

}

/*
 * Given a node, return its list of attribute names in a buffer.
 * The size of used/required buffer will returned through parameter
 * len, even if the buffer is NULL.  Returns EOPNOTSUPP if underlying
 * filesystem has no extended attributes support.  Returns EIO if
 * xattr block is invalid (has no valid h_magic number).
 */
error_t
ext2_list_xattr (struct node *np, char *buffer, size_t *len)
{

  error_t err;
  block_t blkno;
  void *block;
  struct ext2_inode *ei;
  struct ext2_xattr_header *header;
  struct ext2_xattr_entry *entry;

  if (!EXT2_HAS_COMPAT_FEATURE (sblock, EXT2_FEATURE_COMPAT_EXT_ATTR))
    {
      ext2_debug ("Filesystem has no support for extended attributes.");
      return EOPNOTSUPP;
    }

  if (!len)
    return EINVAL;

  size_t size = *len;

  ei = dino_ref (np->cache_id);
  blkno = ei->i_file_acl;
  dino_deref (ei);

  if (blkno == 0)
    {
      *len = 0;
      return 0;
    }

  err = EIO;
  block = disk_cache_block_ref (blkno);

  header = EXT2_XATTR_HEADER (block);
  if (header->h_magic != EXT2_XATTR_BLOCK_MAGIC || header->h_blocks != 1)
    {
      ext2_warning ("Invalid extended attribute block.");
      err = EIO;
      goto cleanup;
    }

  entry = EXT2_XATTR_ENTRY_FIRST (header);

  while (!EXT2_XATTR_ENTRY_LAST (entry))
    {
      err = xattr_entry_list (entry, buffer, &size);
      if (err)
	goto cleanup;
      if (buffer)
        buffer += strlen (buffer) + 1;
      entry = EXT2_XATTR_ENTRY_NEXT (entry);
    }

  *len = *len - size;

cleanup:
  disk_cache_block_deref (block);

  return err;

}


/*
 * Given a node and an attribute name, returns the value and its
 * length in a buffer. The length is returned through parameter len
 * even if the value is NULL.  May return EOPNOTSUPP if underlying
 * filesystem does not support extended attributes or the given name
 * prefix.  If there is no sufficient space in value buffer or
 * attribute name is too long, returns ERANGE.  Returns EIO if xattr
 * block is invalid and ENODATA if there is no such block or no entry
 * in the block matching the name.
 */
error_t
ext2_get_xattr (struct node *np, const char *name, char *value, size_t *len)
{

  size_t size;
  int err;
  void *block;
  struct ext2_inode *ei;
  struct ext2_xattr_header *header;
  struct ext2_xattr_entry *entry;

  if (!EXT2_HAS_COMPAT_FEATURE (sblock, EXT2_FEATURE_COMPAT_EXT_ATTR))
    {
      ext2_debug ("Filesystem has no support for extended attributes.");
      return EOPNOTSUPP;
    }

  if (!name || !len)
    return EINVAL;

  if (strlen(name) > 255)
    return ERANGE;

  ei = dino_ref (np->cache_id);

  if (ei->i_file_acl == 0)
    {
      dino_deref (ei);
      return ENODATA;
    }

  block = disk_cache_block_ref (ei->i_file_acl);
  dino_deref (ei);

  header = EXT2_XATTR_HEADER (block);
  if (header->h_magic != EXT2_XATTR_BLOCK_MAGIC || header->h_blocks != 1)
    {
      ext2_warning ("Invalid extended attribute block.");
      err = EIO;
      goto cleanup;
    }

  err = ENODATA;
  entry = EXT2_XATTR_ENTRY_FIRST (header);

  while (!EXT2_XATTR_ENTRY_LAST (entry))
    {
      size = *len;
      err = xattr_entry_get (block, entry, name, value, &size, NULL);
      if (err!= ENODATA)
	break;
      entry = EXT2_XATTR_ENTRY_NEXT (entry);
    }

  if (!err)
    *len = size;

cleanup:
  disk_cache_block_deref (block);

  return err;

}

/*
 * Set the value of an attribute giving the node, the attribute name,
 * value, the value length and flags. If name or value is too long,
 * ERANGE is returned.  If flags is XATTR_CREATE, the
 * attribute is created if no existing matching entry is found.
 * Otherwise, EEXIST is returned.  If flags is XATTR_REPLACE, the
 * attribute value is replaced if an entry is found and ENODATA is
 * returned otherwise.  If no flags are used, the entry is properly
 * created or replaced.  The entry is removed if value is NULL and no
 * flags are used.  In this case, if any flags are used, EINVAL is
 * returned.  If no matching entry is found, ENODATA is returned.
 * EOPNOTSUPP is returned in case extended attributes or the name
 * prefix are not supported.  If there is no space available in the
 * block, ERANGE is returned.  If there is no any entry after removing
 * the specified entry, free the xattr block.
 */
error_t
ext2_set_xattr (struct node *np, const char *name, const char *value,
		size_t len, int flags)
{

  int found;
  size_t rest;
  error_t err;
  block_t blkno;
  void *block = NULL;
  struct ext2_inode *ei;
  struct ext2_xattr_header *header;
  struct ext2_xattr_entry *entry;
  struct ext2_xattr_entry *location;

  if (!EXT2_HAS_COMPAT_FEATURE (sblock, EXT2_FEATURE_COMPAT_EXT_ATTR))
    {
      ext2_warning ("Filesystem has no support for extended attributes.");
      return EOPNOTSUPP;
    }

  if (!name)
    return EINVAL;

  if (strlen(name) > 255 || len > block_size)
    return ERANGE;

  ei = dino_ref (np->cache_id);
  blkno = ei->i_file_acl;

  /* Avoid allocating a block if this is a request to delete data.  */
  if (blkno == 0 && value == NULL)
    {
      block = NULL;
      err = ENODATA;
      goto cleanup;
    }

  if (blkno == 0)
    {
      /* Allocate and initialize new block */
      block_t goal;

      assert_backtrace (!diskfs_readonly);

      goal = sblock->s_first_data_block + np->dn->info.i_block_group *
	EXT2_BLOCKS_PER_GROUP (sblock);
      blkno = ext2_new_block (goal, 0, 0, 0);

      if (blkno == 0)
	{
	  err = ENOSPC;
	  goto cleanup;
	}

      block = disk_cache_block_ref (blkno);
      memset (block, 0, block_size);

      header = EXT2_XATTR_HEADER (block);
      header->h_magic = EXT2_XATTR_BLOCK_MAGIC;
      header->h_blocks = 1;
      header->h_refcount = 1;
    }
  else
    {
      block = disk_cache_block_ref (blkno);
      header = EXT2_XATTR_HEADER (block);
      if (header->h_magic != EXT2_XATTR_BLOCK_MAGIC || header->h_blocks != 1)
	{
	  ext2_warning ("Invalid extended attribute block.");
	  err = EIO;
	  goto cleanup;
	}
    }

  entry = EXT2_XATTR_ENTRY_FIRST (header);
  location = NULL;

  rest = block_size;
  err = ENODATA;
  found = FALSE;

  while (!EXT2_XATTR_ENTRY_LAST (entry))
    {
      size_t size;
      int cmp;

      err = xattr_entry_get (NULL, entry, name, NULL, &size, &cmp);
      if (err == 0)
	{
	  location = entry;
	  found = TRUE;
	}
      else if (err == ENODATA)
	{
	  /* The xattr entries are sorted by attribute name */
	  if (cmp < 0 && !found)
	    {
	      location = entry;
	      found = FALSE;
	    }
	}
      else
	{
	  break;
	}

      rest -= EXT2_XATTR_ALIGN (entry->e_value_size);
      entry = EXT2_XATTR_ENTRY_NEXT (entry);
    }

  if (err != 0 && err != ENODATA)
    {
      goto cleanup;
    }

  if (location == NULL)
    location = entry;

  rest = rest - EXT2_XATTR_ENTRY_OFFSET (header, entry);
  ext2_debug("space rest: %d", rest);

  /* 4 null bytes after xattr entry */
  if (rest < 4)
    {
      err = EIO;
      goto cleanup;
    }

  if (value && flags & XATTR_CREATE)
    {
      if (found)
	{
	  err = EEXIST;
	  goto cleanup;
	}
      else
	err = xattr_entry_create (header, entry, location, name, value, len,
	  rest);
    }
  else if (value && flags & XATTR_REPLACE)
    {
      if (!found)
	{
	  err = ENODATA;
	  goto cleanup;
	}
      else
	err = xattr_entry_replace (header, entry, location, value, len, rest);
    }
  else if (value)
    {
      if (found)
	err = xattr_entry_replace (header, entry, location, value, len, rest);
      else
	err = xattr_entry_create (header, entry, location, name, value, len,
		rest);
    }
  else
    {
      if (flags & XATTR_REPLACE || flags & XATTR_CREATE)
	{
	  err = EINVAL;
	  goto cleanup;
	}
      else if (!found)
	{
	  err = ENODATA;
	  goto cleanup;
	}
      else
	err = xattr_entry_remove (header, entry, location, rest);
    }

  /* Check if the xattr block is empty */
  entry = EXT2_XATTR_ENTRY_FIRST (header);
  int empty = EXT2_XATTR_ENTRY_LAST (entry);

  if (err == 0)
    {
      if (empty)
	{
	  disk_cache_block_deref (block);
	  dino_deref (ei);

	  return ext2_free_xattr_block (np);
	}
      else
	{
	  xattr_entry_rehash (header, location);

	  record_global_poke (block);

	  if (ei->i_file_acl == 0)
	    {
	      np->dn_stat.st_blocks += 1 << log2_stat_blocks_per_fs_block;
	      np->dn_set_ctime = 1;

	      ei->i_file_acl = blkno;
	      record_global_poke (ei);
	    }
	  else
	      dino_deref (ei);

	  return 0;
	}
    }

cleanup:
  if (block)
    disk_cache_block_deref (block);
  if (ei->i_file_acl == 0 && blkno != 0)
    /* We allocated a block, but for some reason we did not register
       it.  */
    ext2_free_blocks (blkno, 1);
  dino_deref (ei);

  return err;

}
