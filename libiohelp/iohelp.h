/* 
   Copyright (C) 1993, 1994, 1996 Free Software Foundation

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
#include <cthreads.h>
#include <hurd/shared.h>

/* Conch manipulation.  */
struct conch
{
  struct mutex *lock;
  struct condition wait;
  void *holder;
  struct shared_io *holder_shared_page;
};

/* Initialize a conch box */
void iohelp_initialize_conch (struct conch *, struct mutex *);

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


#endif
