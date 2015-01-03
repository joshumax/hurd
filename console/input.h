/* input.h - Interface to the input component of a virtual console.
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

#ifndef INPUT_H
#define INPUT_H

#include <errno.h>

struct input;
typedef struct input *input_t;

/* Create a new virtual console input queue, with the system encoding
   being ENCODING.  */
error_t input_create (input_t *r_input, const char *encoding);

/* Destroy the input queue INPUT.  */
void input_destroy (input_t input);

/* Enter DATALEN characters from the buffer DATA into the input queue
   INPUT.  The DATA must be supplied in UTF-8 encoding (XXX UCS-4
   would be nice, too, but it requires knowledge of endianness).  The
   function returns the amount of bytes written (might be smaller than
   DATALEN) or -1 and the error number in errno.  If NONBLOCK is not
   zero, return with -1 and set errno to EWOULDBLOCK if operation
   would block for a long time.  */
ssize_t input_enqueue (input_t input, int nonblock, char *data,
		       size_t datalen);

/* Remove DATALEN characters from the input queue and put them in the
   buffer DATA.  The data will be supplied in the local encoding.  The
   function returns the amount of bytes removed (might be smaller than
   DATALEN) or -1 and the error number in errno.  If NONBLOCK is not
   zero, return with -1 and set errno to EWOULDBLOCK if operation
   would block for a long time.  */
ssize_t input_dequeue (input_t input, int nonblock, char *data,
		       size_t datalen);

/* Flush the input buffer, discarding all pending data.  */
void input_flush (input_t input);

#endif	/* INPUT_H */
