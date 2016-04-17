/*
   Copyright (C) 1997, 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */


#include <sys/types.h>
#include <sys/mman.h>
#include <hurd/diskfs.h>
#include <hurd/diskfs-pager.h>
#include <hurd/store.h>

#include "rr.h"

/* There is no such thing as an inode in this format, all such informatio n
   being recorded in the directory entry.  So we report inode numbers as
   absolute offsets from DISK_IMAGE. */

struct disknode
{
  struct dirrect *dr; /* Somewhere in disk_image.  */

  off_t file_start; /* In store->block_size units */

  struct user_pager_info *fileinfo;

  char *link_target;		/* for S_ISLNK */

  size_t translen;
  char *translator;
};

struct user_pager_info
{
  struct node *np;
  enum pager_type
    {
      DISK,
      FILE_DATA,
    } type;
    struct pager *p;
};

struct lookup_context
{
  /* The directory record.  Points somewhere into the disk_image.  */
  struct dirrect *dr;

  /* The results of an rrip_scan_lookup call for this node.  */
  struct rrip_lookup rr;
};

/* The physical media */
extern struct store *store;

char *host_name;

/* Name we are mounted on, with trailing slash */
char *mounted_on;

/* Mapped image of disk */
void *disk_image;
size_t disk_image_len;

/* Processed sblock info */

/* Block size of pointers etc. on disk (6.2.2). */
size_t logical_block_size;

/* Size of "logical sectors" (6.1.2).  These are 2048 or the
   largest power of two that will fit in a physical sector, whichever is
   greater.  I don't know how to fetch the physical sector size; so
   we'll just use a constant. */
#define logical_sector_size	2048

/* Unprocessed superblock */
struct sblock *sblock;



void drop_pager_softrefs (struct node *);
void allow_pager_softrefs (struct node *);
void create_disk_pager (void);

/* Given RECORD and RR, calculate the cache id.  */
error_t cache_id (struct dirrect *record, struct rrip_lookup *rr, ino_t *idp);

error_t calculate_file_start (struct dirrect *, off_t *, struct rrip_lookup *);

char *isodate_915 (char *, struct timespec *);
char *isodate_84261 (char *, struct timespec *);
