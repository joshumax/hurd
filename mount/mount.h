/* Private definitions for the mount server

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <hurd/ports.h>

#include "mount_types.h"

/* A particular mount of a filesystem.  */
struct mount
{
  struct port_info pi;		/* libports header.  */

  /* The filesystem this a mount of.  */
  struct mount_fsys *fsys;

  /* Modes of this mount, from the set MOUNT_READ, MOUNT_WRITE, &c.  */
  int mode;

  /* The filesystem control port for its translator.  This may
     MACH_PORT_NULL, in which case it's not actually a filesystem (maybe it's
     a fsck or something).  */
  fsys_t translator;

  /* A timestamp associated with TRANSLATOR, used to validate state if the
     mount server is restarted.  */
  struct timespec timestamp;

  /* The place in the filesystem where this filesystem is mounted, using the
     mount server's root node.  */
  char *mount_point;

  /* The next mount of this filesystem.  */
  struct mount *next;
};

struct mount_fsys
{
  /* A string identifying the `backing store' associated with this
     filesystem, who's interpretation depends on KEY_CLASS:
       MOUNT_KEY_UNKNOWN	-- no interpretation, just a string
       MOUNT_KEY_FILE		-- KEY is a filename
       MOUNT_KEY_DEVICE		-- KEY is a (mach) device name
     No two filesystems can have the same KEY and KEY_CLASS.  */
  char *key;
  enum mount_key_class key_class;

  /* In what condition we think the filesystem is.  */
  enum mount_state state;

  /* How this file system deals with multiple mount requests.  */
  enum mount_excl excl;

  /* Active mounts of this filesystem.  */
  struct mount *mounts;
};
