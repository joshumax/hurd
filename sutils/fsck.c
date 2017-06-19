/* Hurd-aware fsck wrapper

   Copyright (C) 1996, 97, 98, 99 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.org>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* This wrapper runs other file-system specific fsck programs.  They are
   expected to accept at least the following options:

     -p  Terse automatic mode
     -y  Automatically answer yes to all questions
     -n  Automatically answer no to all questions
     -f  Check even if clean
     -s  Only print diagostic messages

   They should also return exit-status codes as following:

     0   Filesystem was clean
     1,2 Filesystem fixed (and is now clean)
     4,8 Filesystem was broken, but couldn't be fixed
     ... Anything else is assumed be some horrible error

   The exit-status from this wrapper is the greatest status returned from any
   individual fsck.

   Although it knows something about the hurd, this fsck still uses
   /etc/fstab, and is generally not very integrated.  That will have to wait
   until the appropriate mechanisms for doing so are decided.  */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <error.h>
#include <argp.h>
#include <argz.h>
#include <assert-backtrace.h>
#include <version.h>

#include "fstab.h"

const char *argp_program_version = STANDARD_HURD_VERSION (fsck);


/* for debugging  */
static int _debug = 0;
#define debug(fmt, args...)						      \
  do { if (_debug) {							      \
 	 fprintf (stderr, "[%s: ", __FUNCTION__);			      \
	 fprintf (stderr, fmt , ##args);				      \
	 fprintf (stderr, "]\n"); } } while (0)
#define fs_debug(fs, fmt, args...)					      \
  debug ("%s: " fmt, (fs)->mntent.mnt_dir , ##args)

#define FSCK_SEARCH_FMTS "/sbin/fsck.%s"

/* Exit codes we return.  */
#define FSCK_EX_OK       0      /* No errors */
#define FSCK_EX_FIXED    1      /* File system errors corrected */
#define FSCK_EX_BROKEN   4      /* File system errors left uncorrected */
#define FSCK_EX_QUIT	 12	/* Got SIGQUIT */
#define FSCK_EX_SIGNAL	 20	/* Signalled (not SIGQUIT) */
#define FSCK_EX_ERROR	 50
#define FSCK_EX_EXEC	 99	/* Exec failed */
/* Everything else is some sort of fsck problem.  */

/* Things we know about what child fsck's might return.  */
#define FSCK_EX_IS_FIXED(st) ({ int _st = (st); _st >= 1 || _st <= 2; })
#define FSCK_EX_IS_BROKEN(st) ({ int _st = (st); _st >= 4 || _st <= 8; })

/* Common fsck flags.  */
#define FSCK_F_PREEN	0x1
#define FSCK_F_YES	0x2
#define FSCK_F_NO	0x4
#define FSCK_F_FORCE	0x8
#define FSCK_F_SILENT	0x10

/* The following are only used internally.  */
#define FSCK_F_VERBOSE	0x100
#define FSCK_F_WRITABLE	0x200	/* Make writable after fscking.  */
#define FSCK_F_AUTO	0x400	/* Do all filesystems in fstab.  */
#define FSCK_F_DRYRUN	0x800	/* Don't actually do anything.  */

static int got_sigquit = 0, got_sigint = 0;

static void sigquit ()
{
  got_sigquit = 1;
}

static void sigint ()
{
  got_sigint = 1;
}

struct fsck
{
  struct fs *fs;		/* Filesystem being fscked.  */
  int pid;			/* Pid for process.  */
  int make_writable;		/* Make writable after fscking if possible.  */
  struct fsck *next, **self;
};

struct fscks
{
  struct fsck *running;		/* Fsck processes now running.  */
  int free_slots;		/* Number of fsck processes we can start.  */
  int flags;
};

/* Starts FS's fsck program on FS's device, returning the pid of the process.
   If an error is encountered, prints an error message and returns 0.
   Filesystems that need not be fscked at all also return 0 (but don't print
   an error message).  */
static pid_t
fs_start_fsck (struct fs *fs, int flags)
{
  pid_t pid;
  char flags_buf[10];
  char *argv[4], **argp = argv;
  struct fstype *type;
  error_t err = fs_type (fs, &type);

  assert_perror_backtrace (err);		/* Should already have been checked for. */
  assert_backtrace (type->program);

  *argp++ = type->program;

  if (flags & (FSCK_F_PREEN|FSCK_F_YES|FSCK_F_NO|FSCK_F_FORCE|FSCK_F_SILENT))
    {
      char *p = flags_buf;
      *argp++ = flags_buf;
      *p++ = '-';
      if (flags & FSCK_F_PREEN)  *p++ = 'p';
      if (flags & FSCK_F_YES)    *p++ = 'y';
      if (flags & FSCK_F_NO)     *p++ = 'n';
      if (flags & FSCK_F_FORCE)  *p++ = 'f';
      if (flags & FSCK_F_SILENT) *p++ = 's';
      *p = '\0';
    }

  *argp++ = fs->mntent.mnt_fsname;
  *argp = 0;

  if (flags & FSCK_F_DRYRUN)
    {
      char *argz;
      size_t argz_len;
      argz_create (argv, &argz, &argz_len);
      argz_stringify (argz, argz_len, ' ');
      puts (argz);
      free (argz);
      return 0;
    }

  pid = fork ();
  if (pid < 0)
    {
      error (0, errno, "fork");
      return 0;
    }

  if (pid == 0)
    /* Child.  */
    {
      execv (type->program, argv);
      exit (FSCK_EX_EXEC);	/* Exec failed. */
    }

  if ((flags & FSCK_F_VERBOSE) || _debug)
    {
      char *argz;
      size_t argz_len;
      argz_create (argv, &argz, &argz_len);
      argz_stringify (argz, argz_len, ' ');
      fs_debug (fs, "Spawned pid %d: %s", pid, argz);
      if (flags & FSCK_F_VERBOSE)
	puts (argz);
      free (argz);
    }

  return pid;
}

/* Start a fsck process for FS running, and add an entry for it to FSCKS.
   This also ensures that if FS is currently mounted, it will be made
   readonly first.  If the fsck is successfully started, 0 is returned,
   otherwise FSCK_EX_ERROR.  */
static int
fscks_start_fsck (struct fscks *fscks, struct fs *fs)
{
  error_t err;
  int mounted, make_writable;
  struct fsck *fsck;

  if (got_sigint)
    /* We got SIGINT, so we pretend that all fscks got a signal without even
       attempting to run them.  */
    {
      fs_debug (fs, "Forcing signal");
      return FSCK_EX_SIGNAL;
    }

#define CK(err, fmt, args...) \
    do { if (err) { error (0, err, fmt , ##args); return FSCK_EX_ERROR; } } while (0)

  fs_debug (fs, "Checking mounted state");
  err = fs_mounted (fs, &mounted);
  CK (err, "%s: Cannot check mounted state", fs->mntent.mnt_dir);

  if (mounted)
    {
      int readonly;

      fs_debug (fs, "Checking readonly state");
      err = fs_readonly (fs, &readonly);
      CK (err, "%s: Cannot check readonly state", fs->mntent.mnt_dir);

      if (fscks->flags & FSCK_F_DRYRUN)
	{
	  if (! readonly)
	    {
	      printf ("%s: writable filesystem %s would be made read-only\n",
		      program_invocation_name, fs->mntent.mnt_dir);
	      readonly = 1;
	    }
	}

      if (! readonly)
	{
	  fs_debug (fs, "Making readonly");
	  err = fs_set_readonly (fs, 1);
	  CK (err, "%s: Cannot make readonly", fs->mntent.mnt_dir);
	}

      make_writable = !readonly
	|| ((fscks->flags & FSCK_F_WRITABLE) && hasmntopt (&fs->mntent, "rw"));
      if (make_writable)
	{
	  fs_debug (fs, "Will make writable after fscking if possible");
	  make_writable = 1;
	}
    }
  else
    make_writable = 0;

#undef CK

  /* Ok, any mounted filesystem is safely readonly.  */

  fsck = malloc (sizeof (struct fsck));
  if (! fsck)
    {
      error (0, ENOMEM, "malloc");
      return FSCK_EX_ERROR;
    }

  fsck->fs = fs;
  fsck->make_writable = make_writable;
  fsck->next = fscks->running;
  if (fsck->next)
    fsck->next->self = &fsck->next;
  fsck->self = &fscks->running;
  fsck->pid = fs_start_fsck (fs, fscks->flags);
  fscks->running = fsck;

  if (fsck->pid)
    fscks->free_slots--;

  return 0;
}

/* Cleanup after fscking with FSCK.  If REMOUNT is true, ask the filesystem
   to remount itself (to incorporate changes made by the fsck program).  If
   MAKE_WRITABLE is true, then if the filesystem should be made writable, do
   so (after remounting if applicable).  */
static void
fsck_cleanup (struct fsck *fsck, int remount, int make_writable)
{
  error_t err = 0;
  struct fs *fs = fsck->fs;

  /* Remove from chain.  */
  *fsck->self = fsck->next;
  if (fsck->next)
    fsck->next->self = fsck->self;

  fs_debug (fs, "Cleaning up after fsck (remount = %d, make_writable = %d)",
	    remount, make_writable);

  if (fs->mounted > 0)
    /* It's currently mounted; if the fsck modified the device, tell the
       running filesystem to remount it.  Also we may make it writable.  */
    {
      if (remount)
	{
	  fs_debug (fs, "Remounting");
	  err = fs_remount (fs);
	  if (err)
	    error (0, err, "%s: Cannot remount", fs->mntent.mnt_dir);
	}
      if (!err && make_writable && fsck->make_writable)
	{
	  fs_debug (fs, "Making writable");
	  err = fs_set_readonly (fs, 0);
	  if (err)
	    error (0, err, "%s: Cannot make writable", fs->mntent.mnt_dir);
	}
    }

   free (fsck);
}

/* Wait for some fsck process to exit, cleaning up after it, and return its
   exit-status.  */
static int
fscks_wait (struct fscks *fscks)
{
  pid_t pid;
  int wstatus, status;
  struct fsck *fsck, *next;

  /* Cleanup fscks that didn't even start.  */
  for (fsck = fscks->running; fsck; fsck = next)
    {
      next = fsck->next;
      if (fsck->pid == 0)
	{
	  fs_debug (fsck->fs, "Pruning failed fsck");
	  fsck_cleanup (fsck, 0, 1);
	}
    }

  debug ("Waiting...");

  do
    pid = wait (&wstatus);
  while (pid < 0 && errno == EINTR);

  if (pid > 0)
    {
      if (WIFEXITED (wstatus))
	status = WEXITSTATUS (wstatus);
      else if (WIFSIGNALED (wstatus))
	status = FSCK_EX_SIGNAL;
      else
	status = FSCK_EX_ERROR;

      for (fsck = fscks->running; fsck; fsck = fsck->next)
	if (fsck->pid == pid)
	  {
	    int remount = (status != 0);
	    int make_writable = (status == 0 || FSCK_EX_IS_FIXED (status));
	    fs_debug (fsck->fs, "Fsck finished (status = %d)", status);
	    fsck_cleanup (fsck, remount, make_writable);
	    fscks->free_slots++;
	    break;
	  }
      if (! fsck)
	error (0, 0, "%d: Unknown process exited", pid);
    }
  else if (errno == ECHILD)
    /* There are apparently no child processes left, and we weren't told of
       their demise.  This can't happen.  */
    {
      while (fscks->running)
	{
	  error (0, 0, "%s: Fsck process disappeared!",
		 fscks->running->fs->mntent.mnt_fsname);
	  /* Be pessimistic -- remount the filesystem, but leave it
	     readonly.  */
	  fsck_cleanup (fscks->running, 1, 0);
	  fscks->free_slots++;
	}
      status = FSCK_EX_ERROR;
    }
  else
    status = FSCK_EX_ERROR;	/* What happened?  */

  return status;
}

/* Fsck all the filesystems in FSTAB, with the flags in FLAGS, doing at most
   MAX_PARALLEL parallel fscks.  The greatest exit code returned by any one
   fsck is returned.  */
static int
fsck (struct fstab *fstab, int flags, int max_parallel)
{
  int pass;
  struct fs *fs;
  int autom = (flags & FSCK_F_AUTO);
  int summary_status = 0;
  struct fscks fscks = { running: 0, flags: flags };

  void merge_status (int status)
    {
      if (status > summary_status)
	summary_status = status;
    }

  /* Do in pass order; pass 0 is never run, it is reserved for "off".  */
  for (pass = 1; pass > 0; pass = fstab_next_pass (fstab, pass))
    /* Submit all filesystems in the given pass, up to MAX_PARALLEL at a
       time.  There should currently be no fscks running.  */
    {
      debug ("Pass %d", pass);

      fscks.free_slots = max_parallel;

      /* Try and fsck every filesystem in this pass.  */
      for (fs = fstab->entries; fs; fs = fs->next)
	if (fs->mntent.mnt_passno == pass)
	  /* FS is applicable for this pass.  */
	  {
	    struct fstype *type;
	    error_t err = fs_type (fs, &type);

	    if (err)
	      {
		error (0, err, "%s: Cannot find fsck program (type %s)",
		       fs->mntent.mnt_dir, fs->mntent.mnt_type);
		merge_status (FSCK_EX_ERROR);
	      }
	    else if (type->program)
	      /* This is a fsckable filesystem.  */
	      {
		fs_debug (fs, "Fsckable; free_slots = %d", fscks.free_slots);
		while (fscks.free_slots == 0)
		  /* No room; wait for another fsck to finish.  */
		  merge_status (fscks_wait (&fscks));
		merge_status (fscks_start_fsck (&fscks, fs));
	      }
	    else if (autom)
	      fs_debug (fs, "Not fsckable");
	    else
	      error (0, 0, "%s: %s: Not a fsckable filesystem type",
		     fs->mntent.mnt_dir, fs->mntent.mnt_type);
	  }

      /* Now wait for them all to finish.  */
      while (fscks.running)
	merge_status (fscks_wait (&fscks));
    }

  return summary_status;
}

static const struct argp_option options[] =
{
  {"preen",      'p', 0,      0, "Terse automatic mode", 1},
  {"yes",        'y', 0,      0, "Automatically answer yes to all questions"},
  {"no",         'n', 0,      0, "Automatically answer no to all questions"},
  {"parallel",   'l', "NUM",  0, "Limit the number of parallel checks to NUM"},
  {"verbose",	 'v', 0,      0, "Print informational messages"},
  {"writable",   'w', 0,      0,
     "Make RW filesystems writable after fscking, if possible"},
  {"debug",	 'D', 0,      OPTION_HIDDEN },
  {"force",	 'f', 0,      0, "Check even if clean"},

  {"dry-run",	 'N', 0,      0, "Don't check, just show what would be done"},
  {0, 0, 0, 0, "In --preen mode, the following also apply:", 2},
  {"silent",     's', 0,      0, "Print only diagnostic messages"},
  {"quiet",      'q', 0,      OPTION_ALIAS | OPTION_HIDDEN },
  {0, 0}
};
static const char doc[] = "Filesystem consistency check and repair";
static const char args_doc[] = "[ DEVICE|FSYS... ]";


int
main (int argc, char **argv)
{
  struct fstab *check;
  int status;			/* exit status */
  int flags = 0;
  int max_parallel = -1;	/* -1 => use default */

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      struct fstab_argp_params *params = state->input;
      switch (key)
	{
	case ARGP_KEY_INIT:
	  state->child_inputs[0] = params; /* pass down to fstab_argp parser */
	  break;
	case 'p': flags |= FSCK_F_PREEN; break;
	case 'y': flags |= FSCK_F_YES; break;
	case 'n': flags |= FSCK_F_NO; break;
	case 'f': flags |= FSCK_F_FORCE; break;
	case 's': flags |= FSCK_F_SILENT; break;
	case 'v': flags |= FSCK_F_VERBOSE; break;
	case 'w': flags |= FSCK_F_WRITABLE; break;
	case 'N': flags |= FSCK_F_DRYRUN; break;
	case 'D': _debug = 1; break;
	case 'l':
	  max_parallel = atoi (arg);
	  if (max_parallel < 1)
	    argp_error (state, "%s: Invalid value for --max-parallel", arg);
	  break;
	case ARGP_KEY_NO_ARGS:
	  if (flags & FSCK_F_PREEN)
	    params->do_all = 1;
	  else if (!params->do_all)
	    {
	      argp_usage (state);
	      return EINVAL;
	    }
	  break;
	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  static const struct argp_child kids[] =
  { { &fstab_argp, 0,
      "Filesystem selection (default is all in " _PATH_MNTTAB "):", 2 },
    { 0 } };
  struct argp argp = { options, parse_opt, args_doc, doc, kids };
  struct fstab_argp_params fstab_params;

  argp_parse (&argp, argc, argv, 0, 0, &fstab_params);

  check = fstab_argp_create (&fstab_params,
			     FSCK_SEARCH_FMTS, sizeof FSCK_SEARCH_FMTS);
  if (fstab_params.do_all)
    flags |= FSCK_F_AUTO;

  if (max_parallel <= 0)
    {
      if (flags & FSCK_F_PREEN)
	max_parallel = 100;	/* In preen mode, do lots in parallel.  */
      else
	max_parallel = 1;	/* Do one at a time to keep output rational. */
    }

  /* If the user send a SIGQUIT (usually ^\), then do all checks, but
     regardless of their outcome, return a status that will cause the
     automatic reboot to stop after fscking is complete.  */
  signal (SIGQUIT, sigquit);

  /* Let currently running fscks complete (each such program can handle
     signals as it sees fit), and cause not-yet-run fscks to act as if they
     got a signal.  */
  signal (SIGINT, sigint);

  debug ("Fscking...");
  status = fsck (check, flags, max_parallel);
  if (got_sigquit && status < FSCK_EX_QUIT)
    status = FSCK_EX_QUIT;

  exit (status);
}
