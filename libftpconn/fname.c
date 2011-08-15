/* Filename frobbing

   Copyright (C) 1997 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <string.h>

#include "ftpconn.h"

/* Give a name which refers to a directory file, and a name in that
   directory, this should return in COMPOSITE the composite name referring to
   that name in that directory, in malloced storage.  */
error_t
ftp_conn_append_name (struct ftp_conn *conn,
		      const char *dir, const char *name,
		      char **composite)
{
  error_t err = ftp_conn_validate_syshooks (conn);
  if (err)
    return err;
  else if (conn->syshooks.append_name)
    return (*conn->syshooks.append_name) (conn, dir, name, composite);
  else
    return EOPNOTSUPP;
}

/* If the name of a file COMPOSITE is a composite name (containing both a
   filename and a directory name), this function will return the name
   component only in BASE, in malloced storage, otherwise it simply returns a
   newly malloced copy of COMPOSITE in BASE.  */
error_t
ftp_conn_basename (struct ftp_conn *conn, const char *composite, char **base)
{
  error_t err = ftp_conn_validate_syshooks (conn);

  if (err)
    return err;

  if (conn->syshooks.basename)
    {
      size_t in_size = strlen (composite) + 1;
      char *in = strdup (composite), *out = in;

      if (! in)
	return ENOMEM;

      err = (*conn->syshooks.basename) (conn, &out);
      if (err || out != in)
	{
	  if (!err && out >= in && out < in + in_size)
	    /* OUT uses storage from IN, but not at the beginning.  */
	    out = strdup (out);
	  free (in);
	}

      if (! err)
	*base = out;

      return err;
    }
  else
    return EOPNOTSUPP;
}
