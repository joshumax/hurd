/* Remote (server-to-server) transfer

   Copyright (C) 1997, 1998 Free Software Foundation, Inc.

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
#include "priv.h"

/* Transfer the output of SRC_CMD/SRC_NAME on SRC_CONN to DST_NAME on
   DST_CONN, moving the data directly between servers.  */
error_t
ftp_conn_rmt_transfer (struct ftp_conn *src_conn,
		       const char *src_cmd, const char *src_name,
		       const int *src_poss_errs,
		       struct ftp_conn *dst_conn, const char *dst_name)
{
  struct sockaddr *src_addr;
  error_t err = ftp_conn_get_pasv_addr (src_conn, &src_addr);

  if (! err)
    {
      err = ftp_conn_send_actv_addr (dst_conn, src_addr);

      if (! err)
	{
	  int reply;
	  const char *txt;
	  err = ftp_conn_cmd (src_conn, src_cmd, src_name, 0, 0);

	  if (! err)
	    {
	      err = ftp_conn_cmd (dst_conn, "stor", dst_name, &reply, &txt);

	      if (! err)
		{
		  if (REPLY_IS_PRELIM (reply))
		    {
		      err = ftp_conn_get_reply (src_conn, &reply, &txt);
		      if (!err && !REPLY_IS_PRELIM (reply))
			err = unexpected_reply (src_conn, reply, txt,
						src_poss_errs);

		      if (err)
			ftp_conn_abort (dst_conn);
		      else
			err = ftp_conn_finish_transfer (dst_conn);
		    }
		  else
		    err = unexpected_reply (dst_conn, reply, txt,
					    ftp_conn_poss_file_errs);
		}
	      if (err)
		/* Ftp servers seem to hang trying to abort at this point, so
		   just close the connection entirely.  */
		ftp_conn_close (src_conn);
	      else
		err = ftp_conn_finish_transfer (src_conn);
	    }
	}

      free (src_addr);
    }

  return err;
}

/* Copy the SRC_NAME on SRC_CONN to DST_NAME on DST_CONN, moving the data
   directly between servers.  */
error_t
ftp_conn_rmt_copy (struct ftp_conn *src_conn, const char *src_name,
		   struct ftp_conn *dst_conn, const char *dst_name)
{
  return
    ftp_conn_rmt_transfer (src_conn, "retr", src_name, ftp_conn_poss_file_errs,
			   dst_conn, dst_name);
}
