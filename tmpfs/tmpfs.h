/* Private data structures for tmpfs.
   Copyright (C) 2000 Free Software Foundation, Inc.

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

#ifndef _tmpfs_h
#define _tmpfs_h 1

#include <hurd/diskfs.h>
#include <sys/types.h>
#include <dirent.h>
#include <stdint.h>

struct disknode
{
  uint_fast8_t type;		/* DT_REG et al */

  unsigned int gen;
  off_t size;
  mode_t mode;
  nlink_t nlink;
  uid_t uid, author;
  gid_t gid;
  struct timespec atime, mtime, ctime;
  unsigned int flags;

  char *trans;
  size_t translen;

  union
  {
    char *lnk;			/* malloc'd symlink target */
    struct
    {
      mach_port_t memobj;
      vm_address_t memref;
      unsigned int allocpages;	/* largest size while memobj was live */
    } reg;
    struct
    {
      struct tmpfs_dirent *entries;
      struct disknode *dotdot;
    } dir;
    dev_t chr, blk;
  } u;

  struct node *hnext, **hprevp;
};

struct tmpfs_dirent
{
  struct tmpfs_dirent *next;
  struct disknode *dn;
  uint8_t namelen;
  char name[0];
};

extern off_t tmpfs_page_limit;
extern mach_port_t default_pager;

/* These two must be accessed using atomic operations.  */
extern unsigned int num_files;
extern off_t tmpfs_space_used;

/* Convenience function to adjust tmpfs_space_used.  */
static inline void
adjust_used (off_t change)
{
  __atomic_add_fetch (&num_files, change, __ATOMIC_RELAXED);
}

/* Convenience function to get tmpfs_space_used.  */
static inline off_t
get_used (void)
{
  return __atomic_load_n (&num_files, __ATOMIC_RELAXED);
}

#endif
