/* 
   Copyright (C) 1994 Free Software Foundation

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
#include <stdarg.h>
#include <stdio.h>
#include <device/device.h>
#include <hurd/startup.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

char *ufs_version = "0.0 pre-alpha";


/* Parse the arguments for ufs when started as a translator. */
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

int diskfs_readonly;

void
main (int argc, char **argv)
{
  char *devname;
  mach_port_t bootstrap;
  error_t err;
  int sizes[DEV_GET_SIZE_COUNT];
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
  
  err = device_open (diskfs_master_device, 
		     (diskfs_readonly ? 0 : D_WRITE) | D_READ,
		     devname, &ufs_device);
  assert (!err);

  /* Check to make sure device sector size is reasonable. */
  err = device_get_status (ufs_device, DEV_GET_SIZE, sizes, &sizescnt);
  assert (sizescnt == DEV_GET_SIZE_COUNT);
  if (sizes[DEV_GET_SIZE_RECORD_SIZE] != DEV_BSIZE)
    {
      fprintf (stderr, "Bad device record size %d (should be %d)\n",
	       sizes[DEV_GET_SIZE_RECORD_SIZE], DEV_BSIZE);
      exit (1);
    }
  
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

  get_hypermetadata ();

  if (diskpagersize < sblock->fs_size * sblock->fs_fsize)
    {
      fprintf (stderr, 
	       "Disk size (%d) less than necessary "
	       "(superblock says we need %ld)\n",
	       sizes[DEV_GET_SIZE_DEVICE_SIZE],
	       sblock->fs_size * sblock->fs_fsize);
      exit (1);
    }

  vm_allocate (mach_task_self (), &zeroblock, sblock->fs_bsize, 1);

  /* If the filesystem has new features in it, don't pay attention to
     the user's request not to use them. */
  if ((sblock->fs_inodefmt == FS_44INODEFMT
       || direct_symlink_extension)
      && compat_mode == COMPAT_BSD42)
    /* XXX should syslog to this effect */
    compat_mode = COMPAT_BSD44;

  if (!diskfs_readonly)
    {
      sblock->fs_clean = 0;
      strcpy (sblock->fs_fsmnt, "Hurd /"); /* XXX */
      sblock_dirty = 1;
      diskfs_set_hypermetadata (1, 0);
    }

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
  proc_register_version (proc, diskfs_host_priv, "ufs", HURD_RELEASE,
			 ufs_version);
  err = proc_getmsgport (proc, 1, &startup);
  if (!err)
    {
      startup_essential_task (startup, mach_task_self (), MACH_PORT_NULL,
			      "ufs", diskfs_host_priv);
      mach_port_deallocate (mach_task_self (), startup);
    }
  mach_port_deallocate (mach_task_self (), proc);
}

  
