/* Common interface for auth frobbing utilities

   Copyright (C) 1997 Free Software Foundation, Inc.

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

#ifndef __FROBAUTH_H__
#define __FROBAUTH_H__

#include <sys/types.h>
#include <ugids.h>
#include <argp.h>

/* Structure describing which processes to frob, and how to frob them.  */
struct frobauth
{
  struct ugids ugids;
  pid_t *pids;
  size_t num_pids;
  int verbose, dry_run;		/* User options */
  uid_t default_user;		/* If none specified; -1 means none.  */
  int require_ids;		/* If true, require some ids be specified. */
};

#define FROBAUTH_INIT { UGIDS_INIT, 0, 0, 0, 0, -1 }

/* For every pid in FROBAUTH, call MODIFY to change its argument UGIDS from
   the current authentication to what it should be; CHANGE is whatever ids
   the user specified.  AUTHS, of length NUM_AUTHS, should be a vector of
   auth ports giving whatever additional authentication is needed (besides
   the process's current authentication).  If the user specifies the
   --verbose flags, PRINT_INFO is called after successfully installing the
   new authentication in each process, to print a message about what
   happened.  True is returned if no errors occur, although most errors do
   not cause termination, and error messages are printed for them.  */
error_t frobauth_modify (struct frobauth *frobauth,
			 const auth_t *auths, size_t num_auths,
			 error_t (*modify) (struct ugids *ugids,
					    const struct ugids *change,
					    pid_t pid, void *hook),
			 void (*print_info) (const struct ugids *new,
					     const struct ugids *old,
					     const struct ugids *change,
					     pid_t pid, void *hook),
			 void *hook);

/* Parse frobauth args/options, where user args are added as single ids to
   either the effective or available ids.  */
extern struct argp frobauth_ea_argp;

/* Parse frobauth args/options, where user args are added as posix user.  */
extern struct argp frobauth_posix_argp;

/* Parse frobauth args/options, with no user specifications.  */
extern struct argp frobauth_no_ugids_argp;

#endif /* __FROBAUTH_H__ */
