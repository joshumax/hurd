/* 
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

/* This library supports client-side network file system
   implementations.  It is analogous to the diskfs library provided for 
   disk-based filesystems.  */


/* The user must define this function.  Request the operation encoded
   in OPERATION.  Return a 32-bit unique ID for the operation.  When
   the operation completes, call netfs_operation_complete. */
int netfs_start_operation (struct operation *operation);

/* When an operation has completed, call this routine with the same
   32-bit ID that was used in the call to netfs_start_operation.  (The
   ID will be validated by the library, so there is no need for the
   user to check it if it is externally generated.)  */
void netfs_operation_complete (int id);


