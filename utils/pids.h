/* Pid parsing/frobbing

   Copyright (C) 1997,2001 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifndef __PIDS_H__
#define __PIDS_H__

/* Add the pids returned in vm_allocated memory by calling PIDS_FN with ID as
   an argument to PIDS and NUM_PIDS, reallocating it in malloced memory.  */
extern error_t add_fn_pids (pid_t **pids, size_t *num_pids, unsigned id,
			    error_t (*pids_fn)(process_t proc, pid_t id,
					       pid_t **pids, size_t *num_pids));

/* Add PID to PIDS and NUM_PIDS, reallocating it in malloced memory.  */
extern error_t add_pid (pid_t **pids, size_t *num_pids, pid_t pid);

/* Params to be passed as the input when parsing PIDS_ARGP.  */
struct pids_argp_params
{
  /* Array to be extended with parsed pids.  */
  pid_t **pids;
  size_t *num_pids;

  /* If true, parse non-option arguments as pids.  */
  int parse_pid_args;
};

/* A parser for selecting a set of pids.  */
extern struct argp pids_argp;

#endif /* __PIDS_H__ */
