/* Exported types for the mount server

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

/* Modes a particular mount may have.  */
#define MOUNT_READ  0x1		/* We only read. */
#define MOUNT_WRITE 0x2		/* We write too. */
#define MOUNT_FORCE 0x4		/* Used to try forcing a writable mount. */

/* The condition we think a file system is in (this refers to the permanent
   storage from which it is backed, not an active file system).  */
enum mount_state
{
  MOUNT_STATE_UNKNOWN,		/* Just so.  When this value is passed to a
				   mount routine that takes a state argument,
				   it usually means `keep the last known
				   state'. */
  MOUNT_STATE_SUSPICIOUS,	/* We think it may have been compromised. */
  MOUNT_STATE_DIRTY,		/* May be transiently inconsistent.  This is
				   the normal state for an active writable
				   file system.  */
  MOUNT_STATE_CLEAN		/* Peachy. */
};
typedef enum mount_state mount_state_t;	/* For mig's use. */

/* How the key associated with a filesystem is interpreted.  */
enum mount_key_class
{
  MOUNT_KEY_UNKNOWN,		/*  */
  MOUNT_KEY_FILE,		/* A file (including e.g., `/dev/rsd0a').  */
  MOUNT_KEY_DEVICE		/* A mach device name.  */
};
typedef enum mount_key_class mount_key_class_t;	/* For mig's use. */

/* Types of multiple mounts of a single filesystem that are allowed.  */
enum mount_excl
{
  MOUNT_EXCL_NONE,		/* Both multiple readers and multiple writers
				   allowed; some external private protocol
				   must be used to main consistency.  */
  MOUNT_EXCL_WRITE,		/* Multiple readers allowed, but if there's a
				   writer, it must be the only mount.  */
  MOUNT_EXCL_RDWR		/* Only a single mount of any type allowed.  */
};
typedef enum mount_excl mount_excl_t; /* For mig */
