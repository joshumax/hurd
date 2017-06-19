/* Ftp connection management

   Copyright (C) 1997,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>
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

#include <assert-backtrace.h>
#include <stdint.h>

#include "ftpfs.h"

/* A particular connection.  */
struct ftpfs_conn
{
  struct ftp_conn *conn;
  struct ftpfs_conn *next;
};

/* For debugging purposes, give each connection a unique integer id.  */
static unsigned conn_id = 0;

/* Get an ftp connection to use for an operation. */
error_t
ftpfs_get_ftp_conn (struct ftpfs *fs, struct ftp_conn **conn)
{
  struct ftpfs_conn *fsc;

  pthread_spin_lock (&fs->conn_lock);
  fsc = fs->free_conns;
  if (fsc)
    fs->free_conns = fsc->next;
  pthread_spin_unlock (&fs->conn_lock);

  if (! fsc)
    {
      error_t err;

      fsc = malloc (sizeof (struct ftpfs_conn));
      if (! fsc)
	return ENOMEM;

      err = ftp_conn_create (fs->ftp_params, fs->ftp_hooks, &fsc->conn);

      if (! err)
	{
	  /* Set connection type to binary.  */
	  err = ftp_conn_set_type (fsc->conn, "I");
	  if (err)
	    ftp_conn_free (fsc->conn);
	}

      if (err)
	{
	  free (fsc);
	  return err;
	}

      /* For debugging purposes, give each connection a unique integer id.  */
      fsc->conn->hook = (void *)(uintptr_t)conn_id++;
    }

  pthread_spin_lock (&fs->conn_lock);
  fsc->next = fs->conns;
  fs->conns = fsc;
  pthread_spin_unlock (&fs->conn_lock);

  *conn = fsc->conn;

  return 0;
}

/* Return CONN to the pool of free connections in FS.  */
void
ftpfs_release_ftp_conn (struct ftpfs *fs, struct ftp_conn *conn)
{
  struct ftpfs_conn *fsc, *pfsc;

  pthread_spin_lock (&fs->conn_lock);
  for (pfsc = 0, fsc = fs->conns; fsc; pfsc = fsc, fsc = fsc->next)
    if (fsc->conn == conn)
      {
	if (pfsc)
	  pfsc->next = fsc->next;
	else
	  fs->conns = fsc->next;
	fsc->next = fs->free_conns;
	fs->free_conns = fsc;
	break;
      }
  assert_backtrace (fsc);
  pthread_spin_unlock (&fs->conn_lock);
}
