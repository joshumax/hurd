/* 
   Copyright (C) 1997 Free Software Foundation, Inc.
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

char *diskfs_server_name = "isofs";
char *diskfs_server_version = HURD_VERSION;
char *diskfs_extra_version = "GNU Hurd";
int diskfs_synchronous = 0;

int diskfs_link_max = INT_MAX;
int diskfs_maxsymlinks = 8;

int diskfs_readonly = 1;

/* Fetch the root node */
static void
fetch_root ()
{
  struct rrip_lookup rl;
  struct dirrect *dr;
  error_t err;

  dr = (struct dirrect *) sblock->root;

  /* First check for SUSP and all relevant extensions */
  rrip_initialize (dr);
  
  /* Now rescan the node for real */
  rrip_lookup (dr, &rl, 1);
  
  /* And fetch the node. */
  err = load_inode (&diskfs_root_node, dr, &rl);
  assert_perror (err);
  
  mutex_unlock (&diskfs_root_node->lock);
}


/* Find and read the superblock.  */
static void
read_sblock ()
{
  struct voldesc *vd;
  error_t err;
  struct sblock * volatile sb = 0;
  
  err = diskfs_catch_exception ();
  if (err)
    error (4, err, "reading superblock");
  
  /* Start at logical sector 16 and keep going until
     we find a matching superblock */
  for (vd = disk_image + (logical_sector_size * 16);
       (void *) vd < disk_image + (logical_sector_size * 500); /* for sanity */
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
  bcopy (sb, sblock, sizeof (struct sblock));
  diskfs_end_catch_exception ();

  /* Parse some important bits of this */
  logical_block_size = isonum_723 (sblock->blksize);
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct store_argp_params store_params = { 0 };

  argp_parse (&diskfs_store_startup_argp, argc, argv, 0, 0, &store_params);
  store_parsed = store_params.result;
  
  err = store_parsed_name (store_parsed, &diskfs_disk_name);
  if (err)
    error (2, err, "store_parsed_name");
  
  diskfs_console_stdio ();
  
  if (diskfs_boot_flags)
    bootstrap = MACH_PORT_NULL;
  else
    {
      task_get_bootstrap_port (mach_task_self (), &bootstrap);
      if (bootstrap == MACH_PORT_NULL)
	error (2, 0, "Must be started as a translator");
    }

  err = diskfs_init_diskfs ();
  if (err)
    error (4, err, "init");
  
  err = store_parsed_open (store_parsed, STORE_READONLY, &store);
  if (err)
    error (3, err, "%s", diskfs_disk_name);
   
  diskfs_readonly = diskfs_hard_readonly = 1;

  create_disk_pager ();
  
  diskfs_spawn_first_thread ();
  
  read_sblock ();
  
  fetch_root ();
  
  diskfs_startup_diskfs (bootstrap, 0);
  
  cthread_exit (0);
  
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
  /* We should never get here because we define our own diskfs_set_readonly
     above. */
  abort ();
}
