/* Some random handy memory ops that know about VM.

   Copyright (C) 1995 Free Software Foundation, Inc.

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

/* For bcopy &c.  */
#include <string.h>

/* Makes sure that BUF, points to a buffer with AMOUNT bytes available.
   *BUF_LEN should be the current length of *BUF, and if this isn't enough to
   hold AMOUNT bytes, then more is allocated and the new buffer is returned
   in *BUF and *BUF_LEN.  If a memory allocation error occurs, the error code
   is returned, otherwise 0.  */
error_t allocate(vm_address_t *buf, vm_size_t *buf_len, vm_size_t amount);

/* Deallocates any pages entirely within the last EXCESS bytes of the BUF_LEN
   long buffer, BUF.  */
error_t deallocate_excess(vm_address_t buf, vm_size_t buf_len, vm_size_t excess);
