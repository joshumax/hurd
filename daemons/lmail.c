/* Local mail delivery

   Copyright (C) 1996, 1997, 1998, 1999 Free Software Foundation, Inc.

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

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <syslog.h>
#include <sysexits.h>
#include <paths.h>
#include <argp.h>
#include <hurd.h>
#include <hurd/fd.h>
#include <version.h>
#include <time.h>
#include <sys/time.h>
#include <sys/mman.h>

#define OPT_FILE -5
#define OPT_REMOVE -6

const char *argp_program_version = STANDARD_HURD_VERSION (mail.local);

static const struct argp_option
options[] =
{
  {"from",    'f',	"USER",	0, "Record sender as USER"},
  {0,         'd',	0,     	OPTION_ALIAS|OPTION_HIDDEN},
  {0,         'r',	0,     	OPTION_ALIAS|OPTION_HIDDEN},
  {"file",    OPT_FILE, "FILE",	0, "Deliver FILE instead of standard input"},
  {"remove",  OPT_REMOVE, 0,   	0, "Remove FILE after successful delivery"},
  {"mail-dir",'m',	"DIR", 	0, "Look for mailboxes in DIR"},
  {"use-lock-file",'l',	0,     	OPTION_HIDDEN,
   "Use a lock file instead of flock for mailboxes"},
  {0}
};
static const char args_doc[] = "USER...";
static const char doc[] = "Deliver mail to the local mailboxes of USER...";

#define HDR_PFX "From "		/* Header, at the beginning of a line,
				   starting each msg in a mailbox.  */
#define ESC_PFX ">"		/* Used to escape occurrences of HDR_PFX in
				   the msg body.  */

#define BMAX (64*1024)		/* Chunk size for I/O.  */

struct params
{
  char *from;			/* Who the mail is from.  */
  char *mail_dir;		/* Mailbox directory.  */
};

/* Convert the system error code ERR to an appropriate exit value.  This
   function currently only returns three sorts: success, temporary failure,
   or error, with exit codes 0, EX_TEMPFAIL, or EX_UNAVAILABLE.  The table of
   temporary failures is from the bsd original of this program.  */
static int
err_to_ex (error_t err)
{
  switch (err)
    {
    case 0:
      return 0;			/* Success */
    /* Temporary failures: */
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
    case EWOULDBLOCK:	/* Operation would block. */
#endif
    case EAGAIN:		/* Resource temporarily unavailable */
    case EDQUOT:		/* Disc quota exceeded */
    case EBUSY:			/* Device busy */
    case EPROCLIM:		/* Too many processes */
    case EUSERS:		/* Too many users */
    case ECONNABORTED:		/* Software caused connection abort */
    case ECONNREFUSED:		/* Connection refused */
    case ECONNRESET:		/* Connection reset by peer */
    case EDEADLK:		/* Resource deadlock avoided */
    case EFBIG:			/* File too large */
    case EHOSTDOWN:		/* Host is down */
    case EHOSTUNREACH:		/* No route to host */
    case EMFILE:		/* Too many open files */
    case ENETDOWN:		/* Network is down */
    case ENETRESET:		/* Network dropped connection on reset */
    case ENETUNREACH:		/* Network is unreachable */
    case ENFILE:		/* Too many open files in system */
    case ENOBUFS:		/* No buffer space available */
    case ENOMEM:		/* Cannot allocate memory */
    case ENOSPC:		/* No space left on device */
    case EROFS:			/* Read-only file system */
    case ESTALE:		/* Stale NFS file handle */
    case ETIMEDOUT:		/* Connection timed out */
      return EX_TEMPFAIL;
    default:
      return EX_UNAVAILABLE;
    }
}

/* Print and syslog the given error message, with the system error string for
   ERRNO appended.  Return an appropriate exit code for ERRNO.  */
#define SYSERR(fmt, args...) \
  ({ syslog (LOG_ERR, fmt ": %m" , ##args); err_to_ex (errno); })

/* Print and syslog the given error message.  Return the exit code
   EX_UNAVAILABLE.  */
#define ERR(fmt, args...) \
  ({ syslog (LOG_ERR, fmt , ##args); EX_UNAVAILABLE; })

/* Print and syslog the given error message, with the system error string for
   CODE appended.  Return an appropriate exit code for CODE.  */
#define SYSERRX(code, fmt, args...)					      \
  ({ error_t _code = (code);						      \
     syslog (LOG_ERR, fmt ": %s" , ##args , strerror (_code));		      \
     err_to_ex (_code); })

/* Block I/O functions.  These are structured to allow the use of a read
   method that avoids copying the data locally.  */

/* Free block allocated by bread.  */
static void
bfree (char *blk, size_t blk_len)
{
  if (blk_len > 0)
    munmap (blk, blk_len);
}

/* Read up to MAX chars from IN into BLK & BLK_LEN, which may be reused or
   freed.  */
static int
bread (int in, char *in_name, size_t max, char **blk, size_t *blk_len)
{
  char *orig_blk = *blk;
  size_t orig_blk_len = *blk_len;
  error_t err = HURD_DPORT_USE (in, io_read (port, blk, blk_len, -1, max));

  if (err)
    return SYSERRX (err, "%s", in_name);

  if (*blk != orig_blk)
    bfree (orig_blk, orig_blk_len);

  return 0;
}

/* Write BLK & BLK_LEN to OUT.  An exit status is returned.  */
static int
bwrite (int out, char *out_name, const char *blk, size_t blk_len)
{
  while (blk_len > 0)
    {
      ssize_t wr = write (out, blk, blk_len);
      if (wr < 0)
	return SYSERR ("%s", out_name);
      blk += wr;
      blk_len -= wr;
    }
  return 0;
}

/* Copy from file descriptor IN to OUT.  An exit status is returned.  */
static int
copy (int in, char *in_name, int out, char *out_name)
{
  int ex = 0;
  char *blk = 0;
  size_t blk_len = 0;

  do
    {
      ex = bread (in, in_name, BMAX, &blk, &blk_len);
      if (! ex)
	ex = bwrite (out, out_name, blk, blk_len);
    }
  while (blk_len > 0 && !ex);

  bfree (blk, blk_len);

  return ex;
}

static int
write_header (int out, char *out_name, struct params *params)
{
  char *hdr;
  size_t hdr_len;
  struct timeval tv;
  time_t time;
  int ex = 0;

  if (gettimeofday (&tv, 0) < 0)
    return SYSERR ("gettimeofday");

  /* Note that the string returned by ctime includes a terminating newline.  */
  time = tv.tv_sec;
  hdr_len = asprintf (&hdr, "From %s %s", params->from, ctime (&time));
  if (! hdr)
    return SYSERRX (ENOMEM, "%s", out_name);

  ex = bwrite (out, out_name, hdr, hdr_len);

  free (hdr);

  return ex;
}

/* Copy from file descriptor IN to OUT, making any changes needed to make the
   contents a valid mailbox entry.  These include:
    (1) Prepending a `From ...' line, and appending a blank line.
    (2) Replacing any occurrences of `From ' at the beginning of lines with
        `>From '.
   An exit status is returned.  */
static int
process (int in, char *in_name, int out, char *out_name, struct params *params)
{
  /* The block currently being processed.  */
  char *blk = 0;
  size_t blk_len = 0;
  /* MATCH is the string we're looking for to escape, NL_MATCH is the same
     string prefixed by a newline to ease searching (MATCH only matches at
     the beginning of lines).  */
  const char *const nl_match = "\n" HDR_PFX, *const match = nl_match + 1;
  /* The portion of MATCH that matched the end of the previous block.  0
     means that there was at least a newline, so initializing MATCHED to 0
     simulates a newline at the start of IN.  */
  ssize_t matched = 0;
  int ex = write_header (out, out_name, params);

#define match_len (sizeof HDR_PFX - 1)

  if (ex)
    return ex;

#define BWRITE(p, p_len)						      \
  ({ size_t _len = (p_len);						      \
     if (_len > 0 && (ex = bwrite (out, out_name, p, _len)))		      \
       break; })

  do
    {
      char *start, *end;

      ex = bread (in, in_name, BMAX, &blk, &blk_len);

      if (matched >= 0)
	/* The last block ended in a partial match, so see if we can complete
	   it in this block.  */
	{
	  if (blk_len >= match_len - matched
	      && memcmp (blk, match + matched, match_len - matched) == 0)
	    /* It matched; output the escape prefix before the match.  */
	    BWRITE (ESC_PFX, sizeof ESC_PFX - 1);
	  /* Now we have to output the part of the preceding block that we
	     didn't do before because it was a partial match.  */
	  BWRITE (match, matched);
	  /* Ok, we're all caught up.  The portion of the match that's in
	     this block will be output as normal text.  */
	  matched = -1;
	}

      /* Scan through the block looking for matches.  */
      for (start = end = blk; start < blk + blk_len; start = end)
	{
	  /* Look for our match, prefixed by a newline.  */
	  end = memmem (start, blk + blk_len - start, nl_match, match_len + 1);
	  if (end)
	    /* We found a match, output the escape prefix before it.  */
	    {
	      end++;		/* The newline should precede the escape.  */
	      BWRITE (start, end - start); /* part of block before match.  */
	      BWRITE (ESC_PFX, sizeof ESC_PFX - 1); /* escape prefix.  */
	    }
	  else
	    {
	      end = blk + blk_len;
	      break;
	    }
	}

      /* Now see if there are any partial matches at the end.  */
      for (matched =
	     end - start < match_len + 1 ? end - start - 1 : match_len;
	   matched >= 0;
	   matched--)
	if (memcmp (end - matched - 1, nl_match, matched + 1) == 0)
	  /* There's a partial match MATCHED characters long at the end of
	     this block, so delay outputting it until the next block can be
	     examined; we do output the preceding newline here, though.  */
	  {
	    end -= matched;
	    break;
	  }

      BWRITE (start, end - start);
    }
  while (blk_len > 0);

  if (! ex)
    ex = bwrite (out, out_name, "\n", 1); /* Append a blank line.  */

  bfree (blk, blk_len);

  return ex;
}

/* Deliver flags. */
#define D_PROCESS 0x1		/* Deliver */
#define D_REWIND  0x2		/* Rewind MSG before using it.  */

/* Deliver the text from the file descriptor MSG to the mailbox of the user
   RCPT in MAIL_DIR.  FLAGS is from the set D_* above.  An exit appropriate
   exit code is returned.  */
static int
deliver (int msg, char *msg_name, char *rcpt, int flags, struct params *params)
{
  char *mbox;			/* Name of mailbox */
  int fd;			/* Opened mailbox */
  struct stat stat;
  int ex = 0;			/* Exit status */
  struct passwd *pw = getpwnam (rcpt); /* Details of recipient */

  if (! pw)
    return ERR ("%s: Unknown user", rcpt);

  asprintf (&mbox, "%s/%s", params->mail_dir, rcpt);
  if (! mbox)
    return SYSERRX (ENOMEM, "%s", rcpt);

  do
    {
      /* First try to open an existing mailbox.  */
      fd = open (mbox, O_WRONLY|O_APPEND|O_NOLINK|O_EXLOCK);
      if (fd < 0 && errno == ENOENT)
	/* There is none; try to create it.  */
	{
	  fd = open (mbox, O_WRONLY|O_APPEND|O_CREAT|O_EXCL|O_NOLINK|O_EXLOCK,
		     S_IRUSR|S_IWUSR);
	  if (fd >= 0)
	    /* Made a new mailbox!  Set the owner and group appropriately.  */
	    {
	      if (fchown (fd, pw->pw_uid, pw->pw_gid) < 0)
		{
		  close (fd);
		  fd = -1;	/* Propagate error.  */
		}
	    }
	}
    }
  /* EEXIST can only be returned someone else created the mailbox between the
     two opens above, so if we try again, the first open should work.  */
  while (fd < 0 && errno == EEXIST);

  if (fd < 0 || fstat (fd, &stat) < 0)
    ex = SYSERR ("%s", mbox);
  else if (S_ISLNK (stat.st_mode) || stat.st_nlink != 1)
    ex = ERR ("%s: Is linked", mbox);
  else
    {
      if (flags & D_REWIND)
	{
	  if (lseek (msg, 0L, SEEK_SET) < 0)
	    ex = SYSERR ("%s", msg_name);
	}
      if (! ex)
	{
	  if (flags & D_PROCESS)
	    ex = process (msg, msg_name, fd, mbox, params);
	  else
	    ex = copy (msg, msg_name, fd, mbox);
	}
    }

  if (fd >= 0)
    {
      if (fsync (fd) < 0 && !ex)
	ex = SYSERR ("%s", mbox);
      if (close (fd) < 0 && !ex)
	ex = SYSERR ("%s", mbox);
    }
  free (mbox);

  return ex;
}

/* Process from the file descriptor IN into a temporary file, which return a
   descriptor to in CACHED; once *CACHED is closed, it will go away
   permanently.  The file pointer of *CACHED is at an undefined location.  An
   exit status is returned.  */
static int
cache (int in, char *in_name, struct params *params, int *cached)
{
  int ex;
  error_t err;
  file_t file;			/* Hurd port for temp file */
  int fd;			/* File descriptor for it */
  file_t tmp_dir = file_name_lookup (_PATH_TMP, O_RDONLY, 0);

  if (tmp_dir == MACH_PORT_NULL)
    return SYSERR ("%s", _PATH_TMP);

  /* Create FILE without actually putting it into TMP_DIR.  */
  err = dir_mkfile (tmp_dir, O_RDWR, 0600, &file);
  if (err)
    return SYSERRX (err, "%s", _PATH_TMP);

  fd = _hurd_intern_fd (file, O_RDWR, 1);
  if (fd < 0)
    return SYSERR ("%s", _PATH_TMP);

  ex = process (in, in_name, fd, _PATH_TMP, params);
  if (! ex)
    *cached = fd;
  else
    close (fd);

  return ex;
}

int
main (int argc, char **argv)
{
  int rcpt = 0;			/* Index in ARGV of next recipient.  */
  char *file = 0;		/* File containing message.  */
  int remove = 0;		/* Remove file after successful delivery.  */
  int in = 0;			/* Raw input file descriptor.  */
  int ex = 0;			/* Exit status.  */
  struct params params = { from: 0, mail_dir: _PATH_MAILDIR };

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'd':
	  /* do nothing; compatibility */
	  break;
	case 'f':
	case 'r':
	  params.from = arg; break;
	case OPT_FILE:
	  file = arg; break;
	case OPT_REMOVE:
	  remove = 1; break;
	case 'm':
	  params.mail_dir = arg; break;
	case 'l':
	  argp_failure (state, EX_USAGE, EINVAL, "-l not supported");
	case ARGP_KEY_NO_ARGS:
	  argp_error (state, "No recipients");
	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  const struct argp argp = { options, parse_opt, args_doc, doc };

  /* Parse arguments.  */
  argp_parse (&argp, argc, argv, 0, &rcpt, 0);

  /* Use syslog to log errors.  */
  openlog ("mail.local", LOG_PERROR, LOG_MAIL);

  if (! params.from)
    /* No from address specified, use the current user.  */
    {
      struct passwd *pw;
      int uid = getuid ();

      if (uid == -1)
	exit (ERR ("No user id"));

      pw = getpwuid (uid);
      if (! pw)
	exit (ERR ("%d: Unknown uid", uid));

      params.from = strdup (pw->pw_name);
    }

  if (file)
    /* Use FILE as the message contents.  */
    {
      in = open (file, O_RDONLY);
      if (in < 0)
	exit (SYSERR ("%s", file));
    }
  else
    /* Use standard input.  */
    in = 0;

  if (rcpt == argc - 1)
    /* Only a single recipient.  */
    ex = deliver (in, file ?: "-", argv[rcpt], D_PROCESS, &params);
  else
    /* Multiple recipients.  */
    {
      int cached = 0;		/* Temporary processed input file.  */

      ex = cache (in, file ?: "-", &params, &cached);
      if (! ex)
	while (rcpt < argc)
	  {
	    /* Deliver to one recipient.  */
	    int rex = deliver (cached, "message cache", argv[rcpt++],
			       D_REWIND, &params);

	    /* Merge the exit code for that recipient.  Temporary failures
	       take precedence over hard failures and success, as
	       subsequently delivering duplicates (of the successful
	       messages) is preferable to not delivering the temporary
	       failures.  */
	    if (ex != EX_TEMPFAIL)
	      {
		if (rex == EX_TEMPFAIL)
		  ex = EX_TEMPFAIL;
		else if (! ex)
		  ex = rex;
	      }
	  }
    }

  if (file && remove && !ex)
    unlink (file);

  exit (ex);
}
