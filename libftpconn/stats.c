/* Fetch file stats

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

#include <unistd.h>
#include <errno.h>

#include <ftpconn.h>

/* Start an operation to get a list of file-stat structures for NAME (this
   is often similar to ftp_conn_start_dir, but with OS-specific flags), and
   return a file-descriptor for reading on, and a state structure in STATE
   suitable for passing to cont_get_stats.  If CONTENTS is true, NAME must
   refer to a directory, and the contents will be returned, otherwise, the
   (single) result will refer to NAME.  */
error_t
ftp_conn_start_get_stats (struct ftp_conn *conn,
			  const char *name, int contents,
			  int *fd, void **state)
{
  if (conn->syshooks.start_get_stats)
    return
      (*conn->syshooks.start_get_stats) (conn, name, contents, fd, state);
  else
    return EOPNOTSUPP;
}

/* Read stats information from FD, calling ADD_STAT for each new stat (HOOK
   is passed to ADD_STAT).  FD and STATE should be returned from
   start_get_stats.  If this function returns EAGAIN, then it should be
   called again to finish the job (possibly after calling select on FD); if
   it returns 0, then it is finishe,d and FD and STATE are deallocated.  */
error_t
ftp_conn_cont_get_stats (struct ftp_conn *conn, int fd, void *state,
			 ftp_conn_add_stat_fun_t add_stat, void *hook)
{
  if (conn->syshooks.cont_get_stats)
    return (*conn->syshooks.cont_get_stats) (conn, fd, state, add_stat, hook);
  else
    return EOPNOTSUPP;
}

/* Get a list of file-stat structures for NAME, calling ADD_STAT for each one
   (HOOK is passed to ADD_STAT).  If CONTENTS is true, NAME must refer to a
   directory, and the contents will be returned, otherwise, the (single)
   result will refer to NAME.  This function may block.  */
error_t
ftp_conn_get_stats (struct ftp_conn *conn,
		    const char *name, int contents,
		    ftp_conn_add_stat_fun_t add_stat, void *hook)
{
  int fd;
  void *state;
  error_t err = ftp_conn_start_get_stats (conn, name, contents, &fd, &state);

  if (err)
    return err;

  do
    err = ftp_conn_cont_get_stats (conn, fd, state, add_stat, hook);
  while (err == EAGAIN);

  return err;
}
