/* vcons.h - Interface for the device independant part of a virtual console.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <errno.h>
#include <argp.h>
#include <sys/types.h>
#include <sys/ioctl.h>


/* A handle for a console device.  */
typedef struct cons *cons_t;

/* A handle for a virtual console device.  */
typedef struct vcons *vcons_t;


/* Lookup the console with name NAME, acquire a reference for it, and
   return it in R_CONS.  If NAME doesn't exist, return ESRCH.  */
error_t cons_lookup (const char *name, cons_t *r_cons);

/* Release a reference to CONS.  */
void cons_release (cons_t cons);

/* Lookup the virtual console with number ID in the console CONS,
   acquire a reference for it, and return it in R_VCONS.  If CREATE is
   true, the virtual console will be created if it doesn't exist yet.
   If CREATE is true, and ID 0, the first free virtual console id is
   used.  */
error_t vcons_lookup (cons_t cons, int id, int create, vcons_t *r_vcons);

/* Release a reference to the virtual console VCONS.  If this was the
   last reference the virtual console is destroyed.  */
void vcons_release (vcons_t vcons);


/* Activate virtual console VCONS for WHO.  WHO is a unique identifier
   for the entity requesting the activation (which can be used by the
   display driver to group several activation requests together).  */
void vcons_activate (vcons_t vcons, int who);

/* Resume the output on the virtual console VCONS.  */
void vcons_start_output (vcons_t vcons);

/* Stop all output on the virtual console VCONS.  */
void vcons_stop_output (vcons_t vcons);

/* Return the number of pending output bytes for VCONS.  */
size_t vcons_pending_output (vcons_t vcons);

/* Fush the input buffer, discarding all pending data.  */
void vcons_flush_input (vcons_t vcons);

/* Fush the output buffer, discarding all pending data.  */
void vcons_discard_output (vcons_t vcons);

/* Add DATALEN bytes starting from DATA to the input queue in
   VCONS.  */
error_t vcons_input (vcons_t vcons, char *data, size_t datalen);

/* Output DATALEN characters from the buffer DATA on the virtual
   console VCONS.  The DATA must be supplied in the system encoding
   configured for VCONS.  The function returns the amount of bytes
   written (might be smaller than DATALEN) or -1 and the error number
   in errno.  If NONBLOCK is not zero, return with -1 and set errno
   to EWOULDBLOCK if operation would block for a long time.  */
ssize_t vcons_output (vcons_t vcons, int nonblock, char *data, size_t datalen);

/* Hang interruptible until one of the conditions in TYPE is
   fulfilled.  Return the conditions fulfilled in TYPE.  */
error_t vcons_select (vcons_t vcons, int *type);

/* Return the dimension of the virtual console VCONS in WINSIZE.  */
void vcons_getsize (vcons_t vcons, struct winsize *winsize);

/* Set the owner of the virtual console VCONS to PID.  The owner
   receives the SIGWINCH signal when the terminal size changes.  */
error_t vcons_set_owner (vcons_t vcons, pid_t pid);

/* Return the owner of the virtual console VCONS in PID.  If there is
   no owner, return ENOTTY.  */
error_t vcons_get_owner (vcons_t vcons, pid_t *pid);
