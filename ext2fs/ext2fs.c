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
#include <getopt.h>
#include "ext2fs.h"
#include "error.h"

/* ---------------------------------------------------------------- */

int diskfs_link_max = EXT2_LINK_MAX;
int diskfs_maxsymlinks = 8;
int diskfs_shortcut_symlink = 1;
int diskfs_shortcut_chrdev = 1;
int diskfs_shortcut_blkdev = 1;
int diskfs_shortcut_fifo = 1;
int diskfs_shortcut_ifsock = 1;

char *diskfs_server_name = "ext2fs";
int diskfs_major_version = 0;
int diskfs_minor_version = 0;
int diskfs_edit_version = 0;

int diskfs_synchronous = 0;
int diskfs_readonly = 0;

/* ---------------------------------------------------------------- */

struct node *diskfs_root_node;

/* Set diskfs_root_node to the root inode. */
static void
warp_root (void)
{
  error_t err;
  err = iget (EXT2_ROOT_INO, &diskfs_root_node);
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

  va_start (args, fmt);
  vsprintf (error_buf, fmt, args);
  va_end (args);

  mutex_lock(&printf_lock);
  fprintf (stderr, "ext2fs: %s: %s: %s\n", device_name, function, error_buf);
  mutex_unlock(&printf_lock);
}

void ext2_panic (const char * function, const char * fmt, ...)
{
  va_list args;

  va_start (args, fmt);
  vsprintf (error_buf, fmt, args);
  va_end (args);

  mutex_lock(&printf_lock);
  fprintf(stderr, "ext2fs: %s: panic: %s: %s\n", device_name, function, error_buf);
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
  fprintf (stderr, "ext2fs: %s: %s: %s\n", device_name, function, error_buf);
  mutex_unlock(&printf_lock);
}

/* ---------------------------------------------------------------- */

#define USAGE "Usage: %s [OPTION...] DEVICE\n"

static void
usage(int status)
{
  if (status != 0)
    fprintf(stderr, "Try `%s --help' for more information.\n",
	    program_invocation_name);
  else
    {
      printf(USAGE, program_invocation_name);
      printf("\
\n\
  -r, --readonly             disable writing to DEVICE\n\
  -s, --synchronous          write data out immediately\n\
      --help                 display this help and exit\n\
      --version              output version information and exit\n\
");
    }
  exit (status);
}

#define SHORT_OPTIONS "rs?"

static struct option options[] =
{
  {"readonly", no_argument, 0, 'r'},
  {"synchronous", no_argument, 0, 's'},
  {"help", no_argument, 0, '?'},
  {"version", no_argument, 0, 'V'},
  {0, 0, 0, 0}
};


/* ---------------------------------------------------------------- */

int check_string = 1;

void
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  int sizes[DEV_GET_SIZE_COUNT];
  unsigned sizescnt = 2;

  mutex_init (&printf_lock);	/* XXX */

  if (getpid () > 0)
    {
      int opt;
      int fd = open ("/dev/console", O_RDWR);

      /* Make errors go somewhere reasonable.  */
      while (fd >= 0 && fd < 2)
	fd = dup(fd);
      if (fd > 2)
	close (fd);

      while ((opt = getopt_long(argc, argv, SHORT_OPTIONS, options, 0)) != EOF)
	switch (opt)
	  {
	  case 'r':
	    diskfs_readonly = 1; break;
	  case 's':
	    diskfs_synchronous = 1; break;
	  case 'V':
	    printf("%s %d.%d.%d\n", diskfs_server_name, diskfs_major_version,
		   diskfs_minor_version, diskfs_edit_version);
	    exit(0);
	  case '?':
	    usage(0);
	  default:
	    usage(1);
	  }

      if (argc - optind != 1)
	{
	  fprintf (stderr, USAGE, program_invocation_name);
	  usage (1);
	}

      device_name = argv[optind];
    }
  else
    /* We are the bootstrap filesystem.  */
    device_name = diskfs_parse_bootargs (argc, argv);
  
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
			 device_name, &device_port);
      if (err == D_NO_SUCH_DEVICE && getpid () <= 0)
	{
	  /* Prompt the user to give us another name rather
	     than just crashing */
	  printf ("Cannot open device %s\n", device_name);
	  printf ("Open instead: ");
	  fflush (stdout);
	  len = getline (&line, &linesz, stdin);
	  if (len > 2)
	    device_name = line;
	}
    }
  while (err && err == D_NO_SUCH_DEVICE && getpid () <= 0);
	  
  if (err)
    error(1, errno, "%s", device_name);

  /* Check to make sure device sector size is reasonable. */
  err = device_get_status (device_port, DEV_GET_SIZE, sizes, &sizescnt);
  assert (sizescnt == DEV_GET_SIZE_COUNT);

  device_block_size = sizes[DEV_GET_SIZE_RECORD_SIZE];
  device_size = sizes[DEV_GET_SIZE_DEVICE_SIZE];
  assert (device_size >= SBLOCK_OFFS + SBLOCK_SIZE);

  /* Map the entire disk. */
  create_disk_pager ();

  /* Start the first request thread, to handle RPCs and page requests. */
  diskfs_spawn_first_thread ();

  err = vm_map (mach_task_self (), (vm_address_t *)&disk_image,
		device_size, 0, 1, disk_pager_port, 0, 0, 
		VM_PROT_READ | (diskfs_readonly ? 0 : VM_PROT_WRITE),
		VM_PROT_READ | (diskfs_readonly ? 0 : VM_PROT_WRITE), 
		VM_INHERIT_NONE);
  if (err)
    error (2, err, "vm_map");

  diskfs_register_memory_fault_area (disk_pager->p, 0, disk_image, device_size);

  pokel_init (&global_pokel, disk_pager->p, disk_image);

  err = get_hypermetadata();
  if (err)
    error (3, err, "get_hypermetadata");

  if (device_size < sblock->s_blocks_count * block_size)
    ext2_panic("main",
	       "Disk size (%d) too small (superblock says we need %ld)",
	       sizes[DEV_GET_SIZE_DEVICE_SIZE],
	       sblock->s_blocks_count * block_size);

  /* A handy source of page-aligned zeros.  */
  vm_allocate (mach_task_self (), &zeroblock, block_size, 1);

  if (!diskfs_readonly && (sblock->s_state & EXT2_VALID_FS))
    {
      sblock->s_state &= ~EXT2_VALID_FS;
      sync_super_block ();
    }

  inode_init ();

  /* Find our root node.  */
  warp_root ();

  /* Now that we are all set up to handle requests, and diskfs_root_node is
     set properly, it is safe to export our fsys control port to the
     outside world.  */
  (void) diskfs_startup_diskfs (bootstrap);

#if 0
  if (bootstrap == MACH_PORT_NULL)
    /* We are the bootstrap filesystem; do special boot-time setup.  */
    diskfs_start_bootstrap (argv);
#endif
  
  /* Now become a generic request thread.  */
  diskfs_main_request_loop ();
}


void
diskfs_init_completed ()
{
  string_t version;
  mach_port_t proc, startup;
  error_t err;

  sprintf(version, "%s %d.%d.%d",
	  diskfs_server_name, diskfs_major_version,
	  diskfs_minor_version, diskfs_edit_version);

  proc = getproc ();
  proc_register_version (proc, diskfs_host_priv, diskfs_server_name,
			 HURD_RELEASE, version);
  err = proc_getmsgport (proc, 1, &startup);
  if (!err)
    {
      startup_essential_task (startup, mach_task_self (), MACH_PORT_NULL,
			      diskfs_server_name, diskfs_host_priv);
      mach_port_deallocate (mach_task_self (), startup);
    }
  mach_port_deallocate (mach_task_self (), proc);
}

/* ---------------------------------------------------------------- */

static spin_lock_t free_page_bufs_lock = SPIN_LOCK_INITIALIZER;
static vm_address_t free_page_bufs = 0;

/* Returns a single page page-aligned buffer.  */
vm_address_t get_page_buf ()
{
  vm_address_t buf;

  spin_lock (&free_page_bufs_lock);

  buf = free_page_bufs;
  if (buf == 0)
    {
      spin_unlock (&free_page_bufs_lock);
      vm_allocate (mach_task_self (), &buf, vm_page_size, 1);
    }
  else
    {
      free_page_bufs = *(vm_address_t *)buf;
      spin_unlock (&free_page_bufs_lock);
    }

  return buf;
}

/* Frees a block returned by get_page_buf.  */
void free_page_buf (vm_address_t buf)
{
  spin_lock (&free_page_bufs_lock);
  *(vm_address_t *)buf = free_page_bufs;
  free_page_bufs = buf;
  spin_unlock (&free_page_bufs_lock);
}
