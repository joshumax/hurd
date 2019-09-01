/* main.c - FAT filesystem.

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

#include <string.h>
#include <error.h>
#include <argp.h>
#include <argz.h>
#include <limits.h>

#include <version.h>
#include "fatfs.h"
#include "libdiskfs/fsys_S.h"

struct node *diskfs_root_node;

struct store *store = 0;
struct store_parsed *store_parsed = 0;
char *diskfs_disk_name = 0;

char *diskfs_server_name = "fatfs";
char *diskfs_server_version = HURD_VERSION;
char *diskfs_extra_version = "GNU Hurd";
int diskfs_synchronous = 0;

int diskfs_link_max = 1;
int diskfs_name_max = FAT_NAME_MAX;
int diskfs_maxsymlinks = 8;     /* XXX */

/* Handy source of zeroes.  */
vm_address_t zerocluster;

struct dirrect dr_root_node;

/* The UID and GID for all files in the filesystem.  */
uid_t default_fs_uid;
gid_t default_fs_gid;
uid_t fs_uid;
gid_t fs_gid;

/* fatfs specific options.  */
static const struct argp_option options[] =
  {
    { "uid", 'U', "uid", 0, "Default uid for files" },
    { "gid", 'G', "gid", 0, "Default gid for files" },
    { 0 }
  };

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  switch (key)
    {
    case 'U':
      if (arg)
	fs_uid = atoi (arg);
      refresh_node_stats ();
      break;
    case 'G':
      if (arg)
	fs_gid = atoi (arg);
      refresh_node_stats ();
      break;
    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input;
      break;
    case ARGP_KEY_SUCCESS:
      break;
    default:
      return ARGP_ERR_UNKNOWN;
    }
  
  return 0;
}

/* Add our startup arguments to the standard diskfs set.  */
static const struct argp_child startup_children[] =
 { { &diskfs_store_startup_argp }, { 0 } };
static struct argp startup_argp =
  { options, parse_opt, 0, 0, startup_children };

/* Similarly at runtime.  */
static const struct argp_child runtime_children[] =
 { { &diskfs_std_runtime_argp }, { 0 } };
static struct argp runtime_argp =
  { options, parse_opt, 0, 0, runtime_children };

struct argp *diskfs_runtime_argp = (struct argp *) &runtime_argp;


/* Override the standard diskfs routine so we can add our own
   output.  */
error_t
diskfs_append_args (char **argz, unsigned *argz_len)
{
  error_t err;
  char buf[100];

  /* Get the standard things.  */
  err = diskfs_append_std_options (argz, argz_len);

  if (!err && fs_uid != default_fs_uid)
    {
      snprintf (buf, sizeof buf, "--uid=%d", fs_uid);
      err = argz_add (argz, argz_len, buf);
    }

  if (!err && fs_gid != default_fs_gid)
    {
      snprintf (buf, sizeof buf, "--gid=%d", fs_gid);
      err = argz_add (argz, argz_len, buf);
    }

  if (! err)
    err = store_parsed_append_args (store_parsed, argz, argz_len);

  return err;
}


/* Fetch the root node.  */
static void
fetch_root ()
{
  error_t err;
  ino_t inum;
  struct lookup_context ctx;

  memset (&dr_root_node, 0, sizeof(struct dirrect));

  /* Fill root directory entry.  XXX Should partially be in fat.c  */
  dr_root_node.attribute = FAT_DIR_ATTR_DIR;
  if (fat_type == FAT32)
    {
      /* FAT12/16: There is no such thing as a start cluster, because
	 the whole root dir is in a special region after the FAT.  The
	 start cluster of the root node is undefined.  */
      dr_root_node.first_cluster_high[1]
	= sblock->compat.fat32.root_cluster[3];
      dr_root_node.first_cluster_high[0]
	= sblock->compat.fat32.root_cluster[2];
      dr_root_node.first_cluster_low[1] = sblock->compat.fat32.root_cluster[1];
      dr_root_node.first_cluster_low[0] = sblock->compat.fat32.root_cluster[0];
    }

  /* Determine size of the directory (different for fat12/16 vs 32).  */
  switch (fat_type)
    {
    case FAT12:
    case FAT16:
      write_dword(dr_root_node.file_size, nr_of_root_dir_sectors
		  << log2_bytes_per_sector);
      break;

    case FAT32:
      {
	/* Extend the cluster chain of the root directory and calculate
	   file_size based on that.  */
	cluster_t rootdir;
	int cs = 0;

	rootdir = (cluster_t) *sblock->compat.fat32.root_cluster;
	while (rootdir != FAT_EOC)
	  {
	    fat_get_next_cluster (rootdir, &rootdir);
	    cs++;
	  }
	write_dword (dr_root_node.file_size, cs << log2_bytes_per_cluster);
      }
      break;

    default:
      assert_backtrace (!"don't know how to set size of root dir");
    };

  /* The magic vi_key {0, 1} for the root directory is distinguished
     from the vi_zero_key (in the dir_offset value) as well as all
     normal virtual inode keys (in the dir_inode value).  Enter the
     disknode into the inode table.  */
  err = vi_new ((struct vi_key) {0, 1}, &inum, &ctx.inode);
  assert_perror_backtrace (err);

  /* Allocate a node for the root directory disknode in
     diskfs_root_node.  */
  if (!err)
    err = diskfs_cached_lookup_context (inum, &diskfs_root_node, &ctx);

  assert_perror_backtrace (err);

  pthread_mutex_unlock (&diskfs_root_node->lock);
}


int
main (int argc, char **argv)
{
  mach_port_t bootstrap;

  default_fs_uid = getuid ();
  default_fs_gid = getgid ();
  fs_uid = default_fs_uid;
  fs_gid = default_fs_gid;

  /* This filesystem is not capable of writing yet.  */
  diskfs_readonly = 1;
  diskfs_hard_readonly = 1;

  /* Initialize the diskfs library, parse arguments, and open the
     store.  This starts the first diskfs thread for us.  */
  store = diskfs_init_main (&startup_argp, argc, argv, &store_parsed,
			    &bootstrap);

  fat_read_sblock ();

  create_fat_pager ();

  zerocluster = (vm_address_t) mmap (0, bytes_per_cluster, PROT_READ|PROT_WRITE,
				     MAP_ANON, 0, 0);

  fetch_root ();

  diskfs_startup_diskfs (bootstrap, 0);

  pthread_exit (NULL);

  return 0;
}


/* Nothing to do for read-only medium.  */
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
  /* We should never get here because we set diskfs_hard_readonly above.  */
  abort ();
}

/* FIXME: libdiskfs doesn't lock the parent dir when looking up a node
   for fsys_getfile, so we disable NFS.  */
error_t
diskfs_S_fsys_getfile (struct diskfs_control *pt,
                      mach_port_t reply, mach_msg_type_name_t reply_type,
                      uid_t *uids, mach_msg_type_number_t nuids,
                      gid_t *gids, mach_msg_type_number_t ngids,
                      data_t handle, mach_msg_type_number_t handle_len,
                      mach_port_t *file, mach_msg_type_name_t *file_type)
{
  return EOPNOTSUPP;
}
