/* Unix-specific ftpconn hooks

   Copyright (C) 1997, 1998, 2002 Free Software Foundation, Inc.
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

#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <libgen.h> /* For dirname().  */
#ifdef HAVE_HURD_HURD_TYPES_H
#include <hurd/hurd_types.h>
#endif

#include <ftpconn.h>

/* Uid/gid to use when we don't know about a particular user name.  */
#define DEFAULT_UID 65535
#define DEFAULT_GID 65535

struct ftp_conn_syshooks ftp_conn_unix_syshooks = {
  ftp_conn_unix_pasv_addr, ftp_conn_unix_interp_err,
  ftp_conn_unix_start_get_stats, ftp_conn_unix_cont_get_stats,
  ftp_conn_unix_append_name, ftp_conn_unix_basename
};

/* Try to get an internet address out of the reply to a PASV command.
   Unfortunately, the format of the reply such isn't standardized.  */
error_t
ftp_conn_unix_pasv_addr (struct ftp_conn *conn, const char *txt,
			 struct sockaddr **addr)
{
  unsigned a0, a1, a2, a3;	/* Parts of the inet address */
  unsigned p0, p1;		/* Parts of the prot (msb, lsb) */

  if (sscanf (txt, "%*[^0-9]%d,%d,%d,%d,%d,%d", &a0,&a1,&a2,&a3, &p0,&p1) != 6)
    return EGRATUITOUS;
  else
    {
      unsigned char *a, *p;

      *addr = malloc (sizeof (struct sockaddr_in));
      if (! *addr)
	return ENOMEM;

      (*addr)->sa_len = sizeof (struct sockaddr_in);
      (*addr)->sa_family = AF_INET;

      a = (unsigned char *)&((struct sockaddr_in *)*addr)->sin_addr.s_addr;
      a[0] = a0 & 0xff;
      a[1] = a1 & 0xff;
      a[2] = a2 & 0xff;
      a[3] = a3 & 0xff;

      p = (unsigned char *)&((struct sockaddr_in *)*addr)->sin_port;
      p[0] = p0 & 0xff;
      p[1] = p1 & 0xff;

      return 0;
    }
}

/* Compare strings P & Q in a most forgiving manner, ignoring case and
   everything but alphanumeric characters.  */
static int
strlaxcmp (const char *p, const char *q)
{
  for (;;)
    {
      int ch1, ch2;

      while (*p && !isalnum (*p))
	p++;
      while (*q && !isalnum (*q))
	q++;

      if (!*p || !*q)
	break;

      ch1 = tolower (*p);
      ch2 = tolower (*q);
      if (ch1 != ch2)
	break;

      p++;
      q++;
    }

  return *p - *q;
}

/* Try to convert an error message in TXT into an error code.  POSS_ERRS
   contains a list of likely errors to try; if no other clue is found, the
   first thing in poss_errs is returned.  */
error_t
ftp_conn_unix_interp_err (struct ftp_conn *conn, const char *txt,
			  const error_t *poss_errs)
{
  const char *p;
  const error_t *e;

  if (!poss_errs || !poss_errs[0])
    return EIO;

  /* ignore everything before the last colon.  */
  p = strrchr (txt, ':');
  if (p)
    p++;
  else
    p = txt;

  /* Now, for each possible error, do a string compare ignoring case and
     anything non-alphanumberic.  */
  for (e = poss_errs; *e; e++)
    if (strlaxcmp (p, strerror (*e)) == 0)
      return *e;

  return poss_errs[0];
}

struct get_stats_state
{
  char *name;			/* Last read (maybe partial) name.  */
  size_t name_len;		/* Valid length of NAME, *not including* '\0'.  */
  size_t name_alloced;		/* Allocated size of NAME (>= NAME_LEN).  */
  int name_partial;		/* True if NAME isn't complete.  */

  int contents;			/* Are we looking for directory contents?  */
  char *searched_name;          /* If we are not, then we are only
                                   looking for this name.  */

  int added_slash;		/* Did we prefix the name with `./'?  */

  struct stat stat;		/* Last read stat info.  */

  int start;			/* True if at beginning of output.  */

  size_t buf_len;		/* Length of contents in BUF.  */
  char buf[7000];
};

/* Start an operation to get a list of file-stat structures for NAME (this is
   often similar to ftp_conn_start_dir, but with OS-specific flags), and
   return a file-descriptor for reading on, and a state structure in STATE
   suitable for passing to cont_get_stats.  If CONTENTS is true, NAME must
   refer to a directory, and the contents will be returned, otherwise, the
   (single) result will refer to NAME.  */
error_t
ftp_conn_unix_start_get_stats (struct ftp_conn *conn,
			       const char *name, int contents,
			       int *fd, void **state)
{
  error_t err = 0;
  size_t req_len;
  char *req = NULL;
  struct get_stats_state *s = NULL;
  const char *flags = "-A";
  const char *slash = strchr (name, '/');
  char *searched_name = NULL;

  s = (struct get_stats_state *) malloc (sizeof (struct get_stats_state));
  if (! s)
    {
      err = ENOMEM;
      goto out;
    }
  if (! contents)
    {
      if (! strcmp (name, "/"))
	{
	  /* Listing only the directory itself and not the directory
	     content seems to be not supported by all FTP servers.  If
	     the directory in question is not the root directory, we
	     can simply lookup `..', but that doesn't work if we are
	     already on the root directory.  */
	  err = EINVAL;
	}
      else
	{
	  searched_name = strdup (basename ((char *) name));
	  if (! searched_name)
	    err = ENOMEM;
	}
      if (err)
	goto out;
    }

  if (strcspn (name, "*? \t\n{}$`\\\"'") < strlen (name))
    /* NAME contains some metacharacters, which makes the behavior of various
       ftp servers unpredictable, so punt.  */
    {
      err = EINVAL;
      goto out;
    }

  /* We pack the ls options and the name into the list argument, in REQ,
     which will do the right thing on most unix ftp servers.  */

  req_len = strlen (flags) + 2; /* space character + null character.  */
  if (! contents)
    {
      /* If we are looking for a directory rather than its content,
	 lookup the parent directory and search for the entry, rather
	 than looking it up directly, as not all ftp servers support
	 the -d option to ls.  To make sure we get a directory, append
	 '/', except for the root directory.  */
      char *dirn = dirname (strdupa (name));
      int is_root = ! strcmp (dirn, "/");
      req_len += strlen (dirn) + (is_root ? 0 : 1);
      req = malloc (req_len);
      if (! req)
	err = ENOMEM;
      else
	sprintf (req, "%s %s%s", flags, dirn, (is_root ? "" : "/"));
    }
  else
    {
      /* If NAME doesn't contain a slash, we prepend `./' to it so that we can
	 tell from the results whether it's a directory or not.  */
      req_len += strlen (name) + (slash ? 0 : 2);
      req = malloc (req_len);
      if (! req)
	err = ENOMEM;
      else
	sprintf (req, "%s %s%s", flags, slash ? "" : "./", name);
    }

  if (err)
    goto out;

  /* Make the actual request.  */
  err = ftp_conn_start_dir (conn, req, fd);

 out:

  free (req);
  if (err)
    {
      free (s);
      free (searched_name);
    }
  else
    {
      s->contents = contents;
      s->searched_name = searched_name;
      s->added_slash = !slash;
      s->name = 0;
      s->name_len = s->name_alloced = 0;
      s->name_partial = 0;
      s->buf_len = 0;
      s->start = 1;
      *state = s;
    }

  return err;
}

static char *months[] =
{
  "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct",
  "nov", "dec", 0
};

/* Translate the information in the ls output in *LINE as best we can into
   STAT, and update *LINE to point to the filename at the end of the line.
   If *LINE should be ignored, EAGAIN is returned.  */
static error_t
parse_dir_entry (char **line, struct stat *stat)
{
  char **m;
  struct tm tm;
  char *p = *line, *e;

  /*
drwxrwxrwt  3 root  wheel  1024 May  1 16:58 /tmp
drwxrwxrwt   5 root     daemon       4096 May  1 17:15 /tmp
drwxrwxrwt   4 root     0            1024 May  1 14:34 /tmp
drwxrwxrwt  6 root     wheel         284 May  1 12:46 /tmp
drwxrwxrwt   4 sys      sys          482 May  1 17:11 /tmp
drwxrwxrwt   7 34       archive       512 May  1 14:28 /tmp
  */

  if (strncasecmp (p, "total ", 6) == 0)
    return EAGAIN;

  memset (stat, 0, sizeof *stat);

#ifdef FSTYPE_FTP
  stat->st_fstype = FSTYPE_FTP;
#endif

  /* File format (S_IFMT) bits.  */
  switch (*p++)
    {
    case '-': stat->st_mode |= S_IFREG; break;
    case 'd': stat->st_mode |= S_IFDIR; break;
    case 'c': stat->st_mode |= S_IFCHR; break;
    case 'b': stat->st_mode |= S_IFBLK; break;
    case 'l': stat->st_mode |= S_IFLNK; break;
    case 's': stat->st_mode |= S_IFSOCK; break;
    case 'p': stat->st_mode |= S_IFIFO; break;
    default: return EGRATUITOUS;
    }

  /* User perm bits.  */
  switch (*p++)
    {
    case '-': break;
    case 'r': stat->st_mode |= S_IRUSR; break;
    default: return EGRATUITOUS;
    }
  switch (*p++)
    {
    case '-': break;
    case 'w': stat->st_mode |= S_IWUSR; break;
    default: return EGRATUITOUS;
    }
  switch (*p++)
    {
    case '-': break;
    case 'x': stat->st_mode |= S_IXUSR; break;
    case 's': stat->st_mode |= S_IXUSR | S_ISUID; break;
    case 'S': stat->st_mode |= S_ISUID; break;
    default: return EGRATUITOUS;
    }

  /* Group perm bits.  */
  switch (*p++)
    {
    case '-': break;
    case 'r': stat->st_mode |= S_IRGRP; break;
    default: return EGRATUITOUS;
    }
  switch (*p++)
    {
    case '-': break;
    case 'w': stat->st_mode |= S_IWGRP; break;
    default: return EGRATUITOUS;
    }
  switch (*p++)
    {
    case '-': break;
    case 'x': stat->st_mode |= S_IXGRP; break;
    case 's': stat->st_mode |= S_IXGRP | S_ISGID; break;
    case 'S': stat->st_mode |= S_ISGID; break;
    default: return EGRATUITOUS;
    }

  /* `Other' perm bits.  */
  switch (*p++)
    {
    case '-': break;
    case 'r': stat->st_mode |= S_IROTH; break;
    default: return EGRATUITOUS;
    }
  switch (*p++)
    {
    case '-': break;
    case 'w': stat->st_mode |= S_IWOTH; break;
    default: return EGRATUITOUS;
    }
  switch (*p++)
    {
    case '-': break;
    case 'x': stat->st_mode |= S_IXOTH; break;
    case 't': stat->st_mode |= S_IXOTH | S_ISVTX; break;
    case 'T': stat->st_mode |= S_ISVTX; break;
    default: return EGRATUITOUS;
    }

#define SKIP_WS() \
  while (isspace (*p)) p++;
#define PARSE_INT() ({							      \
    unsigned u = strtoul (p, &e, 10);					      \
    if (e == p || isalnum (*e))						      \
      return EGRATUITOUS;						      \
    p = e;								      \
    u;									      \
  })

  /* Link count.  */
  SKIP_WS ();
  stat->st_nlink = PARSE_INT ();

  /* File owner.  */
  SKIP_WS ();
  if (isdigit (*p))
    stat->st_uid = PARSE_INT ();
  else
    {
      struct passwd *pw;

      e = p + strcspn (p, " \t\n");
      *e++ = '\0';

      pw = getpwnam (p);

      if (pw)
	stat->st_uid = pw->pw_uid;
      else
	stat->st_uid = DEFAULT_UID;

      p = e;
    }

#ifdef HAVE_STAT_ST_AUTHOR
  stat->st_author = stat->st_uid;
#endif

  /* File group.  */
  SKIP_WS ();
  if (isdigit (*p))
    stat->st_gid = PARSE_INT ();
  else
    {
      struct group *gr;

      e = p + strcspn (p, " \t\n");
      *e++ = '\0';

      gr = getgrnam (p);

      if (gr)
	stat->st_gid = gr->gr_gid;
      else
	stat->st_gid = DEFAULT_GID;

      p = e;
    }

  /* File size / device numbers.  */
  SKIP_WS ();
  if (S_ISCHR (stat->st_mode) || S_ISBLK (stat->st_mode))
    /* Block and character devices show the block params instead of the file
       size.  */
    {
      stat->st_dev = PARSE_INT ();
      if (*p != ',')
	return EGRATUITOUS;
      stat->st_dev = (stat->st_dev << 8) | PARSE_INT ();
      stat->st_size = 0;
    }
  else
    /* File size. */
    stat->st_size = PARSE_INT ();

  stat->st_blocks = stat->st_size >> 9;

  /* Date.  Ick.  */
  /* Formats:  MONTH DAY HH:MM and MONTH DAY  YEAR  */

  memset (&tm, 0, sizeof tm);

  SKIP_WS ();
  e = p + strcspn (p, " \t\n");
  for (m = months; *m; m++)
    if (strncasecmp (*m, p, e - p) == 0)
      {
	tm.tm_mon = m - months;
	break;
      }
  if (! *m)
    return EGRATUITOUS;
  p = e;

  SKIP_WS ();
  tm.tm_mday = PARSE_INT ();

  SKIP_WS ();
  if (p[1] == ':' || p[2] == ':')
    {
      struct tm *now_tm;
      struct timeval now_tv;

      tm.tm_hour = PARSE_INT ();
      p++;
      tm.tm_min = PARSE_INT ();

      if (gettimeofday (&now_tv, 0) != 0)
	return errno;

      now_tm = localtime (&now_tv.tv_sec);
      if (now_tm->tm_mon < tm.tm_mon)
	tm.tm_year = now_tm->tm_year - 1;
      else
	tm.tm_year = now_tm->tm_year;
    }
  else
    tm.tm_year = PARSE_INT () - 1900;

  stat->st_mtim.tv_sec = mktime (&tm);
  if (stat->st_mtim.tv_sec == (time_t)-1)
    return EGRATUITOUS;

  /* atime and ctime are the same as mtime.  */
  stat->st_atim.tv_sec  = stat->st_ctim.tv_sec  = stat->st_mtim.tv_sec;
  stat->st_atim.tv_nsec = stat->st_ctim.tv_nsec = stat->st_mtim.tv_nsec = 0;

  /* Update *LINE to point to the filename.  */
  SKIP_WS ();
  *line = p;

  return 0;
}

/* Read stats information from FD, calling ADD_STAT for each new stat (HOOK
   is passed to ADD_STAT).  FD and STATE should be returned from
   start_get_stats.  If this function returns EAGAIN, then it should be
   called again to finish the job (possibly after calling select on FD); if
   it returns 0, then it is finishe,d and FD and STATE are deallocated.  */
error_t
ftp_conn_unix_cont_get_stats (struct ftp_conn *conn, int fd, void *state,
			      ftp_conn_add_stat_fun_t add_stat, void *hook)
{
  char *p, *nl;
  ssize_t rd;
  size_t name_len;
  error_t err = 0;
  struct get_stats_state *s = state;
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
      /* We're done!  Clean up and return the result in STATS.  */
      {
	if (s->start)
	  /* No output at all.  From many ftp servers, this means that the
	     specified file wasn't found.  */
	  err = ENOENT;
	goto finished;
      }
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

  s->start = 0;			/* We've read past the start.  */

  if (!nl && s->buf_len < sizeof (s->buf))
    /* We didn't find any newlines (which implies we didn't hit EOF), and we
       still have room to grow the buffer, so just wait until next time to do
       anything.  */
    return EAGAIN;

  /* Where we start parsing.  */
  p = s->buf;

  do
    {
      if (! s->name_partial)
	/* We aren't continuing to read an overflowed name from the previous
	   call, so we know that we are at the start of a line, and can parse
	   the info here as a directory entry.  */
	{
	  /* Parse the directory entry info, updating P to point to the
	     beginning of the name.  */
	  err = parse_dir_entry (&p, &s->stat);
	  if (err == EAGAIN)
	    /* This line isn't a real entry and should be ignored.  */
	    goto skip_line;
	  if (err)
	    goto finished;
	}

      /* Now fill in S->last_stat's name field, possibly extending it from a
	 previous buffer.  */
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
	  char *symlink_target = 0;

	  if (S_ISLNK (s->stat.st_mode))
	    /* A symlink, see if we can find the link target.  */
	    {
	      symlink_target = strstr (name, " -> ");
	      if (symlink_target)
		{
		  *symlink_target = '\0';
		  symlink_target += 4;
		}
	    }

	  if (strchr (name, '/'))
	    {
	      if (s->contents)
		/* We know that the name originally request had a slash in
		   it (because we added one if necessary), so if a name in
		   the listing has one too, it can't be the contents of a
		   directory; if this is the case and we wanted the
		   contents, this must not be a directory.  */
		{
		  err = ENOTDIR;
		  goto finished;
		}
	      else if (s->added_slash)
		/* S->name must be the same name we passed; if we added a
		   `./' prefix, removed it so the client gets back what it
		   passed.  */
		name += 2;
	    }

	  /* Pass only directory-relative names to the callback function.  */
	  name = basename (name);

	  if (s->contents || ! strcmp (s->name, s->searched_name))
	    {
	      /* We are only interested in searched_name.  */

	      /* Call the callback function to process the current entry; it is
		 responsible for freeing S->name and SYMLINK_TARGET.  */
	      err = (*add_stat) (name, &s->stat, symlink_target, hook);
	      if (err)
		goto finished;
	    }

	  s->name_len = 0;
	  s->name_partial = 0;

	skip_line:
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
  if (s->searched_name)
    free (s->searched_name);
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

/* Give a name which refers to a directory file, and a name in that
   directory, this should return in COMPOSITE the composite name referring to
   that name in that directory, in malloced storage.  */
error_t
ftp_conn_unix_append_name (struct ftp_conn *conn,
			   const char *dir, const char *name,
			   char **composite)
{
   char *path = malloc (strlen (dir) + 1 + strlen (name) + 1);

   if (! path)
     return ENOMEM;

   /* Form the path name.  */
   if (name && *name)
     if (dir[0] == '/' && dir[1] == '\0')
       stpcpy (stpcpy (path, dir), name);
     else
       stpcpy (stpcpy (stpcpy (path, dir), "/"), name);
   else
     strcpy (path, dir);

   *composite = path;

   return 0;
}

/* If the name of a file *NAME is a composite name (containing both a
   filename and a directory name), this function should change *NAME to be
   the name component only; if the result is shorter than the original
   *NAME, the storage pointed to it may be modified, otherwise, *NAME
   should be changed to point to malloced storage holding the result, which
   will be freed by the caller.  */
error_t
ftp_conn_unix_basename (struct ftp_conn *conn, char **name)
{
  *name = basename (*name);
  return 0;
}
