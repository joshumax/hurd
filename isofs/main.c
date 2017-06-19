/*
   Copyright (C) 1997,98,99,2002 Free Software Foundation, Inc.
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

#include <string.h>
#include <error.h>
#include <argp.h>
#include <version.h>
#include <limits.h>
#include "isofs.h"

struct node *diskfs_root_node;
struct store *store = 0;
struct store_parsed *store_parsed = 0;
char *diskfs_disk_name = 0;

char *diskfs_server_name = "iso9660fs";
char *diskfs_server_version = HURD_VERSION;
char *diskfs_extra_version = "GNU Hurd";
int diskfs_synchronous = 0;

int diskfs_link_max = INT_MAX;
int diskfs_name_max = 255;	/* see iso9660.h: struct dirrect::namelen */
int diskfs_maxsymlinks = 8;


/* Fetch the root node */
static void
fetch_root ()
{
  struct lookup_context ctx;
  ino_t id;
  error_t err;

  ctx.dr = (struct dirrect *) sblock->root;

  /* First check for SUSP and all relevant extensions */
  rrip_initialize (ctx.dr);

  /* Now rescan the node for real */
  rrip_lookup (ctx.dr, &ctx.rr, 1);

  err = cache_id (ctx.dr, &ctx.rr, &id);
  assert_perror_backtrace (err);

  /* And fetch the node. */
  err = diskfs_cached_lookup_context (id, &diskfs_root_node, &ctx);
  assert_perror_backtrace (err);

  pthread_mutex_unlock (&diskfs_root_node->lock);
}


/* Find and read the superblock.  */
static void
read_sblock ()
{
  struct voldesc *vd;
  struct sblock * volatile sb = 0;

  /* Start at logical sector 16 and keep going until
     we find a matching superblock */
  for (vd = disk_image + (logical_sector_size * 16);
       (void *) vd < disk_image + (logical_sector_size * 500) /* for sanity */
         && (void *) vd + logical_sector_size < disk_image + disk_image_len;
       vd = (void *) vd + logical_sector_size)
    {
      if (vd->type == VOLDESC_END)
	break;

      if (vd->type == VOLDESC_PRIMARY
	  && !memcmp (ISO_STANDARD_ID, vd->id, 5)
	  && vd->version == 1)
	{
	  /* Here's a valid primary descriptor. */
	  sb = (struct sblock *) vd;
	  break;
	}
    }

  if (!sb)
    error (1, 0, "Could not find valid superblock");

  sblock = malloc (sizeof (struct sblock));
  if (!sblock)
    error (1, errno, "Could not allocate memory for superblock");
  memcpy (sblock, sb, sizeof (struct sblock));

  /* Parse some important bits of this */
  logical_block_size = isonum_723 (sblock->blksize);
}

/* Override the standard diskfs routine so we can add our own output.  */
error_t
diskfs_append_args (char **argz, size_t *argz_len)
{
  error_t err;

  /* Get the standard things.  */
  err = diskfs_append_std_options (argz, argz_len);

  if (! err)
    err = store_parsed_append_args (store_parsed, argz, argz_len);

  return err;
}

int
main (int argc, char **argv)
{
  mach_port_t bootstrap;

  /* This filesystem is never capable of writing.  */
  diskfs_readonly = 1;
  diskfs_hard_readonly = 1;

  /* Initialize the diskfs library, parse arguments, and open the store.
     This starts the first diskfs thread for us.  */
  store = diskfs_init_main (NULL, argc, argv, &store_parsed, &bootstrap);

  create_disk_pager ();

  read_sblock ();

  fetch_root ();

  diskfs_startup_diskfs (bootstrap, 0);

  pthread_exit (NULL);

  return 0;
}

/* Nothing to do for read-only medium */
error_t
diskfs_reload_global_state ()
{
  return 0;
}

error_t
diskfs_set_hypermetadata (int wait, int clean)
{
  return 0;
}

void
diskfs_readonly_changed (int readonly)
{
  /* We should never get here because we set diskfs_hard_readonly above. */
  abort ();
}
