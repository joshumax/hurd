/* Common output function for ps & w

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

#ifndef __PSOUT_H__
#define __PSOUT_H__

#include <ps.h>

void psout (struct proc_stat_list *procs,
	    char *fmt_string, int posix_fmt, struct ps_fmt_specs *specs,
	    char *sort_key_name, int sort_reverse,
	    int output_width, int print_heading,
	    int squash_bogus_fields, int squash_nominal_fields);

#endif /* __PSOUT_H__ */
