/* display.h - Interface to the display component of a virtual console.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
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

#ifndef DISPLAY_H
#define DISPLAY_H

#include <sys/ioctl.h>

struct display;
typedef struct display *display_t;

void display_init (void);

/* Create a new virtual console display, with the system encoding
   being ENCODING and the default colors being FOREGROUND and BACKGROUND.  */
error_t
display_create (display_t *r_display, const char *encoding,
		conchar_attr_t def_attr, unsigned int lines,
		unsigned int width, unsigned int height);


/* Destroy the display DISPLAY.  */
void display_destroy (display_t display);

/* Return the dimension of the display in bytes.  */
off_t display_get_size (display_t display);

/* Return the dimensions of the display DISPLAY in *WINSIZE.  */
void display_getsize (display_t display, struct winsize *winsize);

/* Set the owner of the display DISPLAY to PID.  The owner receives
   the SIGWINCH signal when the terminal size changes.  */
error_t display_set_owner (display_t display, pid_t pid);

/* Return the owner of the display DISPLAY in PID.  If there is no
   owner, return ENOTTY.  */
error_t display_get_owner (display_t display, pid_t *pid);

/* Output DATALEN characters from the buffer DATA on display DISPLAY.
   The DATA must be supplied in the system encoding configured for
   DISPLAY.  The function returns the amount of bytes written (might
   be smaller than DATALEN) or -1 and the error number in errno.  If
   NONBLOCK is not zero, return with -1 and set errno to EWOULDBLOCK
   if operation would block for a long time.  */
ssize_t display_output (display_t display, int nonblock, char *data,
			size_t datalen);

mach_port_t display_get_filemap (display_t display, vm_prot_t prot);

ssize_t display_read (display_t display, int nonblock, off_t off,
		      char *data, size_t len);

error_t display_notice_changes (display_t display, mach_port_t notify);

/* Resume the output on the display DISPLAY.  */
void display_start_output (display_t display);

/* Stop all output on the display DISPLAY.  */
void display_stop_output (display_t display);

/* Return the number of pending output bytes for DISPLAY.  */
size_t display_pending_output (display_t display);

/* Flush the output buffer, discarding all pending data.  */
void display_discard_output (display_t display);

#endif	/* DISPLAY_H */
