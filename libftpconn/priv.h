/* libftpconn private definitions

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

#ifndef __FTPCONN_PRIV_H__
#define __FTPCONN_PRIV_H__

#include <features.h>

#ifdef FTP_CONN_DEFINE_EI
#define FTP_CONN_EI
#else
#define FTP_CONN_EI __extern_inline
#endif

/* Ftp reply codes.  */
#define REPLY_DELAY	120	/* Service ready in nnn minutes */

#define REPLY_OK	200	/* Command OK */
#define REPLY_SYSTYPE	215	/* NAME version */
#define REPLY_HELLO	220	/* Service ready for new user */
#define REPLY_ABORT_OK	225	/* ABOR command successful */
#define REPLY_TRANS_OK	226	/* Closing data connection; requested file
				   action successful */
#define REPLY_PASV_OK	227	/* Entering passive mode */
#define REPLY_LOGIN_OK	230	/* User logged in, proceed */
#define REPLY_FCMD_OK	250	/* Requested file action okay, completed */
#define REPLY_DIR_NAME	257	/* "DIR" msg */

#define REPLY_NEED_PASS	331	/* User name okay, need password */
#define REPLY_NEED_ACCT 332	/* Need account for login */

#define REPLY_CLOSED	421	/* Service not available, closing control connection */
#define REPLY_ABORTED	426	/* Connection closed; transfer aborted */

#define REPLY_BAD_CMD	500	/* Syntax error; command unrecognized */
#define REPLY_BAD_ARG	501	/* Synax error in parameters or arguments */
#define REPLY_UNIMP_CMD	502	/* Command not implemented */
#define REPLY_UNIMP_ARG	504	/* Command not implemented for that parameter */

#define REPLY_NO_LOGIN	530	/* Not logged in */
#define REPLY_NO_ACCT	532	/* Need account for storing files */
#define REPLY_NO_SPACE	552	/* Requested file action aborted
				   Exceeded storage allocation */

#define REPLY_IS_PRELIM(rep) ((rep) >= 100 && (rep) < 200)
#define REPLY_IS_SUCCESS(rep) ((rep) >= 200 && (rep) < 300)
#define REPLY_IS_INCOMPLETE(rep) ((rep) >= 300 && (rep) < 400)
#define REPLY_IS_TRANSIENT(rep) ((rep) >= 400 && (rep) < 500)
#define REPLY_IS_FAILURE(rep) ((rep) >= 500 && (rep) < 600)

extern error_t unexpected_reply (struct ftp_conn *conn, int reply, const char *reply_txt,
		  const error_t *poss_errs);
#if defined(__USE_EXTERN_INLINES) || defined(FTP_CONN_DEFINE_EI)
FTP_CONN_EI error_t
unexpected_reply (struct ftp_conn *conn, int reply, const char *reply_txt,
		  const error_t *poss_errs)
{
  if (reply == REPLY_CLOSED)
    return EPIPE;
  else if (reply == REPLY_UNIMP_CMD || reply == REPLY_UNIMP_ARG)
    return EOPNOTSUPP;
  else if (reply == REPLY_BAD_ARG)
    return EINVAL;
  else if (REPLY_IS_FAILURE (reply) && reply_txt
	   && conn->syshooks.interp_err && poss_errs)
    return (*conn->syshooks.interp_err) (conn, reply_txt, poss_errs);
  else if (REPLY_IS_TRANSIENT (reply))
    return EAGAIN;
  else
    return EGRATUITOUS;
}
#endif /* Use extern inlines.  */

/* Error codes we think may result from file operations we do.  */
extern const error_t ftp_conn_poss_file_errs[];

error_t ftp_conn_get_pasv_addr (struct ftp_conn *conn, struct sockaddr **addr);

error_t ftp_conn_send_actv_addr (struct ftp_conn *conn, struct sockaddr *addr);

#endif /* __FTPCONN_PRIV_H__ */
