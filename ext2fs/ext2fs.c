/* 
   Copyright (C) 1994, 1995 Free Software Foundation

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


#include <stdarg.h>
#include <stdio.h>
#include <device/device.h>
#include <hurd/startup.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include "ext2fs.h"

char *ext2fs_version = "0.0 pre-alpha";

/* Parse the arguments for ext2fs when started as a translator. */
char *
trans_parse_args (int argc, char **argv)
{
  char *devname;
  /* When started as a translator, we are called with
     the device name and an optional argument -r, which
     signifies read-only. */
  if (argc < 2 || argc > 3)
    exit (1);

  if (argc == 2)
    devname = argv[1];

  else if (argc == 3)
    {
      if (argv[1][0] == '-' && argv[1][1] == 'r')
	{
	  diskfs_readonly = 1;
	  devname = argv[2];
	}
      else if (argv[2][0] == '-' && argv[2][1] == 'r')
	{
	  diskfs_readonly = 1;
	  devname = argv[1];
	}
      else
	exit (1);
    }

  return devname;
}

/* ---------------------------------------------------------------- */

struct node *diskfs_root_node;

/* Set diskfs_root_node to the root inode. */
static void
warp_root (void)
{
  error_t err;
  err = iget (2, &diskfs_root_node);
  assert (!err);
  mutex_unlock (&diskfs_root_node->lock);
}

/* ---------------------------------------------------------------- */

/* XXX */
struct mutex printf_lock;
int printf (const char *fmt, ...)
{
  va_list arg;
  int done;
  va_start (arg, fmt);
  mutex_lock (&printf_lock);
  done = vprintf (fmt, arg);
  mutex_unlock (&printf_lock);
  va_end (arg);
  return done;
}

static char error_buf[1024];

void ext2_error (const char * function, const char * fmt, ...)
{
  va_list args;

#if 0
  if (!(sb->s_flags & MS_RDONLY))
    {
      sb->u.ext2_sb.s_mount_state |= EXT2_ERROR_FS;
      sb->u.ext2_sb.s_es->s_state |= EXT2_ERROR_FS;
      mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
      sb->s_dirt = 1;
    }
#endif

  va_start (args, fmt);
  vfprintf (error_buf, fmt, args);
  va_end (args);

  mutex_lock(&printf_lock);

  fprintf (stderr, "ext2fs: %s: %s: %s\n", devname, function, error_buf);

#if 0
  if (test_opt (sb, ERRORS_PANIC) ||
      (sb->u.ext2_sb.s_es->s_errors == EXT2_ERRORS_PANIC &&
       !test_opt (sb, ERRORS_CONT) && !test_opt (sb, ERRORS_RO)))
    /* panic */
    {
      fprintf(stderr, "ext2fs: %s: exiting\n", sb->s_devname);
      exit(99);
    }

  if (test_opt (sb, ERRORS_RO) ||
      (sb->u.ext2_sb.s_es->s_errors == EXT2_ERRORS_RO &&
       !test_opt (sb, ERRORS_CONT) && !test_opt (sb, ERRORS_PANIC))) {
    fprintf (stderr, "ext2fs: %s: Remounting filesystem read-only\n",
	     sb->s_devname);
    sb->s_flags |= MS_RDONLY;
  }
#endif

  mutex_unlock(&printf_lock);
}

void ext2_panic (const char * function, const char * fmt, ...)
{
  va_list args;

#if 0
  if (!(sb->s_flags & MS_RDONLY))
    {
      sb->u.ext2_sb.s_mount_state |= EXT2_ERROR_FS;
      sb->u.ext2_sb.s_es->s_state |= EXT2_ERROR_FS;
      mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
      sb->s_dirt = 1;
    }
#endif

  va_start (args, fmt);
  vsprintf (error_buf, fmt, args);
  va_end (args);

  mutex_lock(&printf_lock);
  fprintf(stderr, "ext2fs: %s: panic: %s: %s\n", devname, function, error_buf);
  mutex_unlock(&printf_lock);

  exit (0);
}

void ext2_warning (const char * function, const char * fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  vsprintf (error_buf, fmt, args);
  va_end (args);

  mutex_lock(&printf_lock);
  fprintf (stderr, "ext2fs: %s: %s: %s\n", devname, function, error_buf);
  mutex_unlock(&printf_lock);
}

/* ---------------------------------------------------------------- */

int diskfs_readonly;

void
main (int argc, char **argv)
{
  char *devname;
  mach_port_t bootstrap;
  error_t err;
  int sizes[DEV_GET_SIZE_COUNT];
  struct super_block sb;
  u_int sizescnt = 2;

  mutex_init (&printf_lock);	/* XXX */

  if (getpid () > 0)
    {
      /* We are in a normal Hurd universe, started as a translator.  */

      devname = trans_parse_args (argc, argv);

      {
	/* XXX let us see errors */
	int fd = open ("/dev/console", O_RDWR);
	assert (fd == 0);
	fd = dup (0);
	assert (fd == 1);
	fd = dup (1);
	assert (fd == 2);
      }
    }
  else
    {
      /* We are the bootstrap filesystem.  */
      devname = diskfs_parse_bootargs (argc, argv);
      compat_mode = COMPAT_GNU;
    }
  
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  
  /* Initialize the diskfs library.  This must come before
     any other diskfs call.  */
  diskfs_init_diskfs ();
  
  do
    {
      char *line = 0;
      size_t linesz = 0;
      ssize_t len;
      
      err = device_open (diskfs_master_device, 
			 (diskfs_readonly ? 0 : D_WRITE) | D_READ,
			 devname, &ext2fs_device);
      if (err == D_NO_SUCH_DEVICE && getpid () <= 0)
	{
	  /* Prompt the user to give us another name rather
	     than just crashing */
	  printf ("Cannot open device %s\n", devname);
	  printf ("Open instead: ");
	  fflush (stdout);
	  len = getline (&line, &linesz, stdin);
	  if (len > 2)
	    devname = line;
	}
    }
  while (err && err == D_NO_SUCH_DEVICE && getpid () <= 0);
	  
  if (err)
    error(1, errno, "%s", devname);

  /* Check to make sure device sector size is reasonable. */
  err = device_get_status (ext2fs_device, DEV_GET_SIZE, sizes, &sizescnt);
  assert (sizescnt == DEV_GET_SIZE_COUNT);
  if (sizes[DEV_GET_SIZE_RECORD_SIZE] != DEV_BSIZE)
    error(1, 0, "Bad device record size %d (should be %d)\n",
	  sizes[DEV_GET_SIZE_RECORD_SIZE], DEV_BSIZE);
  
  diskpagersize = sizes[DEV_GET_SIZE_DEVICE_SIZE];
  assert (diskpagersize >= SBSIZE + SBOFF);

  /* Map the entire disk. */
  create_disk_pager ();

  /* Start the first request thread, to handle RPCs and page requests. */
  diskfs_spawn_first_thread ();

  err = vm_map (mach_task_self (), (vm_address_t *)&disk_image,
		diskpagersize, 0, 1, diskpagerport, 0, 0, 
		VM_PROT_READ | (diskfs_readonly ? 0 : VM_PROT_WRITE),
		VM_PROT_READ | (diskfs_readonly ? 0 : VM_PROT_WRITE), 
		VM_INHERIT_NONE);
  assert (!err);

  get_hypermetadata();

  if (diskpagersize < sblock->s_blocks_count * block_size)
    ext2_panic("Disk size (%d) too small (superblock says we need %ld)",
	       sizes[DEV_GET_SIZE_DEVICE_SIZE],
	       sblock->s_blocks_count * block_size);

  vm_allocate (mach_task_self (), &zeroblock, block_size, 1);

  inode_init ();

  /* Find our root node.  */
  warp_root ();

  /* Now that we are all set up to handle requests, and diskfs_root_node is
     set properly, it is safe to export our fsys control port to the
     outside world.  */
  (void) diskfs_startup_diskfs (bootstrap);

  if (bootstrap == MACH_PORT_NULL)
    /* We are the bootstrap filesystem; do special boot-time setup.  */
    diskfs_start_bootstrap (argv);
  
  /* Now become a generic request thread.  */
  diskfs_main_request_loop ();
}


void
diskfs_init_completed ()
{
  mach_port_t proc, startup;
  error_t err;

  proc = getproc ();
  proc_register_version (proc, diskfs_host_priv, "ext2fs", HURD_RELEASE,
			 ext2fs_version);
  err = proc_getmsgport (proc, 1, &startup);
  if (!err)
    {
      startup_essential_task (startup, mach_task_self (), MACH_PORT_NULL,
			      "ext2fs", diskfs_host_priv);
      mach_port_deallocate (mach_task_self (), startup);
    }
  mach_port_deallocate (mach_task_self (), proc);
}
