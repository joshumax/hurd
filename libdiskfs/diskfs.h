/* Definitions for fileserver helper functions
   Copyright (C) 1994, 1995, 1996 Free Software Foundation, Inc.

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

#ifndef _HURD_DISKFS
#define _HURD_DISKFS

#include <argp.h>
#include <assert.h>
#include <unistd.h>
#include <rwlock.h>
#include <hurd/ports.h>
#include <hurd/fshelp.h>
#include <hurd/ioserver.h>

/* Each user port referring to a file points to one of these
   (with the aid of the ports library).  */
struct protid
{
  struct port_info pi;		/* libports info block */

  /* User identification */
  uid_t *uids, *gids;
  int nuids, ngids;

  /* Object this refers to */
  struct peropen *po;

  /* Shared memory I/O information.  */
  memory_object_t shared_object;
  struct shared_io *mapped;
};

/* One of these is created for each node opened by dir_pathtrans. */
struct peropen
{
  int filepointer;
  int lock_status;
  int refcnt;
  int openstat;
  mach_port_t dotdotport;	/* dotdot from ROOT through this peropen */
  struct node *np;
};

/* A unique one of these exists for each node currently in use (and
   possibly for some not currently in use, but with links) in the
   filesystem.  */
struct node
{
  struct node *next, **prevp;

  struct disknode *dn;

  struct stat dn_stat;
  int istranslated;

  /* Stat has been modified if one of the following four fields
     is nonzero.  Also, if one of the dn_set_?time fields is nonzero,
     the appropriate dn_stat.st_?time field needs to be updated. */
  int dn_set_ctime;
  int dn_set_atime;
  int dn_set_mtime;
  int dn_stat_dirty;

  struct mutex lock;

  int references;		/* hard references */
  int light_references;		/* light references */

  mach_port_t sockaddr;		/* address for S_IFSOCK shortcut */

  int owner;

  struct transbox transbox;

  struct lock_box userlock;

  struct conch conch;

  struct dirmod *dirmod_reqs;

  off_t allocsize;

  int cache_id;
};

/* Possibly lookup types for diskfs_lookup call */
enum lookup_type
{
  LOOKUP,
  CREATE,
  REMOVE,
  RENAME,
};

/* Pending directory modification request */
struct dirmod
{
  mach_port_t port;
  struct dirmod *next;
};


/* Special flag for diskfs_lookup. */
#define SPEC_DOTDOT 0x10000000


/* Declarations of variables the library sets.  */

extern mach_port_t diskfs_default_pager; /* send right */
extern mach_port_t diskfs_exec_ctl;	/* send right */
extern mach_port_t diskfs_exec;	/* send right */
extern auth_t diskfs_auth_server_port; /* send right */

/* The command line diskfs was started, set by the default argument parser.
   If you don't use it, set this yourself.  This is only used for bootstrap
   file systems, to give the procserver.  */
extern char **diskfs_argv;

/* When this is a bootstrap filesystem, the command line options passed from
   the kernel.  If not a bootstrap filesystem, it is 0, so it can be used to
   distinguish between the two cases.  */
extern char *diskfs_boot_flags;

/* Hold this lock while do fsys level operations.  Innocuous users can just
   hold a reader lock, and anyone who's going to do nasty things that would
   screw anyone else should hold a writer lock.  */
extern struct rwlock diskfs_fsys_lock;

extern volatile struct mapped_time_value *diskfs_mtime;

/* True iff we should do every operation synchronously.  It
   is the format-specific code's responsibility to keep allocation
   information permanently in sync if this is set; the rest will
   be done by format independent code. */
extern int diskfs_synchronous;

extern spin_lock_t diskfs_node_refcnt_lock;

extern int pager_port_type;


struct pager;

/* Port classes we manage */
extern struct port_class *diskfs_protid_class;
extern struct port_class *diskfs_control_class;
extern struct port_class *diskfs_execboot_class;
extern struct port_class *diskfs_initboot_class;

extern struct port_bucket *diskfs_port_bucket;



/* Declarations of things the user must or may define.  */

/* The user must define this type.  This should hold information
   between calls to diskfs_lookup and diskfs_dir{enter,rewrite,rename}
   so that those calls work as described below.  */
struct dirstat;

/* The user must define this variable; it should be the size in bytes
   of a struct dirstat. */
extern size_t diskfs_dirstat_size;

/* The user must define this variable; it is the maximum number of
   links to any one file.  The implementation of dir_rename does not know
   how to succeed if this is only one; on such formats you need to
   reimplement dir_rename yourself.  */
extern int diskfs_link_max;

/* The user must define this variable; it is the maximum number of
   symlinks to be traversed within a single call to dir_pathtrans.
   If this is exceeded, dir_pathtrans will return ELOOP.  */
extern int diskfs_maxsymlinks;

/* The user must define this variable and set it if the filesystem
   should be readonly.  */
extern int diskfs_readonly;

/* The user must define this variable.  Set this to be the node
   of root of the filesystem.  */
extern struct node *diskfs_root_node;

/* The user must define this variable.  Set this to the name of the
   filesystem server. */
extern char *diskfs_server_name;

/* The user must define these variables.  Set these to be the major, minor,
   and edit version numbers.  */
extern int diskfs_major_version;
extern int diskfs_minor_version;
extern int diskfs_edit_version;

/* The user may define this variable.  This should be nonzero iff the
   filesystem format supports shortcutting symlink translation.
   The library guarantees that users will not be able to read or write
   the contents of the node directly, and the library will only do so
   if the symlink hook functions return EINVAL or are not defined.
   The library knows that the dn_stat.st_size field is the length of
   the symlink, even if the hook functions are used. */
int diskfs_shortcut_symlink;

/* The user may define this variable.  This should be nonzero iff the
   filesystem format supports shortcutting chrdev translation.  */
int diskfs_shortcut_chrdev;

/* The user may define this variable.  This should be nonzero iff the
   filesystem format supports shortcutting blkdev translation.  */
int diskfs_shortcut_blkdev;

/* The user may define this variable.  This should be nonzero iff the
   filesystem format supports shortcutting fifo translation.  */
int diskfs_shortcut_fifo;

/* The user may define this variable.  This should be nonzero iff the
   filesystem format supports shortcutting ifsock translation. */
int diskfs_shortcut_ifsock;

/* The user may define this variable, otherwise it has a default value of 30.
   diskfs_set_sync_interval is called with this value when the first diskfs
   thread is started up (in diskfs_spawn_first_threa).   */
extern int diskfs_default_sync_interval;

/* The user must define this function.  Set *STATFSBUF with
   appropriate values to reflect the current state of the filesystem.  */
error_t diskfs_set_statfs (fsys_statfsbuf_t *statfsbuf);

/* The user must define this function.  Lookup in directory DP (which
   is locked) the name NAME.  TYPE will either be LOOKUP, CREATE,
   RENAME, or REMOVE.  CRED identifies the user making the call.

   If the name is found, return zero, and (if NP is nonzero) set *NP
   to point to the node for it, locked.  If the name is not found,
   return ENOENT, and (if NP is nonzero) set *NP to zero.  If NP is
   zero, then the node found must not be locked, even transitorily.
   Lookups for REMOVE and RENAME (which must often check permissions
   on the node being found) will always set NP.

   If DS is nonzero then:
     For LOOKUP: set *DS to be ignored by diskfs_drop_dirstat.
     For CREATE: on success, set *DS to be ignored by diskfs_drop_dirstat.
                 on failure, set *DS for a future call to diskfs_direnter.
     For RENAME: on success, set *DS for a future call to diskfs_dirrewrite.
                 on failure, set *DS for a future call to diskfs_direnter.
     For REMOVE: on success, set *DS for a future call to diskfs_dirremove.
                 on failure, set *DS to be ignored by diskfs_drop_dirstat.
   The caller of this function guarantees that if DS is nonzero, then
   either the appropriate call listed above or diskfs_drop_dirstat will
   be called with DS before the directory DP is unlocked, and guarantees
   that no lookup calls will be made on this directory between this
   lookup and the use (or descruction) of *DS.

   If you use the library's versions of diskfs_rename_dir,
   diskfs_clear_directory, and diskfs_init_dir, then lookups for `..'
   might have the flag SPEC_DOTDOT or'd in.  This has the following special
   meaning:
   For LOOKUP: DP should be unlocked and its reference dropped before
               returning.
   For RENAME and REMOVE: The node being found (*NP) is already held
               locked, so don't lock it or add a reference to it.
   (SPEC_DOTDOT will not be given with CREATE.)

   Return ENOENT if NAME isn't in the directory.
   Return EAGAIN if NAME refers to the `..' of this filesystem's root.
   Return EIO if appropriate.
*/
error_t diskfs_lookup_hard (struct node *dp, char *name, enum lookup_type type,
			    struct node **np, struct dirstat *ds,
			    struct protid *cred);

/* The user must define this function.  Add NP to directory DP
   under the name NAME.  This will only be called after an
   unsuccessful call to diskfs_lookup of type CREATE or RENAME; DP
   has been locked continuously since that call and DS is as that call
   set it, NP is locked.   CRED identifies the user responsible
   for the call (to be used only to validate directory growth). */
error_t diskfs_direnter_hard (struct node *dp, char *name,
			      struct node *np, struct dirstat *ds,
			      struct protid *cred);

/* The user must define this function.  This will only be called after
   a successful call to diskfs_lookup of type RENAME; this call should change
   the name found in directory DP to point to node NP instead of its previous
   referent.  DP has been locked continuously since the call to diskfs_lookup
   and DS is as that call set it; NP is locked.  */
error_t diskfs_dirrewrite_hard (struct node *dp, struct node *np,
				struct dirstat *ds);

/* The user must define this function.  This will only be called after a
   successful call to diskfs_lookup of type REMOVE; this call should remove
   the name found from the directory DS.  DP has been locked continuously since
   the call to diskfs_lookup and DS is as that call set it.  */
error_t diskfs_dirremove_hard (struct node *dp, struct dirstat *ds);

/* The user must define this function.  Initialize DS such that
   diskfs_drop_dirstat will ignore it. */
void diskfs_null_dirstat (struct dirstat *ds);

/* The user must define this function.  DS has been set by a previous
   call to diskfs_lookup on directory DP; this function is
   guaranteed to be called if none of
   diskfs_dir{enter,rename,rewrite} is, and should free any state
   retained by a struct dirstat.  DP has been locked continuously since
   the call to diskfs_lookup.  */
error_t diskfs_drop_dirstat (struct node *dp, struct dirstat *ds);

/* The user must define this function.  Return N directory entries
   starting at ENTRY from locked directory node DP.  Fill *DATA with
   the entries; that pointer currently points to *DATACNT bytes.  If
   it isn't big enough, vm_allocate into *DATA.  Set *DATACNT with the
   total size used.  Fill AMT with the number of entries copied.
   Regardless, never copy more than BUFSIZ bytes.  If BUFSIZ is 0,
   then there is no limit on *DATACNT; if N is -1, then there is no limit
   on AMT. */
error_t diskfs_get_directs (struct node *dp, int entry, int n,
			    char **data, u_int *datacnt,
			    vm_size_t bufsiz, int *amt);

/* The user must define this function.  For locked node NP (for which
   diskfs_node_translated is true) look up the name of its translator.
   Store the name into newly malloced storage; set *NAMELEN to the
   total length.  */
error_t diskfs_get_translator (struct node *np, char **namep, u_int *namelen);

/* The user must define this function.  For locked node NP, set
   the name of the translating program to be NAME, length NAMELEN.  CRED
   identifies the user responsible for the call.  */
error_t diskfs_set_translator (struct node *np, char *name, u_int namelen,
			       struct protid *cred);

/* The user must define this function.  Truncate locked node NP to be SIZE
   bytes long.  (If NP is already less than or equal to SIZE bytes
   long, do nothing.)  If this is a symlink (and diskfs_shortcut_symlink
   is set) then this should clear the symlink, even if
   diskfs_create_symlink_hook stores the link target elsewhere.  */
error_t diskfs_truncate (struct node *np, off_t size);

/* The user must define this function.  Grow the disk allocated to locked node
   NP to be at least SIZE bytes, and set NP->allocsize to the actual
   allocated size.  (If the allocated size is already SIZE bytes, do
   nothing.)  CRED identifies the user responsible for the call.  */
error_t diskfs_grow (struct node *np, off_t size, struct protid *cred);

/* The user must define this function.  Write to disk (synchronously
   iff WAIT is nonzero) from format-specific buffers any non-paged
   metadata.  If CLEAN is nonzero, then after this is written the
   filesystem will be absolutely clean, and the non-paged metadata can
   so indicate.  */
void diskfs_set_hypermetadata (int wait, int clean);

/* The user must define this function.  Allocate a new node to be of
   mode MODE in locked directory DP (don't actually set the mode or
   modify the dir, that will be done by the caller); the user
   responsible for the request can be identified with CRED.  Set *NP
   to be the newly allocated node.  */
error_t diskfs_alloc_node (struct node *dp, mode_t mode, struct node **np);

/* Free node NP; the on disk copy has already been synced with
   diskfs_node_update (where NP->dn_stat.st_mode was 0).  It's
   mode used to be MODE.  */
void diskfs_free_node (struct node *np, mode_t mode);

/* Node NP has no more references; free local state, including *NP
   if it isn't to be retained.  diskfs_node_refcnt_lock is held. */
void diskfs_node_norefs (struct node *np);

/* The user must define this function.  Node NP has some light
   references, but has just lost its last hard references.  Take steps
   so that if any light references can be freed, they are.  NP is locked
   as is the pager refcount lock.  This function will be called after
   diskfs_lost_hardrefs.  */
void diskfs_try_dropping_softrefs (struct node *np);

/* The user must define this funcction.  Node NP has some light
   references but has just lost its last hard reference.  NP is locked. */
void diskfs_lost_hardrefs (struct node *np);

/* The user must define this function.  Node NP has just acquired
   a hard reference where it had none previously.  It is thus now
   OK again to have light references without real users.  NP is
   locked. */
void diskfs_new_hardrefs (struct node *np);

/* The user must define this function.  Return non-zero if locked
   directory DP is empty.  If the user does not redefine
   diskfs_clear_directory and diskfs_init_directory, then `empty'
   means `possesses entries labelled . and .. only'.  CRED
   identifies the user making the call (if this user can't search
   the directory, then this routine should fail). */
int diskfs_dirempty (struct node *dp, struct protid *cred);

/* The user must define this function.  Sync the info in NP->dn_stat
   and any associated format-specific information to disk.  If WAIT is true,
   then return only after the physicial media has been completely updated. */
void diskfs_write_disknode (struct node *np, int wait);

/* The user must define this function.  Sync the file contents and all
   associated meta data of file NP to disk (generally this will involve
   calling diskfs_node_update for much of the metadata).  If WAIT is true,
   then return only after the physical media has been completely updated.  */
void diskfs_file_update (struct node *np, int wait);

/* The user must define this function.  For each active node, call
   FUN.  The node is to be locked around the call to FUN.  If FUN
   returns non-zero for any node, then immediately stop, and return
   that value. */
error_t diskfs_node_iterate (error_t (*fun)(struct node *));

/* The user must define this function.  Sync all the pagers and any
   data belonging on disk except for the hypermetadata.  If WAIT is true,
   then return only after the physicial media has been completely updated. */
void diskfs_sync_everything (int wait);

/* Shutdown all pagers; this is done when the filesystem is exiting and is
   irreversable.  */
void diskfs_shutdown_pager ();

/* The user must define this function.  Return a memory object port (send
   right) for the file contents of NP.  PROT is the maximum allowable
   access.  */
mach_port_t diskfs_get_filemap (struct node *np, vm_prot_t prot);

/* The user must define this function.  Return true if there are pager
   ports exported that might be in use by users.  If this returns false, then
   further pager creation is also blocked.  */
int diskfs_pager_users ();

/* Return the bitwise or of the maximum prot parameter (the second arg to
   diskfs_get_filemap) for all active user pagers. */
vm_prot_t diskfs_max_user_pager_prot ();

/* The user must define this function.  Return a `struct pager *' suitable
   for use as an argument to diskfs_register_memory_fault_area that
   refers to the pager returned by diskfs_get_filemap for node NP.
   NP is locked.  */
struct pager *diskfs_get_filemap_pager_struct (struct node *np);

/* The user may define this function.  It is called when the disk has been
   changed from read-only to read-write mode or vice-versa.  READONLY is the
   new state (which is also reflected in DISKFS_READONLY).  This function is
   also called during initial startup if the filesystem is to be writable.  */
void diskfs_readonly_changed (int readonly);

/* The user must define this function.  It must invalidate all cached global
   state, and re-read it as necessary from disk, without writing anything.
   It is always called with DISKFS_READONLY true.  diskfs_node_reload is
   subsequently called on all active nodes, so this call needn't re-read any
   node-specific data.  */
error_t diskfs_reload_global_state ();

/* The user must define this function.  It must re-read all data specific to
   NODE from disk, without writing anything.  It is always called with
   DISKFS_READONLY true.  */
error_t diskfs_node_reload (struct node *node);

/* The user may define this function, in which case it is called when the the
   filesystem receives a set-options request.  ARGC and ARGV are the
   arguments given, and STANDARD_ARGP is a pointer to a struct argp
   containing the info necessary to parse `standard' diskfs runtime options.
   The user may chain this onto the end of his own argp structure and call
   argp_parse, or ignore it completely (or indeed, just call argp_parse on it
   -- which is the behavior of the default implementation of this function.
   EINVAL is returned if an unknown option is encountered.  */
error_t diskfs_parse_runtime_options (int argc, char **argv,
				      const struct argp *standard_argp);

/* If this function is nonzero (and diskfs_shortcut_symlink is set) it
   is called to set a symlink.  If it returns EINVAL or isn't set,
   then the normal method (writing the contents into the file data) is
   used.  If it returns any other error, it is returned to the user.  */
error_t (*diskfs_create_symlink_hook)(struct node *np, char *target);

/* If this function is nonzero (and diskfs_shortcut_symlink is set) it
   is called to read the contents of a symlink.  If it returns EINVAL or
   isn't set, then the normal method (reading from the file data) is
   used.  If it returns any other error, it is returned to the user. */
error_t (*diskfs_read_symlink_hook)(struct node *np, char *target);

/* The library exports the following functions for general use */

/* Returns the name and a send right for the mach device on which the file
   NAME is stored, and returns it in DEV_NAME (which is malloced) and PORT.
   Other values returned are START, the first valid offset, SIZE, the the
   number of blocks after START, and BLOCK_SIZE, the units in which the
   device is addressed.

   The device is opened for reading, and if the diskfs global variable
   DISKFS_READONLY is false, writing.  */
error_t diskfs_get_file_device (char *name,
				char **dev_name, mach_port_t *port,
				off_t *start, off_t *size, size_t *block_size);

/* Returns a send right to for the mach device called NAME, and returns it in
   PORT.  Other values returned are START, the first valid offset, SIZE, the
   the number of blocks after START, and BLOCK_SIZE, the units in which the
   device is addressed.

   The device is opened for reading, and if the diskfs global variable
   DISKFS_READ_ONLY is false, writing.

   If NAME cannot be opened and this is a bootstrap filesystem, the user will
   be prompted for new names until a valid one is found.  */
error_t diskfs_get_mach_device (char *name,
				mach_port_t *port,
				off_t *start, off_t *size, size_t *block_size);


/* Call this after arguments have been parsed to initialize the library.
   You must call this before calling any other diskfs functions, and after
   parsing diskfs options.  */
error_t diskfs_init_diskfs (void);

/* Call this once the filesystem is fully initialized, to advertise the new
   filesystem control port to our parent filesystem.  If BOOTSTRAP is set,
   the diskfs will call fsys_startup on that port as appropriate and return
   the REALNODE returned in that call; otherwise we return MACH_PORT_NULL.
   FLAGS specifies how to open REALNODE (from the O_* set).  */
mach_port_t diskfs_startup_diskfs (mach_port_t bootstrap, int flags);

/* Call this after all format-specific initialization is done (except
   for setting diskfs_root_node); at this point the pagers should be
   ready to go.  */
void diskfs_spawn_first_thread (void);

/* Once diskfs_root_node is set, call this if we are a bootstrap
   filesystem.  If you call this, then the library will call
   diskfs_init_completed once it has a valid proc and auth port. */
void diskfs_start_bootstrap ();

/* Node NP now has no more references; clean all state.  The
   _diskfs_node_refcnt_lock must be held, and will be released
   upon return.  NP must be locked.  */
void diskfs_drop_node (struct node *np);

/* Set on disk fields from NP->dn_stat; update ctime, atime, and mtime
   if necessary.  If WAIT is true, then return only after the physical
   media has been completely updated.  */
void diskfs_node_update (struct node *np, int wait);

/* Add a hard reference to a node.  If there were no hard
   references previously, then the node cannot be locked
   (because you must hold a hard reference to hold the lock). */
extern inline void
diskfs_nref (struct node *np)
{
  int new_hardref;
  spin_lock (&diskfs_node_refcnt_lock);
  np->references++;
  new_hardref = (np->references == 1);
  spin_unlock (&diskfs_node_refcnt_lock);
  if (new_hardref)
    {
      mutex_lock (&np->lock);
      diskfs_new_hardrefs (np);
      mutex_unlock (&np->lock);
    }
}

/* Unlock node NP and release a hard reference; if this is the last
   hard reference and there are no links to the file then request
   soft references to be dropped.  */
extern inline void
diskfs_nput (struct node *np)
{
  int tried_drop_softrefs = 0;

 loop:
  spin_lock (&diskfs_node_refcnt_lock);
  assert (np->references);
  np->references--;
  if (np->references + np->light_references == 0)
    diskfs_drop_node (np);
  else if (np->references == 0 && !tried_drop_softrefs)
    {
      spin_unlock (&diskfs_node_refcnt_lock);
      diskfs_lost_hardrefs (np);
      if (!np->dn_stat.st_nlink)
	{
	  /* There are no links.  If there are soft references that
	     can be dropped, we can't let them postpone deallocation.
	     So attempt to drop them.  But that's a user-supplied
	     routine, which might result in further recursive calls to
	     the ref-counting system.  So we have to reacquire our
	     reference around the call to forestall disaster. */
	  spin_lock (&diskfs_node_refcnt_lock);
	  np->references++;
	  spin_unlock (&diskfs_node_refcnt_lock);

	  diskfs_try_dropping_softrefs (np);

	  /* But there's no value in looping forever in this
	     routine; only try to drop soft refs once. */
	  tried_drop_softrefs = 1;

	  /* Now we can drop the reference back... */
	  goto loop;
	}
      mutex_unlock (&np->lock);
    }
  else
    {
      spin_unlock (&diskfs_node_refcnt_lock);
      mutex_unlock (&np->lock);
    }
}

/* Release a hard reference on NP.  If NP is locked by anyone, then
   this cannot be the last hard reference (because you must hold a
   hard reference in order to hold the lock).  If this is the last
   hard reference and there are no links, then request soft references
   to be dropped.  */
extern inline void
diskfs_nrele (struct node *np)
{
  int tried_drop_softrefs = 0;

 loop:
  spin_lock (&diskfs_node_refcnt_lock);
  assert (np->references);
  np->references--;
  if (np->references + np->light_references == 0)
    {
      mutex_lock (&np->lock);
      diskfs_drop_node (np);
    }
  else if (np->references == 0)
    {
      mutex_lock (&np->lock);
      spin_unlock (&diskfs_node_refcnt_lock);
      diskfs_lost_hardrefs (np);
      if (!np->dn_stat.st_nlink && !tried_drop_softrefs)
	{
	  /* Same issue here as in nput; see that for explanation */
	  spin_lock (&diskfs_node_refcnt_lock);
	  np->references++;
	  spin_unlock (&diskfs_node_refcnt_lock);

	  diskfs_try_dropping_softrefs (np);
	  tried_drop_softrefs = 1;

	  /* Now we can drop the reference back... */
	  mutex_unlock (&np->lock);
	  goto loop;
	}
      mutex_unlock (&np->lock);
    }
  else
    spin_unlock (&diskfs_node_refcnt_lock);
}

/* Add a light reference to a node. */
extern inline void
diskfs_nref_light (struct node *np)
{
  spin_lock (&diskfs_node_refcnt_lock);
  np->light_references++;
  spin_unlock (&diskfs_node_refcnt_lock);
}

/* Unlock node NP and release a light reference */
extern inline void
diskfs_nput_light (struct node *np)
{
  spin_lock (&diskfs_node_refcnt_lock);
  assert (np->light_references);
  np->light_references--;
  if (np->references + np->light_references == 0)
    diskfs_drop_node (np);
  else
    {
      spin_unlock (&diskfs_node_refcnt_lock);
      mutex_unlock (&np->lock);
    }
}

/* Release a light reference on NP.  If NP is locked by anyone, then
   this cannot be the last reference (because you must hold a
   hard reference in order to hold the lock).  */
extern inline void
diskfs_nrele_light (struct node *np)
{
  spin_lock (&diskfs_node_refcnt_lock);
  assert (np->light_references);
  np->light_references--;
  if (np->references + np->light_references == 0)
    {
      mutex_lock (&np->lock);
      diskfs_drop_node (np);
    }
  else
    spin_unlock (&diskfs_node_refcnt_lock);
}

/* Return nonzero iff the user identified by CRED has uid UID. */
extern inline int
diskfs_isuid (uid_t uid, struct protid *cred)
{
  int i;
  for (i = 0; i < cred->nuids; i++)
    if (cred->uids[i] == uid)
      return 1;
  return 0;
}

/* Return nonzero iff the user identified by CRED has group GRP. */
extern inline int
diskfs_groupmember (uid_t grp, struct protid *cred)
{
  int i;
  for (i = 0; i < cred->ngids; i++)
    if (cred->gids[i] == grp)
      return 1;
  return 0;
}

/* Check to see if the user identified by CRED is permitted to do
   owner-only operations on node NP; if so, return 0; if not, return
   EPERM. */
extern inline error_t
diskfs_isowner (struct node *np, struct protid *cred)
{
  /* Permitted if the user is the owner, superuser, or if the user
     is in the group of the file and has the group ID as their user
     ID.  (This last is colloquially known as `group leader'.) */
  if (diskfs_isuid (np->dn_stat.st_uid, cred) || diskfs_isuid (0, cred)
      || (diskfs_groupmember (np->dn_stat.st_gid, cred)
	  && diskfs_isuid (np->dn_stat.st_gid, cred)))
    return 0;
  else
    return EPERM;
}

/* Check to see is the user identified by CRED is permitted to do
   operation OP on node NP.  Op is one of S_IREAD, S_IWRITE, or S_IEXEC.
   Return 0 if the operation is permitted and EACCES if not. */
extern inline error_t
diskfs_access (struct node *np, int op, struct protid *cred)
{
  int gotit;
  if (diskfs_isuid (0, cred))
    gotit = 1;
  else if (cred->nuids == 0 && (np->dn_stat.st_mode & S_IUSEUNK))
    gotit = np->dn_stat.st_mode & (op << S_IUNKSHIFT);
  else if (!diskfs_isowner (np, cred))
    gotit = np->dn_stat.st_mode & op;
  else if (diskfs_groupmember (np->dn_stat.st_gid, cred))
    gotit = np->dn_stat.st_mode & (op >> 3);
  else
    gotit = np->dn_stat.st_mode & (op >> 6);
  return gotit ? 0 : EACCES;
}

/* Check to see if the user identified by CRED is allowed to modify
   directory DP with respect to existing file NP.  This is the same
   as diskfs_access (dp, S_IWRITE, cred), except when the directory
   has the sticky bit set.  (If there is no existing file NP, then
   0 can be passed.)  */
extern inline error_t
diskfs_checkdirmod (struct node *dp, struct node *np,
		    struct protid *cred)
{
  error_t err;

  /* The user must be able to write the directory, but if the directory
     is sticky, then the user must also be either the owner of the directory
     or the file.  */
  err = diskfs_access (dp, S_IWRITE, cred);
  if (err)
    return err;

  if ((dp->dn_stat.st_mode & S_ISVTX) && np && !diskfs_isuid (0, cred)
      && !diskfs_isowner (dp, cred) && !diskfs_isowner (np, cred))
    return EACCES;

  return 0;
}

/* Reading and writing of files. this is called by other filesystem
   routines and handles extension of files automatically.  NP is the
   node to be read or written, and must be locked.  DATA will be
   written or filled.  OFF identifies where in thi fel the I/O is to
   take place (-1 is not allowed).  AMT is the size of DATA and tells
   how much to copy.  DIR is 1 for writing and 0 for reading.  CRED is
   the user doing the access (only used to validate attempted file
   extension).  For reads, *AMTREAD is filled with the amount actually
   read.  */
error_t
diskfs_node_rdwr (struct node *np, char *data, off_t off,
		  size_t amt, int dir, struct protid *cred,
		  size_t *amtread);


/* Send notifications to users who have requested them with
   dir_notice_changes for directory DP.  The type of modification and
   affected name are TYPE and NAME respectively.  This should be
   called by diskfs_direnter, diskfs_dirremove, and diskfs_dirrewrite,
   and anything else that changes the directory, after the change is
   fully completed.  */
void
diskfs_notice_dirchange (struct node *dp, enum dir_changed_type type,
			 char *name);

/* Create a new node structure with DS as its physical disknode.
   The new node will have one hard reference and no light references.  */
struct node *diskfs_make_node (struct disknode *dn);


/* The library also exports the following functions; they are not generally
   useful unless you are redefining other functions the library provides. */

/* Lookup in directory DP (which is locked) the name NAME.  TYPE will
   either be LOOKUP, CREATE, RENAME, or REMOVE.  CRED identifies the
   user making the call.

   If the name is found, return zero, and (if NP is nonzero) set *NP
   to point to the node for it, locked.  If the name is not found,
   return ENOENT, and (if NP is nonzero) set *NP to zero.  If NP is
   zero, then the node found must not be locked, even transitorily.
   Lookups for REMOVE and RENAME (which must often check permissions
   on the node being found) will always set NP.

   If DS is nonzero then:
     For LOOKUP: set *DS to be ignored by diskfs_drop_dirstat.
     For CREATE: on success, set *DS to be ignored by diskfs_drop_dirstat.
                 on failure, set *DS for a future call to diskfs_direnter.
     For RENAME: on success, set *DS for a future call to diskfs_dirrewrite.
                 on failure, set *DS for a future call to diskfs_direnter.
     For REMOVE: on success, set *DS for a future call to diskfs_dirremove.
                 on failure, set *DS to be ignored by diskfs_drop_dirstat.
   The caller of this function guarantees that if DS is nonzero, then
   either the appropriate call listed above or diskfs_drop_dirstat will
   be called with DS before the directory DP is unlocked, and guarantees
   that no lookup calls will be made on this directory between this
   lookup and the use (or descruction) of *DS.

   If you use the library's versions of diskfs_rename_dir,
   diskfs_clear_directory, and diskfs_init_dir, then lookups for `..'
   might have the flag SPEC_DOTDOT or'd in.  This has the following special
   meaning:
   For LOOKUP: DP should be unlocked and its reference dropped before
               returning.
   For RENAME and REMOVE: The node being found (*NP) is already held
               locked, so don't lock it or add a reference to it.
   (SPEC_DOTDOT will not be given with CREATE.)

   Return ENOTDIR if DP is not a directory.
   Return EACCES if CRED isn't allowed to search DP.
   Return EACCES if completing the operation will require writing
   the directory and diskfs_checkdirmod won't allow the modification.
   Return ENOENT if NAME isn't in the directory.
   Return EAGAIN if NAME refers to the `..' of this filesystem's root.
   Return EIO if appropriate.
   
   This function is a wrapper for diskfs_lookup_hard. 
*/
error_t diskfs_lookup (struct node *dp, char *name, enum lookup_type type,
		       struct node **np, struct dirstat *ds,
		       struct protid *cred);

/* Add NP to directory DP under the name NAME.  This will only be
   called after an unsuccessful call to diskfs_lookup of type CREATE
   or RENAME; DP has been locked continuously since that call and DS
   is as that call set it, NP is locked.  CRED identifies the user
   responsible for the call (to be used only to validate directory
   growth).  This function is a wrapper for diskfs_direnter_hard.  */
error_t
diskfs_direnter (struct node *dp, char *name, struct node *np, 
		 struct dirstat *ds, struct protid *cred);

/* This will only be called after a successful call to diskfs_lookup
   of type RENAME; this call should change the name found in directory
   DP to point to node NP instead of its previous referent, OLDNP.  DP
   has been locked continuously since the call to diskfs_lookup and DS
   is as that call set it; NP is locked.  This routine should call
   diskfs_notice_dirchange if DP->dirmod_reqs is nonzero.  NAME is the
   name of OLDNP inside DP; it is this reference which is being
   rewritten. This function is a wrapper for diskfs_dirrewrite_hard.  */
error_t diskfs_dirrewrite (struct node *dp, struct node *oldnp,
			   struct node *np, char *name, struct dirstat *ds);

/* This will only be called after a successful call to diskfs_lookup
   of type REMOVE; this call should remove the name found from the
   directory DS.  DP has been locked continuously since the call to
   diskfs_lookup and DS is as that call set it.  This routine should
   call diskfs_notice_dirchange if DP->dirmod_reqs is nonzero.  This
   function is a wrapper for diskfs_dirremove_hard.  The entry being
   removed has name NAME and refers to NP.  */
error_t diskfs_dirremove (struct node *dp, struct node *np,
			  char *name, struct dirstat *ds);

/* Return the node corresponding to CACHE_ID in *NPP. */
error_t diskfs_cached_lookup (int cache_id, struct node **npp);

/* Create a new node. Give it MODE; if that includes IFDIR, also
   initialize `.' and `..' in the new directory.  Return the node in NPP.
   CRED identifies the user responsible for the call.  If NAME is nonzero,
   then link the new node into DIR with name NAME; DS is the result of a
   prior diskfs_lookup for creation (and DIR has been held locked since).
   DIR must always be provided as at least a hint for disk allocation
   strategies.  */
error_t
diskfs_create_node (struct node *dir, char *name, mode_t mode,
		    struct node **newnode, struct protid *cred,
		    struct dirstat *ds);

/* Create and return a protid for an existing peropen PO in CRED.  The uid
   set is UID (length NUIDS); the gid set is GID (length NGIDS).  The node
   PO->np must be locked. */
error_t diskfs_create_protid (struct peropen *po, uid_t *uids, int nuids,
			      uid_t *gids, int ngids, struct protid **cred);

/* Backward compatibility.  Use diskfs_create_protid. */
struct protid *diskfs_make_protid (struct peropen *cred, uid_t *uids,
				   int nuids, uid_t *gids, int ngids);

/* Build and return in CRED a protid which has no user identification, for
   peropen PO.  The node PO->np must be locked.  */
error_t diskfs_start_protid (struct peropen *po, struct protid **cred);

/* Finish building protid CRED started with diskfs_start_protid;
   the uid set is UID (length NUIDS); the gid set is GID (length NGIDS). */
void diskfs_finish_protid (struct protid *cred, uid_t *uids, int nuids,
			   gid_t *gids, int nguds);

/* Create and return a new peropen structure on node NP with open
   flags FLAGS.  */
struct peropen *diskfs_make_peropen (struct node *np, int flags,
				     mach_port_t dotdotnode);

/* Called when a protid CRED has no more references.  (Because references\
   to protids are maintained by the port management library, this is
   installed in the clean routines list.)  The ports library will
   free the structure for us.  */
void diskfs_protid_rele (void *arg);

/* Decrement the reference count on a peropen structure. */
void diskfs_release_peropen (struct peropen *po);

/* Node NP has just been found in DIR with NAME.  If NP is null, that
   means that this name has been confirmed as absent in the directory. */
void diskfs_enter_lookup_cache (struct node *dir, struct node *np, char *name);

/* Purge all references in the cache to NP as a node inside 
   directory DP. */
void diskfs_purge_lookup_cache (struct node *dp, struct node *np);

/* Scan the cache looking for NAME inside DIR.  If we don't know
   anything entry at all, then return 0.  If the entry is confirmed to
   not exist, then return -1.  Otherwise, return NP for the entry, with
   a newly allocated reference. */
struct node *diskfs_check_lookup_cache (struct node *dir, char *name);

/* Rename directory node FNP (whose parent is FDP, and which has name
   FROMNAME in that directory) to have name TONAME inside directory
   TDP.  None of these nodes are locked, and none should be locked
   upon return.  This routine is serialized, so it doesn't have to be
   reentrant.  Directories will never be renamed except by this
   routine.  FROMCRED and TOCRED are the users responsible for
   FDP/FNP and TDP respectively.  This routine assumes the usual
   convention where `.' and `..' are represented by ordinary links;
   if that is not true for your format, you have to redefine this
   function.*/
error_t
diskfs_rename_dir (struct node *fdp, struct node *fnp, char *fromname,
		   struct node *tdp, char *toname, struct protid *fromcred,
		   struct protid *tocred);

/* Clear the `.' and `..' entries from directory DP.  Its parent is
   PDP, and the user responsible for this is identified by CRED.  Both
   directories must be locked.  This routine assumes the usual
   convention where `.' and `..' are represented by ordinary links; if
   that is not true for your format, you have to redefine this
   function. */
error_t diskfs_clear_directory (struct node *dp, struct node *pdp,
				struct protid *cred);

/* Locked node DP is a new directory; add whatever links are necessary
   to give it structure; its parent is the (locked) node PDP.
   This routine may not call diskfs_lookup on PDP.  The new directory
   must be clear within the meaning of diskfs_dirempty.  This routine
   assumes the usual convention where `.' and `..' are represented by
   ordinary links; if that is not true for your format, you have to
   redefine this function.   CRED identifies the user making the call. */
error_t
diskfs_init_dir (struct node *dp, struct node *pdp, struct protid *cred);

/* If NP->dn_set_ctime is set, then modify NP->dn_stat.st_ctime
   appropriately; do the analogous operation for atime and mtime as well. */
void diskfs_set_node_times (struct node *np);

/* Shutdown the filesystem; flags are as for fsys_shutdown. */
error_t diskfs_shutdown (int flags);

/* Change an active filesystem between read-only and writable modes, setting
   the global variable DISKFS_READONLY to reflect the current mode.  If an
   error is returned, nothing will have changed.  DISKFS_FSYS_LOCK should be
   held while calling this routine.  */
error_t diskfs_set_readonly (int readonly);

/* Re-read all incore data structures from disk.  This will only work if
   DISKFS_READONLY is true.  DISKFS_FSYS_LOCK should be held while calling
   this routine.  */
error_t diskfs_remount ();

/* Called by S_fsys_startup for execserver bootstrap.  The execserver
   is able to function without a real node, hence this fraud.  Arguments
   are all as for fsys_startup in <hurd/fsys.defs>.  */
error_t diskfs_execboot_fsys_startup (mach_port_t port, int flags,
				      mach_port_t ctl, mach_port_t *real,
				      mach_msg_type_name_t *realpoly);

/* Establish a thread to sync the filesystem every INTERVAL seconds, or
   never, if INTERVAL is zero.  If an error occurs creating the thread, it is
   returned, otherwise 0.  Subsequent calls will create a new thread and
   (eventually) get rid of the old one; the old thread won't do any more
   syncs, regardless.  */
error_t diskfs_set_sync_interval (int interval);

/* Parse and execute the runtime options in ARGC and ARGV.  EINVAL is
   returned if some option is unrecognized.  */
error_t diskfs_set_options (int argc, char **argv);

/* Return an argz string describing the current options.  Fill *ARGZ
   with a pointer to newly malloced storage holding the list and *LEN
   to the length of that storage. */
error_t diskfs_get_options (char **argz, unsigned *argz_len);

/* A pointer to an argp structure for the standard diskfs command line
   arguments.  The user may call argp_parse on this to parse the command
   line, chain it onto the end of his own argp structure, or ignore it
   completely.  */
extern const struct argp *diskfs_startup_argp;

/* Demultiplex incoming messages on ports created by libdiskfs.  */
int diskfs_demuxer (mach_msg_header_t *, mach_msg_header_t *);

/* The diskfs library provides functions to demultiplex the fs, io,
   fsys, interrupt, and notify interfaces.  All the server routines
   have the prefix `diskfs_S_'; `in' arguments of type file_t or io_t
   appear as `struct protid *' to the stub.  */

/* The following are optional convenience routines and global variable, which
   can be used by any user program that uses a mach device to hold the
   underlying filesystem.  */

/* A pointer to an argp structure for the standard diskfs command line
   arguments, that also parses a device argument.  The user may call
   argp_parse on this to parse the command line, chain it onto the end of his
   own argp structure, or ignore it completely.  */
extern const struct argp *diskfs_device_startup_argp;

/* The following two are set by the preceding argument parser:  */
/* The device specifier given on the command line.  */
extern char *diskfs_device_arg;
/* True if --machdev was specified, meaning that DISKFS_DEVICE_ARG names a
   mach device and not a filesystem device node.  */
extern int diskfs_use_mach_device;

/* Uses the values of DISKFS_DEVICE_ARG and DISKFS_USE_MACH_DEVICE, and
   attempts to open the device and set the values of DISKFS_DEVICE,
   DISKFS_DEVICE_NAME, DISKFS_DEVICE_START, DISKFS_DEVICE_SIZE, and
   DISKFS_DEVICE_BLOCK_SIZE.  */
extern error_t diskfs_device_open ();

/* A mach device port for the device we're using.  */
extern mach_port_t diskfs_device;

/* The mach device name of DISKFS_DEVICE.  May be 0 if unknown.  */
extern char *diskfs_device_name;

/* The first valid block of DISKFS_DEVICE, in units of
   DISKFS_DEVICE_BLOCK_SIZE.  */
extern off_t diskfs_device_start;

/* The usable size of DISKFS_DEVICE, in units of DISKFS_DEVICE_BLOCK_SIZE.  */
extern off_t diskfs_device_size;

/* The unit of addressing for DISKFS_DEVICE.  */
extern unsigned diskfs_device_block_size;

/* Some handy calculations based on DISKFS_DEVICE_BLOCK_SIZE.  */
/* Log base 2 of DEVICE_BLOCK_SIZE, or 0 if it's not a power of two.  */
extern unsigned diskfs_log2_device_block_size;
/* Log base 2 of the number of device blocks in a vm page.  This number is
   only valid if DISKFS_LOG2_DEVICE_BLOCK_SIZE is not 0.  */
extern unsigned diskfs_log2_device_blocks_per_page;

/* Write disk block ADDR with DATA of LEN bytes to DISKFS_DEVICE, waiting for
   completion.  ADDR is offset by DISKFS_DEVICE_START.  If an error occurs,
   EIO is returned.  */
error_t diskfs_device_write_sync (off_t addr, vm_address_t data, size_t len);

/* Read disk block ADDR from DISKFS_DEVICE; put the address of the data in
   DATA; read LEN bytes.  Always *DATA should be a full page no matter what.
   ADDR is offset by DISKFS_DEVICE_START.  If an error occurs, EIO is
   returned.  */
error_t diskfs_device_read_sync (off_t addr, vm_address_t *data, size_t len);

/* Make errors go somewhere reasonable.  */
void diskfs_console_stdio ();


#endif	/* hurd/diskfs.h */

