/*
   Copyright (C) 1995, 1996, 1999, 2002 Free Software Foundation, Inc.
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

#include <sys/ioctl.h>
#include <string.h>
#include <hurd/ports.h>
#include <unistd.h>
#include <fcntl.h>
#include "term.h"
#include "tioctl_S.h"

/* Set if we need a wakeup when tty output has been done */
static int pty_read_blocked = 0;

/* Wake this up when tty output occurs and pty_read_blocked is set */
static pthread_cond_t pty_read_wakeup = PTHREAD_COND_INITIALIZER;

static pthread_cond_t pty_select_wakeup = PTHREAD_COND_INITIALIZER;

/* Set if "dtr" is on. */
static int dtr_on = 0;

/* Set if packet mode is on. */
static int packet_mode = 0;

/* Set if user ioctl mode is on. */
static int user_ioctl_mode = 0;

/* Byte to send to user in packet mode or user ioctl mode. */
static char control_byte = 0;

static int output_stopped = 0;

static int pktnostop = 0;

static int ptyopen = 0;

static int nptyperopens = 0;


static error_t
ptyio_init (void)
{
  pty_select_alert = &pty_select_wakeup;
  return 0;
}

error_t
pty_open_hook (struct trivfs_control *cntl,
	       struct iouser *user,
	       int flags)
{
  if ((flags & (O_READ|O_WRITE)) == 0)
    return 0;

  pthread_mutex_lock (&global_lock);

  if (ptyopen)
    {
      pthread_mutex_unlock (&global_lock);
      return EBUSY;
    }

  ptyopen = 1;

  /* Re-initialize pty state.  */
  external_processing = 0;
  packet_mode = 0;
  user_ioctl_mode = 0;
  control_byte = 0;
  pktnostop = 0;

  pthread_mutex_unlock (&global_lock);

  return 0;
}

error_t
pty_po_create_hook (struct trivfs_peropen *po)
{
  pthread_mutex_lock (&global_lock);
  if (po->openmodes & (O_READ | O_WRITE))
    {
      nptyperopens++;
      report_carrier_on ();
    }
  pthread_mutex_unlock (&global_lock);
  return 0;
}

error_t
pty_po_destroy_hook (struct trivfs_peropen *po)
{
  pthread_mutex_lock (&global_lock);
  if ((po->openmodes & (O_READ | O_WRITE)) == 0)
    {
      pthread_mutex_unlock (&global_lock);
      return 0;
    }
  nptyperopens--;
  if (!nptyperopens)
    {
      ptyopen = 0;
      report_carrier_off ();
    }
  pthread_mutex_unlock (&global_lock);
  return 0;
}

static inline void
wake_reader ()
{
  if (pty_read_blocked)
    {
      pty_read_blocked = 0;
      pthread_cond_broadcast (&pty_read_wakeup);
      pthread_cond_broadcast (&pty_select_wakeup);
    }
}


/* Lower half for tty node */

static error_t
ptyio_start_output ()
{
  if (packet_mode && output_stopped && (!(termflags & USER_OUTPUT_SUSP)))
    {
      control_byte &= ~TIOCPKT_STOP;
      control_byte |= TIOCPKT_START;
      output_stopped = 0;
    }
  wake_reader ();
  return 0;
}

static error_t
ptyio_abandon_physical_output ()
{
  if (packet_mode)
    {
      control_byte |= TIOCPKT_FLUSHWRITE;
      wake_reader ();
    }
  return 0;
}

static error_t
ptyio_suspend_physical_output ()
{
  if (packet_mode)
    {
      control_byte &= ~TIOCPKT_START;
      control_byte |= TIOCPKT_STOP;
      output_stopped = 1;
      wake_reader ();
    }
  return 0;
}

static int
ptyio_pending_output_size ()
{
  /* We don't maintain any pending output buffer separate from the outputq. */
  return 0;
}

static error_t
ptyio_notice_input_flushed ()
{
  if (packet_mode)
    {
      control_byte |= TIOCPKT_FLUSHREAD;
      wake_reader ();
    }
  return 0;
}

static error_t
ptyio_assert_dtr ()
{
  dtr_on = 1;
  return 0;
}

static error_t
ptyio_desert_dtr ()
{
  dtr_on = 0;
  wake_reader ();
  return 0;
}

static error_t
ptyio_set_bits (struct termios *state)
{
  if (packet_mode)
    {
      int wakeup = 0;
      int stop = ((state->c_iflag & IXON)
		  && CCEQ (state->c_cc[VSTOP], CHAR_DC3)
		  && CCEQ (state->c_cc[VSTART], CHAR_DC1));

      if (external_processing)
	{
	  control_byte |= TIOCPKT_IOCTL;
	  wakeup = 1;
	}

      if (pktnostop && stop)
	{
	  pktnostop = 0;
	  control_byte |= TIOCPKT_DOSTOP;
	  control_byte &= ~TIOCPKT_NOSTOP;
	  wakeup = 1;
	}
      else if (!pktnostop && !stop)
	{
	  pktnostop = 1;
	  control_byte |= TIOCPKT_NOSTOP;
	  control_byte &= ~TIOCPKT_DOSTOP;
	  wakeup = 1;
	}

      if (wakeup)
	wake_reader ();
    }
  return 0;
}

/* These do nothing.  In BSD the associated ioctls get errors, but
   I'd rather just ignore them. */
static error_t
ptyio_set_break ()
{
  return 0;
}

static error_t
ptyio_clear_break ()
{
  return 0;
}

static error_t
ptyio_mdmctl (int a, int b)
{
  return 0;
}

static error_t
ptyio_mdmstate (int *state)
{
  *state = 0;
  return 0;
}

const struct bottomhalf ptyio_bottom =
{
  TERM_ON_MASTERPTY,
  ptyio_init,
  NULL,				/* fini */
  NULL,				/* gwinsz */
  ptyio_start_output,
  ptyio_set_break,
  ptyio_clear_break,
  ptyio_abandon_physical_output,
  ptyio_suspend_physical_output,
  ptyio_pending_output_size,
  ptyio_notice_input_flushed,
  ptyio_assert_dtr,
  ptyio_desert_dtr,
  ptyio_set_bits,
  ptyio_mdmctl,
  ptyio_mdmstate,
};




/* I/O interface for pty master nodes */

/* Validation has already been done by trivfs_S_io_read. */
error_t
pty_io_read (struct trivfs_protid *cred,
	     char **data,
	     mach_msg_type_number_t *datalen,
	     mach_msg_type_number_t amount)
{
  int size;

  pthread_mutex_lock (&global_lock);

  if ((cred->po->openmodes & O_READ) == 0)
    {
      pthread_mutex_unlock (&global_lock);
      return EBADF;
    }

  while (!control_byte
	 && (termflags & TTY_OPEN)
	 && (!qsize (outputq) || (termflags & USER_OUTPUT_SUSP)))
    {
      if (cred->po->openmodes & O_NONBLOCK)
	{
	  pthread_mutex_unlock (&global_lock);
	  return EWOULDBLOCK;
	}
      pty_read_blocked = 1;
      if (pthread_hurd_cond_wait_np (&pty_read_wakeup, &global_lock))
	{
	  pthread_mutex_unlock (&global_lock);
	  return EINTR;
	}
    }

  if (!(termflags & TTY_OPEN) && !qsize (outputq))
    {
      pthread_mutex_unlock (&global_lock);
      return EIO;
    }

  if (control_byte)
    {
      size = 1;
      if (packet_mode && (control_byte & TIOCPKT_IOCTL))
	size += sizeof (struct termios);
    }
  else
    {
      size = qsize (outputq);
      if (packet_mode || user_ioctl_mode)
	size++;
    }

  if (size > amount)
    size = amount;
  if (size > *datalen)
    *data = mmap (0, size, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  *datalen = size;

  if (control_byte)
    {
      **data = control_byte;
      if (packet_mode && (control_byte & TIOCPKT_IOCTL))
	memcpy (*data + 1, &termstate, size - 1);
      control_byte = 0;
    }
  else
    {
      char *cp = *data;

      if (packet_mode || user_ioctl_mode)
	{
	  *cp++ = TIOCPKT_DATA;
	  --size;
	}
      while (size--)
	*cp++ = dequeue (outputq);
    }

  pthread_mutex_unlock (&global_lock);
  return 0;
}


/* Validation has already been done by trivfs_S_io_write. */
error_t
pty_io_write (struct trivfs_protid *cred,
	      char *data,
	      mach_msg_type_number_t datalen,
	      mach_msg_type_number_t *amount)
{
  int i, flush;
  int cancel = 0;

  pthread_mutex_lock (&global_lock);

  if ((cred->po->openmodes & O_WRITE) == 0)
    {
      pthread_mutex_unlock (&global_lock);
      return EBADF;
    }

  if (remote_input_mode)
    {
      /* Wait for the queue to be empty */
      while (qsize (inputq) && !cancel)
	{
	  if (cred->po->openmodes & O_NONBLOCK)
	    {
	      pthread_mutex_unlock (&global_lock);
	      return EWOULDBLOCK;
	    }
	  cancel = pthread_hurd_cond_wait_np (inputq->wait, &global_lock);
	}
      if (cancel)
	{
	  pthread_mutex_unlock (&global_lock);
	  return EINTR;
	}

      for (i = 0; i < datalen; i++)
	enqueue (&inputq, data[i]);

      /* Extra garbage charater */
      enqueue (&inputq, 0);
    }
  else if (termstate.c_cflag & CREAD)
    for (i = 0; i < datalen; i++)
      {
	flush = input_character (data[i]);

	if (flush)
	  {
	    if (packet_mode)
	      {
		control_byte |= TIOCPKT_FLUSHREAD;
		wake_reader ();
	      }
	    break;
	  }
      }

  pthread_mutex_unlock (&global_lock);

  *amount = datalen;
  return 0;
}

/* Validation has already been done by trivfs_S_io_readable */
error_t
pty_io_readable (size_t *amt)
{
  pthread_mutex_lock (&global_lock);
  if (control_byte)
    {
      *amt = 1;
      if (packet_mode && (control_byte & TIOCPKT_IOCTL))
	*amt += sizeof (struct termios);
    }
  else
    *amt = qsize (outputq);
  pthread_mutex_unlock (&global_lock);
  return 0;
}

/* Validation has already been done by trivfs_S_io_select. */
error_t
pty_io_select (struct trivfs_protid *cred, mach_port_t reply,
	       struct timespec *tsp, int *type)
{
  int avail = 0;
  error_t err;

  if (*type == 0)
    return 0;

  pthread_mutex_lock (&global_lock);

  while (1)
    {
      if ((*type & SELECT_READ)
	  && (control_byte || qsize (outputq) || !(termflags & TTY_OPEN)))
	avail |= SELECT_READ;

      if ((*type & SELECT_URG) && control_byte)
	avail |= SELECT_URG;

      if ((*type & SELECT_WRITE) && (!remote_input_mode || !qsize (inputq)))
	avail |= SELECT_WRITE;

      if (avail)
	{
	  *type = avail;
	  pthread_mutex_unlock (&global_lock);
	  return 0;
	}

      ports_interrupt_self_on_port_death (cred, reply);
      pty_read_blocked = 1;
      err = pthread_hurd_cond_timedwait_np (&pty_select_wakeup, &global_lock,
					    tsp);
      if (err)
	{
	  *type = 0;
	  pthread_mutex_unlock (&global_lock);

	  if (err == ETIMEDOUT)
	    err = 0;

	  return err;
	}
    }
}

error_t
S_tioctl_tiocsig (struct trivfs_protid *cred,
		  int sig)
{
  if (!cred
      || cred->pi.bucket != term_bucket
      || cred->pi.class != pty_class)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);

  drop_output ();
  clear_queue (inputq);
  clear_queue (rawq);
  ptyio_notice_input_flushed ();
  send_signal (sig);

  pthread_mutex_unlock (&global_lock);

  return 0;
}

error_t
S_tioctl_tiocpkt (struct trivfs_protid *cred,
		  int mode)
{
  error_t err;
  if (!cred
      || cred->pi.bucket != term_bucket
      || cred->pi.class != pty_class)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);

  if (!!mode == !!packet_mode)
    err = 0;
  else if (mode && user_ioctl_mode)
    err = EINVAL;
  else
    {
      packet_mode = mode;
      control_byte = 0;
      err = 0;
    }

  pthread_mutex_unlock (&global_lock);

  return err;
}

error_t
S_tioctl_tiocucntl (struct trivfs_protid *cred,
		    int mode)
{
  error_t err;
  if (!cred
      || cred->pi.bucket != term_bucket
      || cred->pi.class != pty_class)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);

  if (!!mode == !!user_ioctl_mode)
    err = 0;
  else if (mode && packet_mode)
    err = EINVAL;
  else
    {
      user_ioctl_mode = mode;
      control_byte = 0;
      err = 0;
    }

  pthread_mutex_unlock (&global_lock);

  return err;
}

error_t
S_tioctl_tiocremote (struct trivfs_protid *cred,
		     int how)
{
  if (!cred
      || cred->pi.bucket != term_bucket
      || cred->pi.class != pty_class)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  remote_input_mode = how;
  drop_output ();
  clear_queue (inputq);
  clear_queue (rawq);
  ptyio_notice_input_flushed ();
  pthread_mutex_unlock (&global_lock);
  return 0;
}

error_t
S_tioctl_tiocext (struct trivfs_protid *cred,
		  int mode)
{
  if (!cred
      || cred->pi.bucket != term_bucket
      || cred->pi.class != pty_class)
    return EOPNOTSUPP;

  pthread_mutex_lock (&global_lock);
  if (mode && !external_processing)
    {
      if (packet_mode)
	{
	  control_byte |= TIOCPKT_IOCTL;
	  wake_reader ();
	}
      external_processing = 1;
      termstate.c_lflag |= EXTPROC;
    }
  else if (!mode && external_processing)
    {
      if (packet_mode)
	{
	  control_byte |= TIOCPKT_IOCTL;
	  wake_reader ();
	}
      external_processing = 0;
      termstate.c_lflag &= ~EXTPROC;
    }
  pthread_mutex_unlock (&global_lock);
  return 0;
}
