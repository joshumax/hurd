/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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
#include <hurd/hurd_types.h>
#include <string.h>
#include <hurd/ports.h>
#include <unistd.h>
#include <fcntl.h>
#include "term.h"

/* Set if we need a wakeup when tty output has been done */
static int pty_read_blocked = 0;

/* Wake this up when tty output occurs and pty_read_blocked is set */
static struct condition pty_read_wakeup = CONDITION_INITIALIZER;

static struct condition pty_select_wakeup = CONDITION_INITIALIZER;

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

void
ptyio_init ()
{
  condition_implies (inputq->wait, &pty_select_wakeup);
  condition_implies (&pty_read_wakeup, &pty_select_wakeup);
}
    
error_t
pty_open_hook (struct trivfs_control *cntl,
	       uid_t *uids, u_int nuids,
	       uid_t *gids, u_int ngids,
	       int flags)
{
  if ((flags & (O_READ|O_WRITE)) == 0)
    return 0;
  
  mutex_lock (&global_lock);

  if (ptyopen)
    {
      mutex_unlock (&global_lock);
      return EBUSY;
    }
    
  ptyopen = 1;
  nptyperopens++;
  report_carrier_on ();
  mutex_unlock (&global_lock);
  return 0;
}

error_t
pty_po_create_hook (struct trivfs_peropen *po)
{
  return 0;
}

error_t
pty_po_destroy_hook (struct trivfs_peropen *po)
{
  mutex_lock (&global_lock);
  nptyperopens--;
  if (!nptyperopens)
    {
      ptyopen = 0;
      report_carrier_off ();
    }
  mutex_unlock (&global_lock);
  return 0;
}

static inline void
wake_reader ()
{
  if (pty_read_blocked)
    {
      pty_read_blocked = 0;
      condition_broadcast (&pty_read_wakeup);
    }
}


/* Lower half for tty node */

static void 
ptyio_start_output ()
{
  if (packet_mode && output_stopped && (!(termflags & USER_OUTPUT_SUSP)))
    {
      control_byte &= ~TIOCPKT_STOP;
      control_byte |= TIOCPKT_START;
      output_stopped = 0;
    }
  wake_reader ();
}

static void
ptyio_abandon_physical_output ()
{
  if (packet_mode)
    {
      control_byte |= TIOCPKT_FLUSHWRITE;
      wake_reader ();
    }
}

static void
ptyio_suspend_physical_output ()
{
  if (packet_mode)
    {
      control_byte &= ~TIOCPKT_START;
      control_byte |= TIOCPKT_STOP;
      output_stopped = 1;
      wake_reader ();
    }
}

static int 
ptyio_pending_output_size ()
{
  /* We don't maintain any pending output buffer separate from the outputq. */
  return 0;
}

static void
ptyio_notice_input_flushed ()
{
  if (packet_mode)
    {
      control_byte |= TIOCPKT_FLUSHREAD;
      wake_reader ();
    }
}

static error_t 
ptyio_assert_dtr ()
{
  dtr_on = 1;
  return 0;
}

static void 
ptyio_desert_dtr ()
{
  dtr_on = 0;
  wake_reader ();
}

static void 
ptyio_set_bits ()
{
  int stop;
  
  if (packet_mode && external_processing)
    {
      control_byte |= TIOCPKT_IOCTL;

      stop = ((termstate.c_iflag & IXON) 
	      && CCEQ (termstate.c_cc[VSTOP], CHAR_DC3)
	      && CCEQ (termstate.c_cc[VSTART], CHAR_DC1));
      if (pktnostop && stop)
	{
	  pktnostop = 0;
	  control_byte |= TIOCPKT_DOSTOP;
	  control_byte &= ~TIOCPKT_NOSTOP;
	}
      else if (!pktnostop && !stop)
	{
	  pktnostop = 1;
	  control_byte |= TIOCPKT_NOSTOP;
	  control_byte &= ~TIOCPKT_DOSTOP;
	}

      wake_reader ();
    }
}

/* These do nothing.  In BSD the associated ioctls get errors, but
   I'd rather just ignore them. */
static void 
ptyio_set_break ()
{
}

static void 
ptyio_clear_break ()
{
}

static void
ptyio_mdmctl (int a, int b)
{
}

static int
ptyio_mdmstate ()
{
  return 0;
}

struct bottomhalf ptyio_bottom =
{
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
  
  mutex_lock (&global_lock);
  
  if ((cred->po->openmodes & O_READ) == 0)
    {
      mutex_unlock (&global_lock);
      return EBADF;
    }

  while (!control_byte
	 && (!qsize (outputq) || (termflags & USER_OUTPUT_SUSP)))
    {
      pty_read_blocked = 1;
      if (hurd_condition_wait (&pty_read_wakeup, &global_lock))
	{
	  mutex_unlock (&global_lock);
	  return EINTR;
	}
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
    vm_allocate (mach_task_self (), (vm_address_t *) data, size, 1);
  *datalen = size;

  if (control_byte)
    {
      **data = control_byte;
      if (packet_mode && (control_byte & TIOCPKT_IOCTL))
	bcopy (&termstate, *data + 1, size - 1);
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

  mutex_unlock (&global_lock);
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

  mutex_lock (&global_lock);
  
  if ((cred->po->openmodes & O_WRITE) == 0)
    {
      mutex_unlock (&global_lock);
      return EBADF;
    }

  if (remote_input_mode)
    {
      /* Wait for the queue to be empty */
      while (qsize (inputq) && !cancel)
	cancel = hurd_condition_wait (inputq->wait, &global_lock);
      if (cancel)
	{
	  mutex_unlock (&global_lock);
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

  mutex_unlock (&global_lock);

  *amount = datalen;
  return 0;
}

/* Validation has already been done by trivfs_S_io_readable */
error_t
pty_io_readable (int *amt)
{
  mutex_lock (&global_lock);
  if (control_byte)
    {
      *amt = 1;
      if (packet_mode && (control_byte & TIOCPKT_IOCTL))
	*amt += sizeof (struct termios);
    }
  else
    *amt = qsize (outputq);
  mutex_unlock (&global_lock);
  return 0;
}

/* Validation has already been done by trivfs_S_io_select. */
error_t
pty_io_select (struct trivfs_protid *cred, mach_port_t reply,
	       int *type, int *idtag)
{
  int avail = 0;
  
  if (*type == 0)
    return 0;

  mutex_lock (&global_lock);

  while (1)
    {
      if ((*type & SELECT_READ) && (control_byte || qsize (outputq)))
	avail |= SELECT_READ;

      if ((*type & SELECT_URG) && control_byte)
	avail |= SELECT_URG;

      if ((*type & SELECT_WRITE) && (!remote_input_mode || !qsize (inputq)))
	avail |= SELECT_WRITE;
      
      if (avail)
	{
	  *type = avail;
	  mutex_unlock (&global_lock);
	  return 0;
	}

      ports_interrupt_self_on_port_death (cred, reply);

      pty_read_blocked = 1;
      if (hurd_condition_wait (&pty_select_wakeup, &global_lock))
	{
	  *type = 0;
	  mutex_unlock (&global_lock);
	  return EINTR;
	}
    }
}

error_t
S_tioctl_tiocsig (io_t port,
		  int sig)
{
  struct trivfs_protid *cred = ports_lookup_port (term_bucket,
						  port, pty_class);
  if (!cred)
    return EOPNOTSUPP;
  
  drop_output ();
  clear_queue (inputq);
  clear_queue (rawq);
  ptyio_notice_input_flushed ();
  send_signal (sig);
  ports_port_deref (cred);
  return 0;
}

error_t
S_tioctl_tiocpkt (io_t port,
		  int mode)
{
  error_t err;
  
  struct trivfs_protid *cred = ports_lookup_port (term_bucket,
						  port, pty_class);
  if (!cred)
    return EOPNOTSUPP;
  
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
  ports_port_deref (cred);
  return err;
}

error_t
S_tioctl_tiocucntl (io_t port,
		    int mode)
{
  error_t err;
  
  struct trivfs_protid *cred = ports_lookup_port (term_bucket,
						  port, pty_class);
  if (!cred)
    return EOPNOTSUPP;
  
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
  ports_port_deref (cred);
  return err;
}

error_t
S_tioctl_tiocremote (io_t port,
		     int how)
{
  struct trivfs_protid *cred = ports_lookup_port (term_bucket,
						  port, pty_class);
  
  if (!cred)
    return EOPNOTSUPP;

  mutex_lock (&global_lock);
  remote_input_mode = how;
  drop_output ();
  clear_queue (inputq);
  clear_queue (rawq);
  ptyio_notice_input_flushed ();
  mutex_unlock (&global_lock);
  ports_port_deref (cred);
  return 0;
}

error_t
S_tioctl_tiocext (io_t port,
		  int mode)
{
  struct trivfs_protid *cred = ports_lookup_port (term_bucket,
						  port, pty_class);
  if (!cred)
    return EOPNOTSUPP;
  
  mutex_lock (&global_lock);
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
  mutex_unlock (&global_lock);
  ports_port_deref (cred);
  return 0;
}
