/* Library providing helper functions for io servers.
   Copyright (C) 1993,94,96,98,2001,02 Free Software Foundation, Inc.

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

#ifndef _HURD_IOHELP_
#define _HURD_IOHELP_

#include <mach.h>
#include <hurd/hurd_types.h>
#include <pthread.h>
#include <hurd/shared.h>

/* Conch manipulation.  */
struct conch
{
  pthread_mutex_t *lock;
  pthread_cond_t wait;
  void *holder;
  struct shared_io *holder_shared_page;
};

/* Initialize a conch box */
void iohelp_initialize_conch (struct conch *, pthread_mutex_t *);

/* These routines are not reentrant.  The server is responsible
   for ensuring that all calls to these routines are serialized
   by locking the lock passed to initialize_conch. */

/* Handle a user request to obtain the conch (io_get_conch) */
void iohelp_handle_io_get_conch (struct conch *, void *,
				      struct shared_io *);

/* Obtain the conch for the server */
void iohelp_get_conch (struct conch *);

/* Handle a user request to release the conch (io_release_conch). */
void iohelp_handle_io_release_conch (struct conch *, void *);

/* Check if the user is allowed to make a shared-data notification
   message. */
error_t iohelp_verify_user_conch (struct conch *, void *);

/* This function must by defined by the server.  It should transfer
   information from the current conch holder's shared page to the server's
   data (the arg is the conch owner). */
void iohelp_fetch_shared_data (void *);

/* This function must be defined by the server.  It should transfer
   information from the server's data to the current conch holder's
   shared page (the arg is the conch owner). */
void iohelp_put_shared_data (void *);



/* User identification */

#include <idvec.h>

struct iouser
{
  struct idvec *uids, *gids;
  void *hook; /* Never used by iohelp library */
};

/* Return a copy of IOUSER in CLONE.  On error, *CLONE is set to NULL.  */
error_t iohelp_dup_iouser (struct iouser **clone, struct iouser *iouser);

/* Free a reference to IOUSER. */
void iohelp_free_iouser (struct iouser *iouser);

/* Create a new IOUSER in USER for the specified idvecs.  On error, *USER
   is set to NULL.  */
error_t iohelp_create_iouser (struct iouser **user, struct idvec *uids,
			      struct idvec *gids);

/* Create a new IOUSER in USER for the specified arrays.  On error, *USER
   is set to NULL.  */
error_t iohelp_create_complex_iouser (struct iouser **user,
				      const uid_t *uids, int nuids,
				      const gid_t *gids, int ngids);

/* Create a new IOUSER in USER for the specified uid and gid.  On error,
   *USER is set to NULL.  */
error_t iohelp_create_simple_iouser (struct iouser **user,
				     uid_t uid, gid_t gid);

/* Create a new IOUSER in USER with no identity.  On error, *USER is set
   to NULL.  */
error_t iohelp_create_empty_iouser (struct iouser **user);

/* Create a new IOUSER in NEW_USER that restricts OLD_USER to the subset
   specified by the two ID lists.  This is appropriate for implementing
   io_restrict_auth.  */
error_t iohelp_restrict_iouser (struct iouser **new_user,
				const struct iouser *old_user,
				const uid_t *uids, int nuids,
				const gid_t *gids, int ngids);

/* Conduct a reauthentication transaction, returning a new iouser in
   USER.  AUTHSERVER is the I/O servers auth port.  The rendezvous port
   provided by the user is REND_PORT.  If the transaction cannot be
   completed, return zero, unless PERMIT_FAILURE is non-zero.  If
   PERMIT_FAILURE is nonzero, then should the transaction fail, return
   an iouser that has no ids.  The new port to be sent to the user is
   newright.  On error, *USER is set to NULL.  */
error_t iohelp_reauth (struct iouser **user, auth_t authserver,
		       mach_port_t rend_port, mach_port_t newright,
		       int permit_failure);


/* Puts data from the malloced buffer BUF, LEN bytes long, into RBUF & RLEN,
   suitable for returning from a mach rpc.  If LEN > 0, BUF is freed,
   regardless of whether an error is returned or not.  */
error_t iohelp_return_malloced_buffer (char *buf, size_t len,
				       char **rbuf,
				       mach_msg_type_number_t *rlen);



#endif
