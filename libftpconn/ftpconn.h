/* Manage an ftp connection

   Copyright (C) 1997,2001,02 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef __FTPCONN_H__
#define __FTPCONN_H__

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <features.h>

#define __need_error_t
#include <errno.h>

#ifndef __error_t_defined
typedef int error_t;
#define __error_t_defined
#endif

#ifdef FTP_CONN_DEFINE_EI
#define FTP_CONN_EI
#else
#define FTP_CONN_EI __extern_inline
#endif

struct ftp_conn;
struct ftp_conn_params;
struct ftp_conn_stat;

/* The type of the function called by ...get_stats to add each new stat.
   NAME is the file in question, STAT is stat info about it, and if NAME is a
   symlink, SYMLINK_TARGET is what it is linked to, or 0 if it's not a
   symlink.  NAME and SYMLINK_TARGET should be copied if they are used
   outside of this function.  HOOK is as passed into ...get_stats.  */
typedef error_t (*ftp_conn_add_stat_fun_t) (const char *name,
# if _FILE_OFFSET_BITS == 64
					    const struct stat *stat,
# else
					    const struct stat64 *stat,
# endif
					    const char *symlink_target,
					    void *hook);

/* Hooks that customize behavior for particular types of remote system.  */
struct ftp_conn_syshooks
{
  /* Should return in ADDR a malloced struct sockaddr containing the address
     of the host referenced by the PASV reply contained in TXT.  */
  error_t (*pasv_addr) (struct ftp_conn *conn, const char *txt,
			struct sockaddr **addr);

  /* Look at the error string in TXT, and try to guess an error code to
     return.  If POSS_ERRS is non-zero, it contains a list of errors
     that are likely to occur with the previous command, terminated with 0.
     If no match is found and POSS_ERRS is non-zero, the first error in
     POSS_ERRS should be returned by default.  */
  error_t (*interp_err) (struct ftp_conn *conn, const char *txt,
			 const error_t *poss_errs);

  /* Start an operation to get a list of file-stat structures for NAME (this
     is often similar to ftp_conn_start_dir, but with OS-specific flags), and
     return a file-descriptor for reading on, and a state structure in STATE
     suitable for passing to cont_get_stats.  If CONTENTS is true, NAME must
     refer to a directory, and the contents will be returned, otherwise, the
     (single) result will refer to NAME.  */
  error_t (*start_get_stats) (struct ftp_conn *conn, const char *name,
			      int contents, int *fd, void **state);

  /* Read stats information from FD, calling ADD_STAT for each new stat (HOOK
     is passed to ADD_STAT).  FD and STATE should be returned from
     start_get_stats.  If this function returns EAGAIN, then it should be
     called again to finish the job (possibly after calling select on FD); if
     it returns 0, then it is finishe,d and FD and STATE are deallocated.  */
  error_t (*cont_get_stats) (struct ftp_conn *conn, int fd, void *state,
			     ftp_conn_add_stat_fun_t add_stat, void *hook);

  /* Give a name which refers to a directory file, and a name in that
     directory, this should return in COMPOSITE the composite name referring
     to that name in that directory, in malloced storage.  */
  error_t (*append_name) (struct ftp_conn *conn,
			  const char *dir, const char *name,
			  char **composite);

  /* If the name of a file *NAME is a composite name (containing both a
     filename and a directory name), this function should change *NAME to be
     the name component only; if the result is shorter than the original
     *NAME, the storage pointed to it may be modified, otherwise, *NAME
     should be changed to point to malloced storage holding the result, which
     will be freed by the caller.  */
  error_t (*basename) (struct ftp_conn *conn, char **name);
};

/* Type parameter for the cntl_debug hook.  */
#define FTP_CONN_CNTL_DEBUG_CMD		1
#define FTP_CONN_CNTL_DEBUG_REPLY	2

/* Type parameter for the get_login_param hook.  */
#define FTP_CONN_GET_LOGIN_PARAM_USER	1
#define FTP_CONN_GET_LOGIN_PARAM_PASS	2
#define FTP_CONN_GET_LOGIN_PARAM_ACCT	3

/* General connection customization.  */
struct ftp_conn_hooks
{
  /* If non-zero, should look at the SYST reply in SYST, and fill in CONN's
     syshooks (with ftp_conn_set_hooks) appropriately; SYST may be zero if
     the remote system doesn't support that command.  If zero, then the
     default ftp_conn_choose_syshooks is used.  */
  void (*choose_syshooks) (struct ftp_conn *conn, const char *syst);

  /* If non-zero, called during io on the ftp control connection -- TYPE is
     FTP_CONN_CNTL_DEBUG_CMD for commands, and FTP_CONN_CNTL_DEBUG_REPLY for
     replies; TXT is the actual text.  */
  void (*cntl_debug) (struct ftp_conn *conn, int type, const char *txt);

  /* Called after CONN's connection the server has been opened (or reopened).  */
  void (*opened) (struct ftp_conn *conn);

  /* If the remote system requires some login parameter that isn't available,
     this hook is called to try and get it, returning a value in TXT.  The
     return value should be in a malloced block of memory.  The returned
     value will only be used once; if it's desired that it should `stick',
     the user may modify the value stored in CONN's params field, but that is
     an issue outside of the scope of this interface -- params are only read,
     never written.  */
  error_t (*get_login_param) (struct ftp_conn *conn, int type, char **txt);

  /* Called after CONN's connection the server has closed for some reason.  */
  void (*closed) (struct ftp_conn *conn);

  /* Called when CONN is initially created before any other hook calls.  An
     error return causes the creation to fail with that error code.  */
  error_t (*init) (struct ftp_conn *conn);

  /* Called when CONN is about to be destroyed.  No hook calls are ever made
     after this one.  */
  void (*fini) (struct ftp_conn *conn);

  /* This hook should return true if the current thread has been interrupted
     in some way, and EINTR (or a short count in some cases) should be
     returned from a blocking function.  */
  int (*interrupt_check) (struct ftp_conn *conn);
};

/* A single ftp connection.  */
struct ftp_conn
{
  const struct ftp_conn_params *params;	/* machine, user, &c */
  const struct ftp_conn_hooks *hooks; /* Customization hooks. */

  struct ftp_conn_syshooks syshooks; /* host-dependent hook functions */
  int syshooks_valid : 1;	/* True if the system type has been determined. */

  int control;			/* fd for ftp control connection */

  char *line;			/* buffer for reading control replies */
  size_t line_sz;		/* allocated size of LINE */
  size_t line_offs;		/* Start of unread input in LINE.  */
  size_t line_len;		/* End of the contents in LINE.  */

  char *reply_txt;		/* A buffer for the text of entire replies */
  size_t reply_txt_sz;		/* size of it */

  char *cwd;			/* Last know CWD, or 0 if unknown.  */
  const char *type;		/* Connection type, or 0 if default.  */

  void *hook;			/* Random user data. */

  int use_passive : 1;		/* If true, first try passive data conns.  */

  struct sockaddr *actv_data_addr;/* Address of port for active data conns.  */
};

/* Parameters for an ftp connection; doesn't include any actual connection
   state.  */
struct ftp_conn_params
{
  void *addr;			/* Address.  */
  size_t addr_len;		/* Length in bytes of ADDR.  */
  int addr_type;		/* Type of ADDR (AF_*).  */

  char *user, *pass, *acct;	/* Parameters for logging into ftp.  */
};

/* Unix hooks */
extern error_t ftp_conn_unix_pasv_addr (struct ftp_conn *conn, const char *txt,
					struct sockaddr **addr);
extern error_t ftp_conn_unix_interp_err (struct ftp_conn *conn, const char *txt,
					 const error_t *poss_errs);
extern error_t ftp_conn_unix_start_get_stats (struct ftp_conn *conn,
					      const char *name,
					      int contents, int *fd,
					      void **state);
extern error_t ftp_conn_unix_cont_get_stats (struct ftp_conn *conn,
					     int fd, void *state,
					     ftp_conn_add_stat_fun_t add_stat,
					     void *hook);
error_t ftp_conn_unix_append_name (struct ftp_conn *conn,
				   const char *dir, const char *name,
				   char **composite);
error_t ftp_conn_unix_basename (struct ftp_conn *conn, char **name);

extern struct ftp_conn_syshooks ftp_conn_unix_syshooks;

error_t
ftp_conn_get_raw_reply (struct ftp_conn *conn,
			int *reply, const char **reply_txt);
error_t
ftp_conn_get_reply (struct ftp_conn *conn, int *reply, const char **reply_txt);

error_t
ftp_conn_cmd (struct ftp_conn *conn, const char *cmd, const char *arg,
	       int *reply, const char **reply_txt);

error_t
ftp_conn_cmd_reopen (struct ftp_conn *conn, const char *cmd, const char *arg,
		      int *reply, const char **reply_txt);

void ftp_conn_abort (struct ftp_conn *conn);

/* Sets CONN's syshooks to a copy of SYSHOOKS.  */
void ftp_conn_set_syshooks (struct ftp_conn *conn,
			    struct ftp_conn_syshooks *syshooks);

error_t ftp_conn_open (struct ftp_conn *conn);

void ftp_conn_close (struct ftp_conn *conn);

extern error_t ftp_conn_validate_syshooks (struct ftp_conn *conn);

#if defined(__USE_EXTERN_INLINES) || defined(FTP_CONN_DEFINE_EI)
/* Makes sure that CONN's syshooks are set according to the remote system
   type.  */
FTP_CONN_EI error_t
ftp_conn_validate_syshooks (struct ftp_conn *conn)
{
  if (conn->syshooks_valid)
    return 0;
  else
    /* Opening the connection should set the syshooks.  */
    return ftp_conn_open (conn);
}
#endif /* Use extern inlines.  */

/* Create a new ftp connection as specified by PARAMS, and return it in CONN;
   HOOKS contains customization hooks used by the connection.  Neither PARAMS
   nor HOOKS is copied, so a copy of it should be made if necessary before
   calling this function; if it should be freed later, a FINI hook may be
   used to do so.  */
error_t ftp_conn_create (const struct ftp_conn_params *params,
			 const struct ftp_conn_hooks *hooks,
			 struct ftp_conn **conn);

/* Free the ftp connection CONN, closing it first, and freeing all resources
   it uses.  */
void ftp_conn_free (struct ftp_conn *conn);

/* Start a transfer command CMD (and optional args ...), returning a file
   descriptor in DATA.  POSS_ERRS is a list of errnos to try matching
   against any resulting error text.  */
error_t
ftp_conn_start_transfer (struct ftp_conn *conn,
			 const char *cmd, const char *arg,
			 const error_t *poss_errs,
			 int *data);

/* Wait for the reply signalling the end of a data transfer.  */
error_t ftp_conn_finish_transfer (struct ftp_conn *conn);

/* Start retreiving file NAME over CONN, returning a file descriptor in DATA
   over which the data can be read.  */
error_t ftp_conn_start_retrieve (struct ftp_conn *conn, const char *name, int *data);

/* Start retreiving a list of files in NAME over CONN, returning a file
   descriptor in DATA over which the data can be read.  */
error_t ftp_conn_start_list (struct ftp_conn *conn, const char *name, int *data);

/* Start retreiving a directory listing of NAME over CONN, returning a file
   descriptor in DATA over which the data can be read.  */
error_t ftp_conn_start_dir (struct ftp_conn *conn, const char *name, int *data);

/* Start storing into file NAME over CONN, returning a file descriptor in DATA
   into which the data can be written.  */
error_t ftp_conn_start_store (struct ftp_conn *conn, const char *name, int *data);

/* Transfer the output of SRC_CMD/SRC_NAME on SRC_CONN to DST_NAME on
   DST_CONN, moving the data directly between servers.  */
error_t
ftp_conn_rmt_transfer (struct ftp_conn *src_conn,
		       const char *src_cmd, const char *src_name,
		       const int *src_poss_errs,
		       struct ftp_conn *dst_conn, const char *dst_name);

/* Copy the SRC_NAME on SRC_CONN to DST_NAME on DST_CONN, moving the data
   directly between servers.  */
error_t
ftp_conn_rmt_copy (struct ftp_conn *src_conn, const char *src_name,
		   struct ftp_conn *dst_conn, const char *dst_name);

/* Return a malloced string containing CONN's working directory in CWD.  */
error_t ftp_conn_get_cwd (struct ftp_conn *conn, char **cwd);

/* Return a malloced string containing CONN's working directory in CWD.  */
error_t ftp_conn_cwd (struct ftp_conn *conn, const char *cwd);

/* Return a malloced string containing CONN's working directory in CWD.  */
error_t ftp_conn_cdup (struct ftp_conn *conn);

/* Set the ftp connection type of CONN to TYPE, or return an error.  */
error_t ftp_conn_set_type (struct ftp_conn *conn, const char *type);

/* Start an operation to get a list of file-stat structures for NAME (this
   is often similar to ftp_conn_start_dir, but with OS-specific flags), and
   return a file-descriptor for reading on, and a state structure in STATE
   suitable for passing to cont_get_stats.  If CONTENTS is true, NAME must
   refer to a directory, and the contents will be returned, otherwise, the
   (single) result will refer to NAME.  */
error_t ftp_conn_start_get_stats (struct ftp_conn *conn,
				  const char *name, int contents,
				  int *fd, void **state);

/* Read stats information from FD, calling ADD_STAT for each new stat (HOOK
   is passed to ADD_STAT).  FD and STATE should be returned from
   start_get_stats.  If this function returns EAGAIN, then it should be
   called again to finish the job (possibly after calling select on FD); if
   it returns 0, then it is finishe,d and FD and STATE are deallocated.  */
error_t ftp_conn_cont_get_stats (struct ftp_conn *conn, int fd, void *state,
				 ftp_conn_add_stat_fun_t add_stat, void *hook);

/* Get a list of file-stat structures for NAME, calling ADD_STAT for each one
   (HOOK is passed to ADD_STAT).  If CONTENTS is true, NAME must refer to a
   directory, and the contents will be returned, otherwise, the (single)
   result will refer to NAME.  This function may block.  */
error_t ftp_conn_get_stats (struct ftp_conn *conn,
			    const char *name, int contents,
			    ftp_conn_add_stat_fun_t add_stat, void *hook);

/* The type of the function called by ...get_names to add each new name.
   NAME is the name in question and HOOK is as passed into ...get_stats.  */
typedef error_t (*ftp_conn_add_name_fun_t) (const char *name, void *hook);

/* Start an operation to get a list of filenames in the directory NAME, and
   return a file-descriptor for reading on, and a state structure in STATE
   suitable for passing to cont_get_names.  */
error_t ftp_conn_start_get_names (struct ftp_conn *conn,
				  const char *name, int *fd, void **state);

/* Read filenames from FD, calling ADD_NAME for each new NAME (HOOK is passed
   to ADD_NAME).  FD and STATE should be returned from start_get_stats.  If
   this function returns EAGAIN, then it should be called again to finish the
   job (possibly after calling select on FD); if it returns 0, then it is
   finishe,d and FD and STATE are deallocated.  */
error_t ftp_conn_cont_get_names (struct ftp_conn *conn, int fd, void *state,
				 ftp_conn_add_name_fun_t add_name, void *hook);

/* Get a list of names in the directory NAME, calling ADD_NAME for each one
   (HOOK is passed to ADD_NAME).  This function may block.  */
error_t ftp_conn_get_names (struct ftp_conn *conn, const char *name,
			    ftp_conn_add_name_fun_t add_name, void *hook);

/* Give a name which refers to a directory file, and a name in that
   directory, this should return in COMPOSITE the composite name referring to
   that name in that directory, in malloced storage.  */
error_t ftp_conn_append_name (struct ftp_conn *conn,
			      const char *dir, const char *name,
			      char **composite);

/* If the name of a file COMPOSITE is a composite name (containing both a
   filename and a directory name), this function will return the name
   component only in BASE, in malloced storage, otherwise it simply returns a
   newly malloced copy of COMPOSITE in BASE.  */
error_t ftp_conn_basename (struct ftp_conn *conn,
			   const char *composite, char **base);

#endif /* __FTPCONN_H__ */
