/*
   Copyright (C) 1995,96,98,99,2000,01,02 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG and Marcus Brinkmann.

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

/* Handle carrier dropped (at least EIO errors in read, write) correctly.  */

#include <termios.h>

#include <assert-backtrace.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <stdio.h>

#include <pthread.h>

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/io.h>
#include <hurd/tioctl.h>

#include "term.h"


/* The thread asserting the DTR and performing all reads.  Only
   different from MACH_PORT_NULL if thread is live and blocked.  */
thread_t reader_thread = MACH_PORT_NULL;

/* The Hurd file_t representing the terminal.  If this is not
   MACH_PORT_NULL, it has the additional meaning that the DTR is
   asserted.  */
static file_t ioport = MACH_PORT_NULL;

/* Each bit represents a supported tioctl call in the underlying node.
   If we detect that a tioctl is not supported, we clear the bit in
   tioc_caps (which is initialized at every open).  */
#define TIOC_CAP_OUTQ  0x001
#define TIOC_CAP_START 0x002
#define TIOC_CAP_STOP  0x004
#define TIOC_CAP_FLUSH 0x008
#define TIOC_CAP_CBRK  0x010
#define TIOC_CAP_SBRK  0x020
#define TIOC_CAP_MODG  0x040
#define TIOC_CAP_MODS  0x080
#define TIOC_CAP_GETA  0x100
#define TIOC_CAP_SETA  0x200
#define TIOC_CAP_GWINSZ 0x400
unsigned int tioc_caps;

/* The thread performing all writes.  Only different from
   MACH_PORT_NULL if thread is live and blocked.  */
thread_t writer_thread = MACH_PORT_NULL;

/* This flag is set if the output was suspended.  */
static int output_stopped;
static pthread_cond_t hurdio_writer_condition;

/* Hold the amount of bytes that are currently in the progress of
   being written.  May be set to zero while you hold the global lock
   to drain the pending output buffer.  */
size_t npending_output;

/* True if we should assert the dtr.  */
int assert_dtr;
static pthread_cond_t hurdio_assert_dtr_condition;


/* Forward */
static error_t hurdio_desert_dtr ();
static void *hurdio_reader_loop (void *arg);
static void *hurdio_writer_loop (void *arg);
static error_t hurdio_set_bits (struct termios *state);


static error_t
hurdio_init (void)
{
  pthread_t thread;
  error_t err;

  pthread_cond_init (&hurdio_writer_condition, NULL);
  pthread_cond_init (&hurdio_assert_dtr_condition, NULL);

  err = pthread_create (&thread, NULL, hurdio_reader_loop, NULL);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }
  err = pthread_create (&thread, NULL, hurdio_writer_loop, NULL);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }
  return 0;
}

static error_t
hurdio_fini (void)
{
  hurdio_desert_dtr ();
  writer_thread = MACH_PORT_NULL;
  /* XXX destroy reader thread too */
  return 0;
}

static error_t
hurdio_gwinsz (struct winsize *size)
{
  if (tioc_caps & TIOC_CAP_GWINSZ)
    {
      error_t err = tioctl_tiocgwinsz (ioport, size);
      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	{
	  tioc_caps &= ~TIOC_CAP_GWINSZ;
	  err = EOPNOTSUPP;
	}
      return err;
    }
  return EOPNOTSUPP;
}


/* Assert the DTR if necessary.  Must be called with global lock held.  */
static void
wait_for_dtr (void)
{
  while (!assert_dtr)
    pthread_hurd_cond_wait_np (&hurdio_assert_dtr_condition, &global_lock);
  assert_dtr = 0;

  if (tty_arg == 0)
    ioport = termctl->underlying;
  else
    {
      /* Open the file in blocking mode, so that the carrier is
	 established as well.  */
      ioport = file_name_lookup (tty_arg, O_READ|O_WRITE, 0);
      if (ioport == MACH_PORT_NULL)
	{
	  report_carrier_error (errno);
	  return;
	}
    }


  error_t err;
  struct termios state = termstate;

  /* Assume that we have a full blown terminal initially.  */
  tioc_caps = ~0;

  /* Set terminal in raw mode etc.  */
  err = hurdio_set_bits (&state);
  if (err)
    report_carrier_error (err);
  else
    {
      termstate = state;

      /* Signal that we have a carrier.  */
      report_carrier_on ();

      /* Signal that the writer thread should resume its work.  */
      pthread_cond_broadcast (&hurdio_writer_condition);
    }
}


/* Read and enqueue input characters.  Is also responsible to assert
   the DTR if necessary.  */
static void *
hurdio_reader_loop (void *arg)
{
  /* XXX The input buffer has 256 bytes.  */
#define BUFFER_SIZE 256
  char buffer[BUFFER_SIZE];
  char *data;
  size_t datalen;
  error_t err;

  pthread_mutex_lock (&global_lock);
  reader_thread = mach_thread_self ();

  while (1)
    {
      /* We can only start when the DTR has been asserted.  */
      while (ioport == MACH_PORT_NULL)
	wait_for_dtr ();
      pthread_mutex_unlock (&global_lock);

      data = buffer;
      datalen = BUFFER_SIZE;

      err = io_read (ioport, &data, &datalen, -1, BUFFER_SIZE);

      pthread_mutex_lock (&global_lock);
      /* Error or EOF can mean the carrier has been dropped.  */
      if (err || !datalen)
	hurdio_desert_dtr ();
      else
	{
	  if (termstate.c_cflag & CREAD)
	    {
	      int i;

	      for (i = 0; i < datalen; i++)
		if (input_character (data[i]))
		  break;
	    }

	  if (data != buffer)
	    vm_deallocate (mach_task_self(), (vm_address_t) data, datalen);
	}
    }
#undef BUFFER_SIZE

  return 0;
}


/* Output characters.  */
static void *
hurdio_writer_loop (void *arg)
{
  /* XXX The output buffer has 256 bytes.  */
#define BUFFER_SIZE 256
  char *bufp;
  char pending_output[BUFFER_SIZE];
  size_t amount;
  error_t err;
  int size;
  int npending_output_copy;
  mach_port_t ioport_copy;

  pthread_mutex_lock (&global_lock);
  writer_thread = mach_thread_self ();

  while (1)
    {
      while (writer_thread != MACH_PORT_NULL
	     && (ioport == MACH_PORT_NULL || !qsize (outputq)
		 || output_stopped))
	pthread_hurd_cond_wait_np (&hurdio_writer_condition, &global_lock);
      if (writer_thread == MACH_PORT_NULL) /* A sign to die.  */
	return 0;

      /* Copy characters onto PENDING_OUTPUT, not bothering
	 those already there. */
      size = qsize (outputq);

      if (size + npending_output > BUFFER_SIZE)
	size = BUFFER_SIZE - npending_output;

      bufp = pending_output + npending_output;
      npending_output += size;
      /* We need to save these values, as otherwise there are races
	 with hurdio_abandon_physical_output or hurdio_desert_dtr,
	 which might overwrite the static variables.  */
      npending_output_copy = npending_output;
      ioport_copy = ioport;
      mach_port_mod_refs (mach_task_self (), ioport_copy,
			  MACH_PORT_RIGHT_SEND, 1);

      while (size--)
	*bufp++ = dequeue (outputq);

      /* Submit all the outstanding characters to the I/O port.  */
      pthread_mutex_unlock (&global_lock);
      err = io_write (ioport_copy, pending_output, npending_output_copy,
		      -1, &amount);
      pthread_mutex_lock (&global_lock);

      mach_port_mod_refs (mach_task_self (), ioport_copy,
			  MACH_PORT_RIGHT_SEND, -1);
      if (err)
	hurdio_desert_dtr ();
      else
	{
	  /* Note that npending_output might be set to null in the
	     meantime by hurdio_abandon_physical_output.  */
	  if (amount >= npending_output)
	    {
	      npending_output = 0;
	      pthread_cond_broadcast (outputq->wait);
	      pthread_cond_broadcast (&select_alert);
	    }
	  else
	    {
	      /* Copy the characters that didn't get output
		 to the front of the array.  */
	      npending_output -= amount;
	      memmove (pending_output, pending_output + amount,
		       npending_output);
	    }
	}
    }
#undef BUFFER_SIZE

  return 0;
}


/* If there are characters on the output queue, then send them.  Is
   called with global lock held.  */
static error_t
hurdio_start_output ()
{
  /* If the output was suspended earlier and not anymore, we have to
     tell the underlying port to resume it.  */
  if (output_stopped && !(termflags & USER_OUTPUT_SUSP))
    {
      if (tioc_caps & TIOC_CAP_START)
	{
	  error_t err = tioctl_tiocstart (ioport);
	  if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	    tioc_caps &= ~TIOC_CAP_START;
	}
      output_stopped = 0;
    }
  pthread_cond_broadcast (&hurdio_writer_condition);
  return 0;
}


/* Stop carrier on the line.  Is called with global lock held.  */
static error_t
hurdio_set_break ()
{
  if (tioc_caps & TIOC_CAP_SBRK)
    {
      error_t err = tioctl_tiocsbrk (ioport);
      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	tioc_caps &= ~TIOC_CAP_SBRK;
      else if (err)
	return err;
    }
  return 0;
}


/* Reassert carrier on the line.  Is called with global lock held.  */
static error_t
hurdio_clear_break ()
{
  if (tioc_caps & TIOC_CAP_CBRK)
    {
      error_t err = tioctl_tioccbrk (ioport);
      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	tioc_caps &= ~TIOC_CAP_CBRK;
      else if (err)
	return err;
    }
  return 0;
}


/* This is called when output queues are being flushed.  But there may
   be pending output which is sitting in a device buffer or other
   place lower down than the terminal's output queue; so this is
   called to flush whatever other such things may be going on.  Is
   called with global lock held.  */
static error_t
hurdio_abandon_physical_output ()
{
  if (tioc_caps & TIOC_CAP_FLUSH)
    {
      error_t err = tioctl_tiocflush (ioport, O_WRITE);
      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	tioc_caps &= ~TIOC_CAP_FLUSH;
      else if (err)
	return err;
    }

  /* Make sure that an incomplete write will not be finished.
     hurdio_writer_loop must take care that meddling with
     npending_output here does not introduce any races.  */
  npending_output = 0;
  return 0;
}


/* Tell the underlying port to suspend all pending output, and stop
   output in the bottom handler as well.  Is called with the global
   lock held.  */
static error_t
hurdio_suspend_physical_output ()
{
  if (!output_stopped)
    {
      if (tioc_caps & TIOC_CAP_STOP)
	{
	  error_t err = tioctl_tiocstop (ioport);
	  if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	    tioc_caps &= ~TIOC_CAP_STOP;
	  else if (err)
	    return err;
	}
      output_stopped = 1;
    }
  return 0;
}

/* This is called to notify the bottom half when an input flush has
   occurred.  It is necessary to support pty packet mode.  */
static error_t
hurdio_notice_input_flushed ()
{
  if (tioc_caps & TIOC_CAP_FLUSH)
    {
      error_t err = tioctl_tiocflush (ioport, O_READ);
      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	tioc_caps &= ~TIOC_CAP_FLUSH;
      else if (err)
	return err;
    }
  return 0;
}


/* Determine the number of bytes of output pending.  */
static int
hurdio_pending_output_size ()
{
  int queue_size = 0;

  if (tioc_caps & TIOC_CAP_OUTQ)
    {
      error_t err = tioctl_tiocoutq (ioport, &queue_size);
      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	tioc_caps &= ~TIOC_CAP_OUTQ;
      else if (err)
	queue_size = 0;
    }
  /* We can not get the correct number, so let's try a guess.  */
  return queue_size + npending_output;
}


/* Desert the DTR.  Is called with global lock held.  */
static error_t
hurdio_desert_dtr ()
{
  if (writer_thread != MACH_PORT_NULL)
    hurd_thread_cancel (writer_thread);
  if (reader_thread != MACH_PORT_NULL)
    hurd_thread_cancel (reader_thread);
  if (ioport != MACH_PORT_NULL && tty_arg)
    {
      mach_port_deallocate (mach_task_self (), ioport);
      ioport = MACH_PORT_NULL;
    }
  /* If we are called after hurdio_assert_dtr before the reader thread
     had a chance to wake up and open the port, we can prevent it from
     doing so by clearing this flag.  */
  assert_dtr = 0;
  report_carrier_off ();
  return 0;
}


static error_t
hurdio_assert_dtr ()
{
  if (ioport == MACH_PORT_NULL)
    {
      assert_dtr = 1;
      pthread_cond_signal (&hurdio_assert_dtr_condition);
    }

  return 0;
}


/* Adjust physical state on the basis of the terminal state.
   Where it isn't possible, mutate terminal state to match
   reality. */
static error_t
hurdio_set_bits (struct termios *state)
{
  error_t err;
  struct termios ttystat;
  /* This structure equals how the Hurd tioctl_tiocgeta/seta split up
     a termios structure into RPC arguments.  */
  struct hurd_termios
  {
    modes_t modes;
    ccs_t ccs;
    speeds_t speeds;
  } *hurd_ttystat = (struct hurd_termios *) &ttystat;

  if (!(state->c_cflag & CIGNORE) && ioport != MACH_PORT_NULL)
    {

      /* If we can not get the terminal state, it doesn't make sense
	 to attempt to change it.  Even if we could change it we
	 wouldn't know what changes took effect.  */
      if (!(tioc_caps & TIOC_CAP_GETA))
	/* XXX Maybe return an error here, but then we must do the
	   right thing in users.c.  */
	return 0;

      err = tioctl_tiocgeta (ioport, hurd_ttystat->modes,
			     hurd_ttystat->ccs, hurd_ttystat->speeds);
      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	{
	  tioc_caps &= ~TIOC_CAP_GETA;
	  /* XXX Maybe return an error here, but then we must do the
	     right thing in users.c.  */
	  return 0;
	}
      else if (err)
	return err;

      /* If possible, change the state.  Otherwise we will just make
	 termstate match reality below.  */
      if (tioc_caps & TIOC_CAP_SETA)
	{
	  if (state->__ispeed)
	    hurd_ttystat->speeds[0] = state->__ispeed;
	  if (state->__ospeed)
	    hurd_ttystat->speeds[1] = state->__ospeed;
	  cfmakeraw (&ttystat);
	  ttystat.c_cflag = state->c_cflag &~ HUPCL;

	  err = tioctl_tiocseta (ioport, hurd_ttystat->modes,
				 hurd_ttystat->ccs, hurd_ttystat->speeds);
	  if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	    tioc_caps &= ~TIOC_CAP_SETA;
	  else if (err)
	    return err;

	  /* Refetch the terminal state.  */
	  err = tioctl_tiocgeta (ioport, hurd_ttystat->modes,
				 hurd_ttystat->ccs, hurd_ttystat->speeds);
	  if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	    tioc_caps &= ~TIOC_CAP_GETA;
	  else if (err)
	    return err;
	}

      /* And now make termstate match reality.  */
      *state = ttystat;
    }

  return 0;
}

/* Diddle the modem control bits.  If HOW is MDMCTL_BIC, the bits set
   in BITS should be cleared.  If HOW is MDMCTL_BIS, the bits in BITS
   should be set.  Otherwise, bits that are set in BITS should be set,
   and the others cleared.  */
static error_t
hurdio_mdmctl (int how, int bits)
{
  error_t err;
  int oldbits, newbits;

  if (tioc_caps & TIOC_CAP_MODS)
    {
      if ((how == MDMCTL_BIS) || (how == MDMCTL_BIC))
	{
	  if (tioc_caps & TIOC_CAP_MODG)
	    {
	      error_t err = tioctl_tiocmodg (ioport, &oldbits);
	      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
		{
		  tioc_caps &= ~TIOC_CAP_MODG;
		  return EOPNOTSUPP;
		}
	      else if (err)
		return err;
	    }
	  else
	    return EOPNOTSUPP;
	}

      if (how == MDMCTL_BIS)
	newbits = (oldbits | bits);
      else if (how == MDMCTL_BIC)
	newbits = (oldbits &= ~bits);
      else
	newbits = bits;

      err = tioctl_tiocmods (ioport, newbits);
      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	tioc_caps &= ~TIOC_CAP_MODS;
      else if (err)
	return err;
    }
  return 0;
}


static int
hurdio_mdmstate ()
{
  int oldbits;

  if (tioc_caps & TIOC_CAP_MODG)
    {
      error_t err = tioctl_tiocmodg (ioport, &oldbits);
      if (err && (err == EMIG_BAD_ID || err == EOPNOTSUPP))
	tioc_caps &= ~TIOC_CAP_MODG;
      else if (err)
	return 0;  /* XXX What else can we do?  */
    }
  return 0;
}



const struct bottomhalf hurdio_bottom =
{
  TERM_ON_HURDIO,
  hurdio_init,
  hurdio_fini,
  hurdio_gwinsz,
  hurdio_start_output,
  hurdio_set_break,
  hurdio_clear_break,
  hurdio_abandon_physical_output,
  hurdio_suspend_physical_output,
  hurdio_pending_output_size,
  hurdio_notice_input_flushed,
  hurdio_assert_dtr,
  hurdio_desert_dtr,
  hurdio_set_bits,
  hurdio_mdmctl,
  hurdio_mdmstate,
};
