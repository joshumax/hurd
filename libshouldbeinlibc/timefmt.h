/* Routines for formatting time

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

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
size_t fmt_named_interval (struct timeval *tv, size_t width,
			   char *buf, size_t buf_len);

/* Format into BUF & BUF_LEN the time interval represented by TV, using
   HH:MM:SS notation where possible, with FRAC_PLACES digits after the
   decimal point, and trying to make the result less than WIDTH characters
   wide.  If LEADING_ZEROS is true, then any fields that are zero-valued, but
   would fit in the given width are printed.  If FRAC_PLACES is negative,
   then any space remaining after printing the time, up to WIDTH, is used for
   the fraction.  The number of characters used is returned.  */
size_t fmt_seconds (struct timeval *tv, int leading_zeros, int frac_places,
		    size_t width, char *buf, size_t buf_len);

/* Format into BUF & BUF_LEN the time interval represented by TV, using HH:MM
   notation where possible, and trying to make the result less than WIDTH
   characters wide.  If LEADING_ZEROS is true, then any fields that are
   zero-valued, but would fit in the given width are printed.  The number of
   characters used is returned.  */
size_t fmt_minutes (struct timeval *tv, int leading_zeros,
		    size_t width, char *buf, size_t buf_len);

/* Format into BUF & BUF_LEN the absolute time represented by TV.  An attempt
   is made to fit the result in less than WIDTH characters, by omitting
   fields implied by the current time, NOW (if NOW is 0, then no assumptions
   are made, so the resulting times will be somewhat long).  The number of
   characters used is returned.  */
size_t fmt_past_time (struct timeval *tv, struct timeval *now,
		      size_t width, char *buf, size_t buf_len);

#endif /* __TIMEFMT_H__ */
