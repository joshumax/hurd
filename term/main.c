/* main.c - A translator that emulates a terminal.
   Copyright (C) 1995, 1996, 1997, 2000, 2002 Free Software Foundation, Inc.
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

#include <version.h>

const char *argp_program_version = STANDARD_HURD_VERSION (term);

int trivfs_fstype = FSTYPE_TERM;
int trivfs_fsid = 0;
int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ|O_WRITE;

/* The argument line options.  */
char *tty_name;
enum { T_NONE = 0, T_DEVICE, T_HURDIO, T_PTYMASTER, T_PTYSLAVE } tty_type;
char *tty_arg;
int rdev;

int
demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  extern int term_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int tioctl_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int device_reply_server (mach_msg_header_t *, mach_msg_header_t *);

  return (trivfs_demuxer (inp, outp)
	  || term_server (inp, outp)
	  || tioctl_server (inp, outp)
	  || device_reply_server (inp, outp));
}

static struct argp_option options[] =
{
  {"rdev",     'n', "ID", 0,
   "The stat rdev number for this node; may be either a"
   " single integer, or of the form MAJOR,MINOR"},
  {0}
};

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    default:
      return ARGP_ERR_UNKNOWN;
    case ARGP_KEY_INIT:
    case ARGP_KEY_SUCCESS:
    case ARGP_KEY_ERROR:
      break;
    case 'n':
      {
        char *start = arg;
        char *end;
	
        rdev = strtoul (start, &end, 0);
        if (*end == ',')
          {
	    /* MAJOR,MINOR form.  */
            start = end;
            rdev = (rdev << 8) + strtoul (start, &end, 0);
          }

        if (end == start || *end != '\0')
          {
            argp_error (state, "%s: Invalid argument to --rdev", arg);
            return EINVAL;
          }
      }
      break;
    case ARGP_KEY_ARG:
      if (!tty_name)
	tty_name = arg;
      else if (!tty_type)
	{
	  if (!strcmp (arg, "device"))
	    tty_type = T_DEVICE;
	  else if (!strcmp (arg, "hurdio"))
	    tty_type = T_HURDIO;
	  else if (!strcmp (arg, "pty-master"))
	    tty_type = T_PTYMASTER;
	  else if (!strcmp (arg, "pty-slave"))
	    tty_type = T_PTYSLAVE;
	  else
	    {
	      argp_error (state, "Invalid terminal type");
	      return EINVAL;
	    }
	}
      else if (!tty_arg)
	tty_arg = arg;
      else
	argp_error (state, "Too many arguments");
      break;
    case ARGP_KEY_END:
      if (!tty_name || !tty_type || !tty_arg)
	argp_error (state, "Too few arguments");
      break;
    }
  return 0;
}

static struct argp term_argp =
  { options, parse_opt, "NAME TYPE ARG", "A translator that emulates a terminal.\v"\
    "Possible values for TYPE:\n"\
    "  device      Use Mach device ARG as bottom handler.\n"\
    "  hurdio      Use file port ARG as bottom handler.\n"\
    "  pty-master  Master for slave at ARG.\n"\
    "  pty-slave   Slave for master at ARG.\n"\
    "\n"\
    "The filename of the node that the translator is attached to should be\n"\
    "supplied in NAME.\n" };

int
main (int argc, char **argv)
{
  struct port_class *ourclass, *ourcntlclass;
  struct port_class *peerclass, *peercntlclass;
  struct trivfs_control **ourcntl, **peercntl;
  mach_port_t bootstrap, right;
  struct stat st;

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
      break;

    case T_HURDIO:
      bottom = &hurdio_bottom;
      ourclass = tty_class;
      ourcntlclass = tty_cntl_class;
      ourcntl = &termctl;
      peerclass = 0;
      peercntlclass = 0;
      peercntl = 0;
      break;

    case T_PTYMASTER:
      bottom = &ptyio_bottom;
      ourclass = pty_class;
      ourcntlclass = pty_cntl_class;
      ourcntl = &ptyctl;
      peerclass = tty_class;
      peercntlclass = tty_cntl_class;
      peercntl = &termctl;
      break;

    case T_PTYSLAVE:
      bottom = &ptyio_bottom;
      ourclass = tty_class;
      ourcntlclass = tty_cntl_class;
      ourcntl = &termctl;
      peerclass = pty_class;
      peercntlclass = pty_cntl_class;
      peercntl = &ptyctl;
      break;

    default:
      /* Should not happen.  */
      fprintf (stderr, "Unknown terminal type\n");
      exit (1);
    }
  
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  
  if (bootstrap == MACH_PORT_NULL)
    {
      fprintf (stderr, "Must be started as a translator\n");
      exit (1);
    }

  /* Set our node.  */
  errno = trivfs_startup (bootstrap, 0,
			  ourcntlclass, term_bucket, ourclass, term_bucket,
			  ourcntl);
  if (errno)
    {
      perror ("Starting translator");
      exit (1);
    }

  /* For ptys, the nodename depends on which half is used.  For now just use
     the hook to store the nodename.  */
  (*ourcntl)->hook = tty_name;

  /* Set peer.  */
  if (peerclass)
    {
      char *peer_name = tty_arg;
      file_t file = file_name_lookup (peer_name, O_CREAT|O_NOTRANS, 0666);

      if (file != MACH_PORT_NULL)
	errno = 0;

      if (! errno)
	errno = trivfs_create_control (file, peercntlclass, term_bucket,
				       peerclass, term_bucket, peercntl);
      if (! errno)
	{
	  right = ports_get_send_right (*peercntl);
	  errno = file_set_translator (file, 0, FS_TRANS_EXCL | FS_TRANS_SET,
				     0, 0, 0, right, MACH_MSG_TYPE_COPY_SEND);
	  mach_port_deallocate (mach_task_self (), right);
	}

      if (errno)
	{
	  perror (peer_name);
	  exit (1);
	}

      (*peercntl)->hook = peer_name;
      ports_port_deref (*peercntl);
    }

  memset (&termstate, 0, sizeof (termstate));
  termflags = NO_CARRIER | NO_OWNER;
  mutex_init (&global_lock);

  /* Initialize status from underlying node.  */
  errno = io_stat ((*ourcntl)->underlying, &st);
  if (errno)
    {
      /* We cannot stat the underlying node.  Fallback to the defaults.  */
      term_owner = term_group = 0;
      term_mode = (bottom == &ptyio_bottom ? DEFFILEMODE : S_IRUSR | S_IWUSR);
      errno = 0;
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
  
  errno = (*bottom->init) ();
  if (errno)
    {
      perror ("Initializing bottom handler");
      exit (1);
    }

  condition_init (&carrier_alert);
  condition_init (&select_alert);
  condition_implies (inputq->wait, &select_alert);
  condition_implies (outputq->wait, &select_alert);

  /* Launch.  */
  ports_manage_port_operations_multithread (term_bucket, demuxer, 0, 0, 0);

  return 0;
}  
