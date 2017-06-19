/* Private declarations for fileserver library

   Copyright (C) 1994, 1995, 1996, 1997, 1998, 1999, 2001, 2006, 2009 Free
   Software Foundation, Inc.

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

#ifndef DISKFS_PRIV_H
#define DISKFS_PRIV_H

#include <mach.h>
#include <hurd.h>
#include <sys/mman.h>
#include <hurd/ports.h>
#include <hurd/fshelp.h>
#include <hurd/iohelp.h>
#include <hurd/port.h>
#include <assert-backtrace.h>
#include <argp.h>

#include "diskfs.h"

/* These inhibit setuid or exec. */
extern int _diskfs_nosuid, _diskfs_noexec;

/* This relaxes the requirement to set `st_atim'.  */
extern int _diskfs_noatime;

/* This enables SysV style group behaviour.  New nodes inherit the GID
   of the user creating them unless the SGID bit is set of the parent
   directory.  */
extern int _diskfs_no_inherit_dir_group;

/* This is the -C argument value.  */
extern char *_diskfs_chroot_directory;

/* If --boot-command is given, this points to the program and args.  */
extern char **_diskfs_boot_command;

/* Port cell holding a cached port to the exec server.  */
extern struct hurd_port _diskfs_exec_portcell;

volatile struct mapped_time_value *_diskfs_mtime;

extern const struct argp_option diskfs_common_options[];
/* Option keys for long-only options in diskfs_common_options.  */
#define OPT_SUID_OK	600	/* --suid-ok */
#define OPT_EXEC_OK	601	/* --exec-ok */
#define OPT_ATIME	602	/* --atime */
#define OPT_NO_INHERIT_DIR_GROUP	603	/* --no-inherit-dir-group */
#define OPT_INHERIT_DIR_GROUP		604	/* --inherit-dir-group */

/* Common value for diskfs_common_options and diskfs_default_sync_interval. */
#define DEFAULT_SYNC_INTERVAL 30
#define DEFAULT_SYNC_INTERVAL_STRING STRINGIFY(DEFAULT_SYNC_INTERVAL)
#define STRINGIFY(x) STRINGIFY_1(x)
#define STRINGIFY_1(x) #x

/* Diskfs thinks the disk is dirty if this is set. */
extern int _diskfs_diskdirty;

/* Needed for MiG. */
typedef struct protid *protid_t;
typedef struct diskfs_control *control_t;
typedef struct bootinfo *bootinfo_t;

/* Actually read or write a file.  The file size must already permit
   the requested access.  NP is the file to read/write.  DATA is a buffer
   to write from or fill on read.  OFFSET is the absolute address (-1
   not permitted here); AMT is the size of the read/write to perform;
   DIR is set for writing and clear for reading.  The inode must
   be locked.   If NOTIME is set, then don't update the access or
   modify times on the file.  */
error_t _diskfs_rdwr_internal (struct node *np, char *data, off_t offset,
			       size_t *amt, int dir, int notime);

/* Called when we have a real user environment (complete with proc
   and auth ports). */
void _diskfs_init_completed (void);

/* Called in a bootstrap filesystem only, to get the privileged ports.  */
void _diskfs_boot_privports (void);

/* Clean routine for control port. */
void _diskfs_control_clean (void *);

/* Called when the last hard reference is released.  If there are no
   links, then request soft references to be dropped.  */
void _diskfs_lastref (struct node *np);

/* Number of outstanding PT_CTL ports. */
extern int _diskfs_ncontrol_ports;

/* Lock for _diskfs_ncontrol_ports. */
extern pthread_spinlock_t _diskfs_control_lock;

/* Callback routines for active translator startup */
extern fshelp_fetch_root_callback1_t _diskfs_translator_callback1;
extern fshelp_fetch_root_callback2_t _diskfs_translator_callback2;

/* This macro locks the node associated with PROTID, and then
   evaluates the expression OPERATION; then it syncs the inode
   (without waiting) and unlocks everything, and then returns
   the value `err' (which can be set by OPERATION if desired). */
#define CHANGE_NODE_FIELD(PROTID, OPERATION)				    \
({									    \
  error_t err = 0;							    \
  struct node *np;							    \
  									    \
  if (!(PROTID))							    \
    return EOPNOTSUPP;							    \
  									    \
  if (diskfs_check_readonly ())						    \
    return EROFS;							    \
  									    \
  np = (PROTID)->po->np;						    \
  									    \
  pthread_mutex_lock (&np->lock);					    \
  (OPERATION);								    \
  if (diskfs_synchronous)						    \
    diskfs_node_update (np, 1);						    \
  pthread_mutex_unlock (&np->lock);					    \
  return err;								    \
})

/* Bits the user is permitted to set with io_*_openmodes */
#define HONORED_STATE_MODES (O_APPEND|O_ASYNC|O_FSYNC|O_NONBLOCK|O_NOATIME)

/* Bits that are turned off after open */
#define OPENONLY_STATE_MODES \
  (O_CREAT|O_EXCL|O_NOLINK|O_NOTRANS|O_NONBLOCK|O_EXLOCK|O_SHLOCK)

#endif
