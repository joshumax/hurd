/* bell.h - The interface to and for a bell driver.
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

#ifndef _BELL_H_
#define _BELL_H_ 1

#include <errno.h>


/* The bell drivers are set up by the driver's initialization routine
   and added to the console client with driver_add_bell.  All
   subsequent operations on the display are fully synchronized by the
   caller.  The driver deinitialization routine should call
   driver_remove_bell.  */

/* Forward declaration.  */
struct bell_ops;
typedef struct bell_ops *bell_ops_t;

/* Add the bell HANDLE with the operations OPS to the console client.
   As soon as this is called, operations on this bell may be
   performed, even before the function returns.  */
error_t driver_add_bell (bell_ops_t ops, void *handle);

/* Remove the bell HANDLE with the operations OPS from the console
   client.  As soon as this function returns, no operations will be
   performed on the bell anymore.  */
error_t driver_remove_bell (bell_ops_t ops, void *handle);

struct bell_ops
{
  /* Beep! the bell HANDLE.  */
  error_t (*beep) (void *handle);

  /* Do not use, do not remove.  */
  void (*deprecated) (void *handle, unsigned int key);
};

#endif	/* _BELL_H_ */
