/* Handy common functions for things in libps.

   Copyright (C) 1995, 1999 Free Software Foundation, Inc.

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

#include <sys/mman.h>

#define ABS(x) ((x) < 0 ? -(x) : (x))
#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* Allocate memory to store an element of type TYPE */
#define NEW(type) ((type *)malloc(sizeof(type)))
/* Allocate a vector of type TYPE *, consisting of LEN elements of type TYPE */

#define NEWVEC(type,len) ((type *)malloc(sizeof(type)*(len)))
/* Change the size of the vector OLD, of type TYPE *, to be LEN elements of type TYPE */
#define GROWVEC(old,type,len) \
    ((type *)realloc((void *)(old),(unsigned)(sizeof(type)*(len))))

#define FREE(x) (void)free((void *)x)
#define VMFREE(x, len) munmap((caddr_t)x, len)

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
