/* random.c - A single-file translator providing random data
   Copyright (C) 1998, 1999, 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef __RANDOM_H__
#define __RANDOM_H__

/* How many random bytes to gather at most.
   XXX: Should be at least POOLSIZE.  */
#define GATHERBUFSIZE 32768

/* The random bytes we collected.  */
extern char gatherbuf[GATHERBUFSIZE];

/* The current positions in gatherbuf[].  */
extern int gatherrpos;
extern int gatherwpos;

#endif
