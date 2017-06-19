/* main.c - A translator that emulates a terminal.
   Copyright (C) 1995,96,97,2000,02 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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

#include "term.h"
#include <hurd.h>
#include <fcntl.h>
#include <hurd/trivfs.h>
#include <stdio.h>
#include <argp.h>
#include <hurd/fsys.h>
#include <string.h>
#include <error.h>
#include <inttypes.h>
#include <argz.h>

#include <version.h>

#include "term_S.h"
#include "tioctl_S.h"
#include "device_reply_S.h"

const char *argp_program_version = STANDARD_HURD_VERSION (term);

int trivfs_fstype = FSTYPE_TERM;
int trivfs_fsid = 0;
int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ|O_WRITE;

enum tty_type { T_NONE = 0, T_DEVICE, T_HURDIO, T_PTYMASTER, T_PTYSLAVE };
static const char *const tty_type_names[] =
{
  [T_DEVICE] = "device",
  [T_HURDIO] = "hurdio",
  [T_PTYMASTER] = "pty-master",
  [T_PTYSLAVE] = "pty-slave",
};


/* The argument line options.  */
char *tty_name;
enum tty_type tty_type;
char *tty_arg;
dev_t rdev;

int
demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = NULL, trivfs_demuxer (inp, outp)) ||
      (routine = term_server_routine (inp)) ||
      (routine = tioctl_server_routine (inp)) ||
      (routine = device_reply_server_routine (inp)))
    {
      if (routine)
        (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

static struct argp_option options[] =
{
  {"rdev",     'n', "ID", 0,
   "The stat rdev number for this node; may be either a"
   " single integer, or of the form MAJOR,MINOR"},
  {"name",	'N', "NAME", 0,
   "The name of this node, to be returned by term_get_nodename."},
  {"type",	'T', "TYPE", 0,
   "Backend type, see below.  This determines the meaning of the argument."},
  {0}
};

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  struct
  {
    dev_t rdev;
    int rdev_set;
    enum tty_type type;
    char *name;
    char *arg;
  } *const v = state->hook;

  switch (opt)
    {
    default:
      return ARGP_ERR_UNKNOWN;

    case ARGP_KEY_INIT:
      state->hook = calloc (1, sizeof *v);
      break;
    case ARGP_KEY_FINI:
      free (v);
      state->hook = 0;
      break;

    case 'n':
      {
        char *start = arg;
        char *end;

        v->rdev = strtoumax (start, &end, 0);
        if (*end == ',')
          {
	    /* MAJOR,MINOR form.  */
            start = end;
            v->rdev = (rdev << 8) + strtoul (start, &end, 0);
          }

        if (end == start || *end != '\0')
          {
            argp_error (state, "%s: Invalid argument to --rdev", arg);
            return EINVAL;
          }

	v->rdev_set = 1;
      }
      break;

    case 'N':
      v->name = arg;
      break;

    case ARGP_KEY_ARG:
      if (!v->name && state->input == 0)
	v->name = arg;
      else if (!v->type && state->input == 0)
	{
	case 'T':
	  if (!strcmp (arg, "device"))
	    v->type = T_DEVICE;
	  else if (!strcmp (arg, "hurdio"))
	    v->type = T_HURDIO;
	  else if (!strcmp (arg, "pty-master"))
	    v->type = T_PTYMASTER;
	  else if (!strcmp (arg, "pty-slave"))
	    v->type = T_PTYSLAVE;
	  else
	    {
	      argp_error (state, "Invalid terminal type");
	      return EINVAL;
	    }
	}
      else if (!v->arg)
	v->arg = arg;
      else
	{
	  argp_error (state, "Too many arguments");
	  return EINVAL;
	}
      break;

    case ARGP_KEY_END:
      if ((v->type && v->type != T_HURDIO && v->arg == 0)
	  || (state->input == 0 && v->name == 0))
	{
	  argp_error (state, "Too few arguments");
	  return EINVAL;
	}
      break;

    case ARGP_KEY_SUCCESS:
      /* Apply the values we've collected.  */
      if (v->rdev_set)
	rdev = v->rdev;
      if (v->name)
	{
	  free (tty_name);
	  tty_name = strdup (v->name);
	}
      if (state->input == 0)	/* This is startup time.  */
	{
	  tty_type = v->type ?: T_HURDIO;
	  tty_arg = v->arg ? strdup (v->arg) : 0;
	}
      else if (v->type || v->arg)
	{
	  /* Dynamic backend switch.  */
	  if (!v->type)
	    v->type = T_HURDIO;
	  switch (v->type)
	    {
	    case T_PTYMASTER:
	    case T_PTYSLAVE:
	      /* Cannot dynamically switch to pty flavors.  */
	      return EINVAL;
	    default:
	      break;
	    }
	  switch (tty_type)
	    {
	    case T_PTYMASTER:
	    case T_PTYSLAVE:
	      /* Cannot dynamically switch from pty flavors either.  */
	      return EINVAL;
	    default:
	      break;
	    }

	  pthread_mutex_lock (&global_lock);
	  (*bottom->fini) ();

	  tty_type = v->type;
	  switch (tty_type)
	    {
	    case T_DEVICE:
	      bottom = &devio_bottom;
	      break;
	    case T_HURDIO:
	      bottom = &hurdio_bottom;
	      break;
	    default:
	      assert_backtrace (! "impossible type");
	      break;
	    }
	  free (tty_arg);
	  tty_arg = strdup (v->arg);
	  error_t err = (*bottom->init) ();
	  if (err == 0 && (termflags & TTY_OPEN))
	    err = (*bottom->assert_dtr) ();
	  pthread_mutex_unlock (&global_lock);
	  return err;
	}
      break;

    case ARGP_KEY_ERROR:
      break;
    }
  return 0;
}

static struct argp term_argp =
  { options, parse_opt, "NAME TYPE ARG",
    "A translator that implements POSIX termios discipline.\v"
    "Possible values for TYPE:\n"
    "  device      Use Mach device ARG for underlying i/o.\n"
    "  hurdio      Use file ARG for i/o, underlying node if no ARG.\n"
    "  pty-master  Master for slave at ARG.\n"\
    "  pty-slave   Slave for master at ARG.\n"\
    "\n"
    "The default type is `hurdio', so no arguments uses the underlying node.\n"
    "The filename of the node that the translator is attached to should be\n"
    "supplied in NAME.\n"
  };

struct argp *trivfs_runtime_argp = &term_argp;

error_t
trivfs_append_args (struct trivfs_control *fsys,
		    char **argz, size_t *argz_len)
{
  error_t err = 0;

  if (rdev)
    {
      char buf[64];
      snprintf (buf, sizeof buf, "--rdev=%#jx", (uintmax_t)rdev);
      err = argz_add (argz, argz_len, buf);
    }

  if (!err && tty_name)
    err = argz_add (argz, argz_len, "--name")
      ?: argz_add (argz, argz_len, tty_name);

  if (!err && tty_type != T_HURDIO)
    err = argz_add (argz, argz_len, "--type")
      ?: argz_add (argz, argz_len, tty_type_names[tty_type]);

  if (!err && tty_arg)
    err = argz_add (argz, argz_len, tty_arg);

  return err;
}

int
main (int argc, char **argv)
{
  struct port_class *ourclass, *ourcntlclass;
  struct port_class *peerclass, *peercntlclass;
  struct trivfs_control **ourcntl, **peercntl;
  mach_port_t bootstrap, right;
  struct stat st;
  error_t err;
  int openmode;

  term_bucket = ports_create_bucket ();

  trivfs_add_control_port_class (&tty_cntl_class);
  trivfs_add_control_port_class (&pty_cntl_class);
  trivfs_add_protid_port_class (&tty_class);
  trivfs_add_protid_port_class (&pty_class);

  cttyid_class = ports_create_class (0, 0);

  init_users ();

  argp_parse (&term_argp, argc, argv, 0, 0, 0);

  switch (tty_type)
    {
    case T_DEVICE:
      bottom = &devio_bottom;
      ourclass = tty_class;
      ourcntlclass = tty_cntl_class;
      ourcntl = &termctl;
      peerclass = 0;
      peercntlclass = 0;
      peercntl = 0;
      openmode = 0;
      break;

    case T_HURDIO:
      bottom = &hurdio_bottom;
      ourclass = tty_class;
      ourcntlclass = tty_cntl_class;
      ourcntl = &termctl;
      peerclass = 0;
      peercntlclass = 0;
      peercntl = 0;

      /* We don't want to have a writable peropen on the underlying node
	 when we'll never use it.  Ideally, we shouldn't open one until we
	 do need it, in case it has an affect on the underlying node (like
	 keeping DTR high and such).  */
      openmode = O_RDWR;
      break;

    case T_PTYMASTER:
      bottom = &ptyio_bottom;
      ourclass = pty_class;
      ourcntlclass = pty_cntl_class;
      ourcntl = &ptyctl;
      peerclass = tty_class;
      peercntlclass = tty_cntl_class;
      peercntl = &termctl;
      openmode = 0;
      break;

    case T_PTYSLAVE:
      bottom = &ptyio_bottom;
      ourclass = tty_class;
      ourcntlclass = tty_cntl_class;
      ourcntl = &termctl;
      peerclass = pty_class;
      peercntlclass = pty_cntl_class;
      peercntl = &ptyctl;
      openmode = 0;
      break;

    default:
      /* Should not happen.  */
      error (1, 0, "Unknown terminal type");
      /*NOTREACHED*/
      return 1;
    }

  task_get_bootstrap_port (mach_task_self (), &bootstrap);

  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  /* Set our node.  */
  err = trivfs_startup (bootstrap, openmode,
			ourcntlclass, term_bucket, ourclass, term_bucket,
			ourcntl);
  if (err)
    error (1, err, "Starting translator");

  /* For ptys, the nodename depends on which half is used.  For now just use
     the hook to store the nodename.  */
  (*ourcntl)->hook = tty_name;

  /* Set peer.  */
  if (peerclass)
    {
      char *peer_name = tty_arg;
      file_t file = file_name_lookup (peer_name, O_CREAT|O_NOTRANS, 0666);

      if (file == MACH_PORT_NULL)
	err = errno;

      if (! err)
	err = trivfs_create_control (file, peercntlclass, term_bucket,
				     peerclass, term_bucket, peercntl);
      if (! err)
	{
	  right = ports_get_send_right (*peercntl);
	  err = file_set_translator (file, 0, FS_TRANS_EXCL | FS_TRANS_SET,
				     0, 0, 0, right, MACH_MSG_TYPE_COPY_SEND);
	  mach_port_deallocate (mach_task_self (), right);
	}

      if (err)
	  error (1, err, "%s", peer_name);

      (*peercntl)->hook = peer_name;
      ports_port_deref (*peercntl);
    }

  memset (&termstate, 0, sizeof (termstate));
  termflags = NO_CARRIER | NO_OWNER;
  pthread_mutex_init (&global_lock, NULL);

  /* Initialize status from underlying node.  */
  err = io_stat ((*ourcntl)->underlying, &st);
  if (err)
    {
      /* We cannot stat the underlying node.  Fallback to the defaults.  */
      term_owner = term_group = 0;
      term_mode = (bottom == &ptyio_bottom ? DEFFILEMODE : S_IRUSR | S_IWUSR);
    }
  else
    {
      term_owner = st.st_uid;
      term_group = st.st_gid;
      term_mode = (st.st_mode & ACCESSPERMS);
    }
  term_mode |= S_IFCHR | S_IROOT;

  inputq = create_queue (256, QUEUE_LOWAT, QUEUE_HIWAT);

  rawq = create_queue (256, QUEUE_LOWAT, QUEUE_HIWAT);

  outputq = create_queue (256, QUEUE_LOWAT, QUEUE_HIWAT);

  err = (*bottom->init) ();
  if (err)
    error (1, err, "Initializing bottom handler");

  pthread_cond_init (&carrier_alert, NULL);
  pthread_cond_init (&select_alert, NULL);

  /* Launch.  */
  ports_manage_port_operations_multithread (term_bucket, demuxer, 0, 0, 0);

  return 0;
}
