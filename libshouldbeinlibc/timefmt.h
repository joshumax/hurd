/* Routines for formatting time

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

#ifndef __TIMEFMT_H__
#define __TIMEFMT_H__

struct timeval;

/* Format into BUF & BUF_LEN the time interval represented by TV, trying to
   make the result less than WIDTH characters wide.  The number of characters
   used is returned.  */
int fmt_named_interval (struct timeval *tv, int width,
			char *buf, unsigned buf_len);

/* Format into BUF & BUF_LEN the time interval represented by TV, using
   HH:MM:SS notation where possible, and trying to make the result less than
   WIDTH characters wide.  The number of characters used is returned.  */
int fmt_seconds (struct timeval *tv, unsigned width,
		 char *buf, unsigned buf_len);

/* Format into BUF & BUF_LEN the time interval represented by TV, using
   HH:MM notation where possible, and trying to make the result less than
   WIDTH characters wide.  The number of characters used is returned.  */
int fmt_minutes (struct timeval *tv, unsigned width,
		 char *buf, unsigned buf_len);

#endif /* __TIMEFMT_H__ */
