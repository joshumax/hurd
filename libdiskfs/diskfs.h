/* Definitions for fileserver helper functions
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

#ifndef _HURD_DISKFS
#define _HURD_DISKFS

/* Each user port referring to a file points to one of these
   (with the aid of the ports library. */
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
  
  struct trans_link translator;

  struct lock_box userlock;

  struct conch conch;

  struct dirmod *dirmod_reqs;

  off_t allocsize;
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

extern mach_port_t diskfs_host_priv;	/* send right */
extern mach_port_t diskfs_master_device; /* send right */
extern mach_port_t diskfs_default_pager; /* send right */
extern mach_port_t diskfs_exec_ctl;	/* send right */
extern mach_port_t diskfs_exec;	/* send right */
extern auth_t diskfs_auth_server_port; /* send right */


extern volatile struct mapped_time_value *diskfs_mtime;

extern int diskfs_bootflags;
extern char *diskfs_bootflagarg;

extern spin_lock_t diskfs_node_refcnt_lock;

extern int pager_port_type;


struct pager;


/* The user must observe the following discipline in port creation
   with the ports library.  Any port for which a send right is
   given to any entity outside the filesystem itself must be
   a hard port.  Other ports used only internally should be
   soft ports.  

   When there are neither hard ports nor soft ports, the diskfs
   library will call diskfs_sync_everything with WAIT set.  This 
   should sync everything; upon return diskfs will exit.  (There
   should be no active pagers at all in this state, because such
   would be either hard or soft ports.)

   When there are only soft ports, diskfs will call
   diskfs_shutdown_soft_ports.  This function should initiate (but
   need not complete) the destruction of any outstanding soft ports.
   When they are actually destroyed, diskfs_sync_everything will be
   called as described above.

   When the only outstanding hard port is the control port for
   the filesystem and the filesystem has been idle for a considerable
   time (as defined by the ports library), diskfs will detach from the
   parent filesystem and call diskfs_shutdown_soft_ports.

   When the filesystem is asked to exit with FSYS_GOAWAY_NOSYNC,
   it will not bother calling diskfs_sync_everything.  When the
   filesystem is asked to exit with FSYS_GOAWAY_FORCE, all existing
   send rights for files are immediately destroyed.  Then 
   diskfs_sync_everything is called and the filesystem will exit.
*/


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

/* The user must define this function.  Set *STATFSBUF with
   appropriate values to reflect the current state of the filesystem.  */
error_t diskfs_set_statfs (fsys_statfsbuf_t *statfsbuf);

/* The user must define this function.  Lookup in directory DP (which
   is locked) the name NAME.  TYPE will either be LOOKUP, CREATE,
   RENAME, or REMOVE.  CRED identifies the user making the call.

   If the name is found, return zero, and (if NP is nonzero) set *NP
   to point to the node for it, locked.  If the name is not found,
   return ENOENT, and (if NP is nonzero) set *NP to zero.

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
*/
error_t diskfs_lookup (struct node *dp, char *name, enum lookup_type type,
			 struct node **np, struct dirstat *ds, 
		       struct protid *cred);

/* The user must define this function.  Add NP to directory DP
   under the name NAME.  This will only be called after an
   unsuccessful call to diskfs_lookup of type CREATE or RENAME; DP
   has been locked continuously since that call and DS is as that call
   set it, NP is locked.   CRED identifies the user responsible
   for the call (to be used only to validate directory growth). 
   The routine should call diskfs_notice_dirchange if DP->dirmod_reqs
   is nonzero.  */
error_t diskfs_direnter (struct node *dp, char *name, 
			 struct node *np, struct dirstat *ds,
			 struct protid *cred);

/* The user must define this function.  This will only be called after
   a successful call to diskfs_lookup of type RENAME; this call should change
   the name found in directory DP to point to node NP instead of its previous
   referent.  DP has been locked continuously since the call to diskfs_lookup
   and DS is as that call set it; NP is locked.  This routine should call 
   diskfs_notice_dirchange if DP->dirmod_reqs is nonzero.  */
error_t diskfs_dirrewrite (struct node *dp, struct node *np,
			     struct dirstat *ds);

/* The user must define this function.  This will only be called after a
   successful call to diskfs_lookup of type REMOVE; this call should remove
   the name found from the directory DS.  DP has been locked continuously since
   the call to diskfs_lookup and DS is as that call set it.  This routine 
   should call diskfs_notice_dirchange if DP->dirmod_reqs is nonzero.  */
error_t diskfs_dirremove (struct node *dp, struct dirstat *ds);

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

/* The user must define this function.  For locked node NP, return nonzero
   iff there is a translator program defined for the node.  */
int diskfs_node_translated (struct node *np);

/* The user must define this function.  For locked node NP (for which
   diskfs_node_translated is true) look up the name of its translator.
   If the length is <= *NAMELEN, then store the name into **NAMEP; otherwise
   set *NAMEP to newly vm_allocate'd storage holding the name.  Set
   *NAMELEN to the length of the name.  */
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
   so that if any light references can be freed, they are.  NP might
   or might not be locked; this routine should not attempt to gain the lock. */
void diskfs_lost_hardrefs (struct node *np);

/* The user must define this function.  Node NP has just acquired
   a hard reference where it had none previously.  It is thus now
   OK again to have light references without real users.  NP might or
   might not be locked; this routine should not attempt to gain the lock. */
void diskfs_new_hardrefs (struct node *np);

/* The user must define this function.  There are no hard ports outstanding;
   the user should take steps so that any soft ports outstanding get
   shut down. */
void diskfs_shutdown_soft_ports ();

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

/* The user must define this function.  Sync all the pagers and any
   data belonging on disk except for the hypermetadata.  If WAIT is true,
   then return only after the physicial media has been completely updated. */
void diskfs_sync_everything (int wait);

/* Shutdown all pagers; this is done when the filesystem is exiting and is
   irreversable.  */
void diskfs_shutdown_pager ();

/* The user must define this function.  Return a memory object port (send
   right) for the file contents of NP.  */
mach_port_t diskfs_get_filemap (struct node *np);

/* The user must define this function.  Return a `struct pager *' suitable
   for use as an argument to diskfs_register_memory_fault_area that
   refers to the pager returned by diskfs_get_filemap for node NP.  */
struct pager *diskfs_get_filemap_pager_struct (struct node *np);

/* The user must define this function if she calls diskfs_start_bootstrap.
   It is called by the library after the filesystem has a normal 
   environment (complete with auth and proc ports). */
void diskfs_init_completed ();

/* It is assumed that the user will use the Hurd pager library; if not
   you need to redefine ports_demuxer and 
   diskfs_do_seqnos_mach_notify_no_senders.  */

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

/* Call this if the bootstrap port is null and you want to support
   being a bootstrap filesystem.  ARGC and ARGV should be as passed
   to main.  If the arguments are not in the proper format, an
   error message will be printed on stderr and exit called.  Otherwise,
   diskfs_priv_host, diskfs_master_device, and diskfs_bootflags will be
   set and the Mach kernel name of the bootstrap device will be
   returned.  */
char *diskfs_parse_bootargs (int argc, char **argv);

/* Call this after arguments have been parsed to initialize the
   library.  If BOOTSTRAP is set, the diskfs will call fsys_startup
   on that port as appropriate and return the REALNODE returned
   in that call; otherwise we return MACH_PORT_NULL.  */ 
mach_port_t diskfs_init_diskfs (mach_port_t bootstrap);

/* Call this after all format-specific initialization is done (except
   for setting diskfs_root_node); at this point the pagers should be
   ready to go.  */ 
void diskfs_spawn_first_thread (void);

/* Once diskfs_root_node is set, call this if we are a bootstrap
   filesystem.  If you call this, then the library will call
   diskfs_init_completed once it has a valid proc and auth port.*/
void diskfs_start_bootstrap (void);

/* Last step of init is to call this, which never returns.  */
void diskfs_main_request_loop (void);

/* Node NP now has no more references; clean all state.  The
   _diskfs_node_refcnt_lock must be held, and will be released
   upon return.  NP must be locked.  */
void diskfs_drop_node (struct node *np);

/* Set on disk fields from NP->dn_stat; update ctime, atime, and mtime
   if necessary.  If WAIT is true, then return only after the physical
   media has been completely updated.  */
void diskfs_node_update (struct node *np, int wait);

/* Add a hard reference to a node. */
extern inline void
diskfs_nref (struct node *np)
{
  int new_hardref;
  spin_lock (&diskfs_node_refcnt_lock);
  np->references++;
  new_hardref = (np->references == 1);
  spin_unlock (&diskfs_node_refcnt_lock);
  if (new_hardref)
    diskfs_new_hardrefs (np);
}

/* Unlock node NP and release a hard reference; if this is the last
   hard reference and there are no links to the file then request
   soft references to be dropped.  */
extern inline void
diskfs_nput (struct node *np)
{
  int nlinks;

  spin_lock (&diskfs_node_refcnt_lock);
  np->references--;
  if (np->references + np->light_references == 0)
    diskfs_drop_node (np);
  else if (np->references == 0)
    {
      spin_unlock (&diskfs_node_refcnt_lock);
      nlinks = np->dn_stat.st_nlink;
      mutex_unlock (&np->lock);
      if (!nlinks)
	diskfs_lost_hardrefs (np);
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
  int nlinks;
  
  spin_lock (&diskfs_node_refcnt_lock);
  np->references--;
  if (np->references + np->light_references == 0)
    {
      mutex_lock (&np->lock);
      diskfs_drop_node (np);
    }
  else if (np->references == 0)
    {
      mutex_lock (&np->lock);
      nlinks = np->dn_stat.st_nlink;
      mutex_unlock (&np->lock);
      spin_unlock (&diskfs_node_refcnt_lock);
      if (!nlinks)
	diskfs_lost_hardrefs (np);
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
  /* The user must be able to write the directory, but if the directory
     is sticky, then the user must also be either the owner of the directory
     or the file.  */
  return (diskfs_access (dp, S_IWRITE, cred)
	  && (!(dp->dn_stat.st_mode & S_ISVTX) || !np || diskfs_isuid (0,cred)
	      || diskfs_isowner (dp, cred) || diskfs_isowner (np, cred)));
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
		  int amt, int dir, struct protid *cred,
		  int *amtread);


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

/* The following two calls are actually macros.  */
/* Begin executing code which might fault.  This contains a call
   to setjmp and so callers must be careful with register variables.
   The first time through, this returns 0.  If the code faults
   accessing a region of memory registered with 
   diskfs_register_memory_fault_area, then this routine will return
   again with the error number as reported by the pager.  */
/* int diskfs_catch_exception (void); */

/* After calling diskfs_catch_exception, this routine must be called
   before exiting the function which called diskfs_catch_exception.
   It will cancel the fault protection installed by diskfs_catch_exception. */
/* void diskfs_end_catch_exception (void); */

/* Register a region of memory for protected fault status as described
   above for diskfs_catch_exception.  This should generally be done
   for any vm_map of the filesystem's own data.  This will mark memory
   at ADDR continuing for LEN bytes to be mapped from pager P at offset
   OFF.  Any memory exceptions in this region will be looked up with
   pager_get_error (until the XP interface is fixed); this is the only
   use made of arguments P and OFF.  */
void diskfs_register_memory_fault_area (struct pager *p, vm_address_t off,
					void *addr, long len);

/* Remove the registration of a region registered with
   diskfs_register_memory_fault_area; the region is that at ADDR
   continuing for LEN bytes. */
void diskfs_unregister_memory_fault_area (void *addr, long len);


/* The library also exports the following functions; they are not generally
   useful unless you are redefining other functions the library provides. */

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

/* Start the translator on node NP.  NP is locked.  The node referenced
   by DIR must not be locked.  NP will be unlocked during the execution
   of this function, and then relocked before return.  The authentication
   of DIR is ignored, so it may be anything convenient.  DIRCRED identifies
   the directory in which this node was found, or 0 if it is root.  */
error_t diskfs_start_translator (struct node *np, file_t dir,
				 struct protid *dircred);

/* Create and return a protid for an existing peropen.  The uid set is
   UID (length NUIDS); the gid set is GID (length NGIDS).  The node
   PO->np must be locked. */
struct protid *diskfs_make_protid (struct peropen *cred, uid_t *uids, 
				   int nuids, uid_t *gids, int ngids);

/* Build and return a protid which has no user identification for 
   peropen PO.  The node PO->np must be locked.  */
struct protid *diskfs_start_protid (struct peropen *po);

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

/* Get rid of any translator running on the file NP; FLAGS
   (from the set FSYS_GOAWAY_*) describes details of shutting
   down the child filesystem.  */
void diskfs_destroy_translator (struct node *np, int flags);

/* Sync all the running translators.  Wait for them to complete if 
   WAIT is nonzero.  */
void diskfs_sync_translators (int wait);

/* If NP->dn_set_ctime is set, then modify NP->dn_stat.st_ctime
   appropriately; do the analogous operation for atime and mtime as well. */
void diskfs_set_node_times (struct node *np);

/* Shutdown the filesystem; flags are as for fsys_shutdown. */
error_t diskfs_shutdown (int flags);

/* Called by S_fsys_startup for execserver bootstrap.  The execserver
   is able to function without a real node, hence this fraud.  Arguments
   are all as for fsys_startup in <hurd/fsys.defs>.  */
error_t diskfs_execboot_fsys_startup (mach_port_t port, mach_port_t ctl,
				      mach_port_t *real,
				      mach_msg_type_name_t *realpoly);

/* The ports library requires the following to be defined; the diskfs
   library provides a definition.  See <hurd/ports.h> for the
   interface description.  The library assumes you use the pager
   library (and calls pager_demuxer).  If you don't, then you need
   to redefine this function as well as the no_senders notify stub.  */
int ports_demuxer (mach_msg_header_t *, mach_msg_header_t *);

/* The diskfs library provides functions to demultiplex the fs, io, fsys,
   memory_object, interrupt, and notify interfaces.  All the server
   routines have the prefix `diskfs_S_'; `in' arguments of type
   file_t or io_t appear as `struct protid *' to the stub.  */


/* Exception handling */

#include <cthreads.h>
#include <setjmp.h>
struct thread_stuff
{
  jmp_buf buf;
  struct thread_stuff *link;
};

#define diskfs_catch_exception() \
  ({									    \
    struct thread_stuff *tmp;						    \
    tmp = __builtin_alloca (sizeof (struct thread_stuff));		    \
    tmp->link = (struct thread_stuff *)cthread_data (cthread_self ());	    \
    cthread_set_data (cthread_self (), (any_t)tmp);			    \
    setjmp (tmp->buf);							    \
  })

#define diskfs_end_catch_exception() \
  (void) ({								    \
    struct thread_stuff *tmp;						    \
    tmp = (struct thread_stuff *)cthread_data (cthread_self ());	    \
    (void) cthread_set_data (cthread_self (), (any_t)tmp->link);	    \
  })

#endif
