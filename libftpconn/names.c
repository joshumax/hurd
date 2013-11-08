/* Fetch directory file names

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
#include <string.h>
#include <errno.h>

#include <ftpconn.h>

struct get_names_state
{
  char *name;			/* Last read (maybe partial) name.  */
  size_t name_len;		/* Valid length of NAME, *not including* '\0'.  */
  size_t name_alloced;		/* Allocated size of NAME (>= NAME_LEN).  */
  int name_partial;		/* True if NAME isn't complete.  */

  size_t buf_len;		/* Length of contents in BUF.  */
  char buf[7000];
};

/* Start an operation to get a list of filenames in the directory NAME, and
   return a file-descriptor for reading on, and a state structure in STATE
   suitable for passing to cont_get_names.  */
error_t
ftp_conn_start_get_names (struct ftp_conn *conn,
			  const char *name, int *fd, void **state)
{
  error_t err;
  struct get_names_state *s = malloc (sizeof (struct get_names_state));

  if (! s)
    return ENOMEM;

  err = ftp_conn_start_list (conn, name, fd);

  if (err)
    free (s);
  else
    {
      s->name = 0;
      s->name_len = s->name_alloced = 0;
      s->name_partial = 0;
      s->buf_len = 0;
      *state = s;
    }

  return err;
}

/* Read filenames from FD, calling ADD_NAME for each new NAME (HOOK is passed
   to ADD_NAME).  FD and STATE should be returned from start_get_names.  If
   this function returns EAGAIN, then it should be called again to finish the
   job (possibly after calling select on FD); if it returns 0, then it is
   finishe,d and FD and STATE are deallocated.  */
error_t
ftp_conn_cont_get_names (struct ftp_conn *conn, int fd, void *state,
			 ftp_conn_add_name_fun_t add_name, void *hook)
{
  char *p, *nl;
  ssize_t rd;
  size_t name_len;
  error_t err = 0;
  struct get_names_state *s = state;
  int (*icheck) (struct ftp_conn *conn) = conn->hooks->interrupt_check;

  /* We always consume full lines, so we know that we have to read more when
     we first get called.  */
  rd = read (fd, s->buf + s->buf_len, sizeof (s->buf) - s->buf_len);
  if (rd < 0)
    {
      err = errno;
      goto finished;
    }

  if (icheck && (*icheck) (conn))
    {
      err = EINTR;
      goto finished;
    }

  if (rd == 0)
    /* EOF */
    if (s->buf_len == 0)
      /* We're done!  Clean up and return the result in NAMES.  */
      goto finished;
    else
      /* Partial line at end of file?  */
      nl = s->buf + s->buf_len;
  else
    /* Look for a new line in what we read (we know that there weren't any in
       the buffer before that).  */
    {
      nl = memchr (s->buf + s->buf_len, '\n', rd);
      s->buf_len += rd;
    }

  if (!nl && s->buf_len < sizeof (s->buf))
    /* We didn't find any newlines (which implies we didn't hit EOF), and we
       still have room to grow the buffer, so just wait until next time to do
       anything.  */
    return EAGAIN;

  /* Where we start parsing.  */
  p = s->buf;

  do
    {
      /* Fill in S->name, possibly extending it from a previous buffer.  */
      name_len = (nl ? nl - p : s->buf + s->buf_len - p);
      if (name_len > 0 && p[name_len - 1] == '\r')
	name_len--;
      if (name_len > 0)
	/* Extending s->name.  */
	{
	  size_t old_len = s->name_len;
	  size_t total_len = old_len + name_len + 1;

	  if (total_len > s->name_alloced)
	    {
	      char *new_name = realloc (s->name, total_len);
	      if (! new_name)
		goto enomem;
	      s->name = new_name;
	      s->name_alloced = total_len;
	    }

	  strncpy (s->name + old_len, p, name_len);
	  s->name[old_len + name_len] = '\0';
	  s->name_len = total_len - 1;
	}

      if (nl)
	{
	  char *name = s->name;

	  if (conn->syshooks.basename)
	    /* Fixup any screwy names returned by the server.  */
	    {
	      err = (*conn->syshooks.basename) (conn, &name);
	      if (err)
		goto finished;
	    }

	  /* Call the callback function to process the current entry.  */
	  err = (*add_name) (name, hook);

	  if (name < s->name || name > s->name + s->name_len)
	    /* User-allocated NAME from the fix_nlist_name hook.  */
	    free (name);

	  if (err)
	    goto finished;

	  s->name_len = 0;
	  s->name_partial = 0;

	  p = nl + 1;
	  nl = memchr (p, '\n', s->buf + s->buf_len - p);
	}
      else
	/* We found no newline, so the name extends past what we read; we'll
	   try to read more next time.  */
	{
	  s->name_partial = 1;
	  /* Skip over the partial name for the next iteration.  */
	  p += name_len;
	}
    }
  while (nl);

  /* Move any remaining characters in the buffer to the beginning for the
     next call.  */
  s->buf_len -= (p - s->buf);
  if (s->buf_len > 0)
    memmove (s->buf, p, s->buf_len);

  /* Try again later.  */
  return EAGAIN;

enomem:
  /* Some memory allocation failed.  */
  err = ENOMEM;

finished:
  /* We're finished (with an error if ERR != 0), deallocate everything &
     return.  */
  if (s->name)
    free (s->name);
  free (s);
  close (fd);

  if (err && rd > 0)
    ftp_conn_abort (conn);
  else if (err)
    ftp_conn_finish_transfer (conn);
  else
    err = ftp_conn_finish_transfer (conn);

  return err;
}

/* Get a list of names in the directory NAME, calling ADD_NAME for each one
   (HOOK is passed to ADD_NAME).  This function may block.  */
error_t
ftp_conn_get_names (struct ftp_conn *conn, const char *name, 
		    ftp_conn_add_name_fun_t add_name, void *hook)
{
  int fd;
  void *state;
  error_t err = ftp_conn_start_get_names (conn, name, &fd, &state);

  if (err)
    return err;

  do
    err = ftp_conn_cont_get_names (conn, fd, state, add_name, hook);
  while (err == EAGAIN);

  return err;
}
