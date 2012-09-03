/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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

/* This file contains stubs for some cthreads functions.
   It should only get used if the user isn't otherwise using cthreads. */

#if 0
#include <cthreads.h>

void condition_wait (condition_t c, mutex_t m) __attribute__ ((weak));
     
void
condition_wait (condition_t c, mutex_t m)
{
}

void cond_broadcast (condition_t c) __attribute__ ((weak));

void
cond_broadcast (condition_t c)
{
}
#endif
