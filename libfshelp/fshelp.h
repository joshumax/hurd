/* FS helper library definitions
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

#ifndef _HURD_FSHELP_
#define _HURD_FSHELP_

/* This library implements various things that are generic to
   all or most implementors of the filesystem protocol.  It 
   presumes that you are using the iohelp library as well.  It
   is divided into separate facilities which may be used independently.  */


/* Translator linkage.  These routines only work for multi-threaded
   servers, and assume you are using the ports library.  */

/* Define one of these structures as part of every disk node.  */
struct trans_link
{
  /* control port for the child filesystem */
  fsys_t control;

  /* this is woken up when fsys_startup is receieved 
     from the child filesystem. */
  struct condition initwait;

  /* This indicates that someone has already started up the translator */
  int starting;

  /* Error to return to user */
  error_t error;

  /* Linked list of all translators */
  struct trans_link *next, **prevp;
};

/* The user must define this variable.  This is the libports type for
   bootstrap ports given to newly started translators. */
extern int fshelp_transboot_port_type;

/* Call this before calling any of the other translator linkage routines,
   normally from your main node initialization routine. */
void fshelp_init_trans_link (struct trans_link *LINK);

/* Call to set the control field of translator LINK to CTL
   directly. */
void fshelp_set_control (struct trans_link *link, mach_port_t ctl);

/* Call this when the control field of a translator is null and you
   want to have the translator started so you can talk to it.  LINK is
   the trans_link structure for this node; NAME is the file to execute
   as the translator (*NAME may be modified).  DIR and NODE should be
   send rights; DIR will be reauthenticated before being given to the
   translator and NODE should already be authenticated properly.  Both
   send-rights will be consumed by this call, whatever its exit value.  DIR
   should refer to the directory holding the node being translater,
   and will be provided as the cwdir of the process.  NODE should
   refer to the node being translated, and will be provided as the
   realnode return value from fsys_startup.  UID and GID are the uid
   and gid of the process to be started.  LOCK must be a mutex which
   you hold; it is assumed that the trans_link structure will not be
   changed unless this is held. */
error_t fshelp_start_translator (struct trans_link *link, char *name, 
				 int namelen, file_t dir, file_t node, 
				 uid_t uid, gid_t gid, struct mutex *lock);

/* Call this when you receive a fsys_startup message on a port of type
   fshelp_transboot_port_type.  PORTSTRUCT is the result of
   ports_check_port_type/ports_get_port; this routine does not call
   ports_done_with_port so the caller normally should.  CTL, REAL, and
   REALPOLY, are copied from the fsys_startup message; CTL will be
   installed as the control field of the translator making this call,
   *REAL will be set to be the underlying port (by calling the
   MAKE_PORT function set at fshelp_start_translator time with the
   NODE argument to that call).  *REALPOLY will be set to the Mach
   message transmission types for that.  If this routine returns an
   error, then the CTL port must be deallocated by the caller. */
error_t fshelp_handle_fsys_startup (void *portstruct, mach_port_t ctl,
				    mach_port_t *real, 
				    mach_msg_type_name_t *realpoly);

/* Install this routine as the ports library type-specific clean routine
   for ports of type fshelp_transboot_port_type. */
void fshelp_transboot_clean (void *arg);

/* Call function FUNC for each translator that has completed its
   startup.  The arguments to FUNC are the translator and ARG 
   respectively.  */
void fshelp_translator_iterate (void (*func)(struct trans_link *, void *),
				void *arg);

/* Set the active translator port to null and clear state.  Deallocate
   our send right on the translator control port. */
void fshelp_translator_drop (struct trans_link *link);

/* A trans_link structure is being deallocated; clean up any state
   we need to. */
void fshelp_kill_translator (struct trans_link *link);



/* Flock handling. */
struct lock_box
{
  int type;
  struct condition wait;
  int waiting;
  int shcount;
};

/* Call when a user makes a request to acquire an lock via file_lock.
   There should be one lock box per object and one int per open; these
   are passed as arguments BOX and USER respectively.  FLAGS are as
   per file_lock.  MUT is a mutex which will be held whenever this
   routine is called, to lock BOX->wait.  */ 
error_t fshelp_acquire_lock (struct lock_box *box, int *user, 
			     struct mutex *mut, int flags);

  
/* Initialize lock_box BOX.  (The user int passed to fshelp_acquire_lock
   should be initialized with LOCK_UN.).  */
void fshelp_lock_init (struct lock_box *box);


#endif
