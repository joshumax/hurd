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

#include "term.h"
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <ctype.h>
#include <string.h>

/* Number of characters in the rawq which have been
   echoed continuously without intervening output.  */
int echo_qsize;

/* Where the output_psize was when echo_qsize was last 0. */
int echo_pstart;

/* PHYSICAL position of the terminal cursor */
int output_psize;

/* Actually drop character onto output queue.  This should be the
   only place where we actually enqueue characters on the output queue;
   it is responsible for keeping track of cursor positions. */
inline void
poutput (int c)
{
  if (termflags & FLUSH_OUTPUT)
    return;			/* never mind */

  if ((c >= ' ') && (c < '\177'))
    output_psize++;
  else if (c == '\r')
    output_psize = 0;
  else if (c == '\t')
    {
      output_psize++;
      while (output_psize % 8)
	output_psize++;
    }
  else if (c == '\b')
    output_psize--;

  enqueue (&outputq, c);
}

/* Place C on output queue, doing normal output processing.
   Only echo routines should directly call this function.  Others
   should call write_character below. */
void
output_character (int c)
{
  int oflag = termstate.c_oflag;

  /* One might think we should turn of INHDERASE here, but, no
     in U*x it is only turned off by echoed characters.
     See echo_char in input.c.  */
  if (oflag & OPOST)
    {
      /* Characters we write specially */
      if ((oflag & ONLCR) && c == '\n')
	{
	  poutput ('\r');
	  poutput ('\n');
	}
      else if (!external_processing && (oflag & OXTABS) && c == '\t')
	{
	  poutput (' ');
	  while (output_psize % 8)
	    poutput (' ');
	}
      else if ((oflag & ONOEOT) && c == CHAR_EOT)
	;
      else if ((oflag & OLCASE) && isalpha (c))
	{
	  if (isupper (c))
	    poutput ('\\');
	  else
	    c = toupper (c);
	  poutput (c);
	}
      else
	poutput (c);
    }
  else
    poutput (c);
}

/* Place C on output queue, doing normal processing.  */
void
write_character (int c)
{
  output_character (c);
  echo_qsize = 0;
  echo_pstart = output_psize;
}

/* Report the width of character C as printed by output_character,
   if output_psize were at LOC. . */
int
output_width (int c, int loc)
{
  int oflag = termstate.c_oflag;

  if (oflag & OPOST)
    {
      if ((oflag & OLCASE) && isalpha (c) && isupper (c))
	return 2;
    }
  if (c == '\t')
    {
      int n = loc + 1;
      while (n % 8)
	n++;
      return n - loc;
    }
  if ((c >= ' ') && (c < '\177'))
    return 1;
  return 0;
}



/* For ICANON mode, this holds the edited line. */
struct queue *rawq;

/* For each character in this table, if the element is set, then
   the character is EVEN */
char const char_parity[] =
{
  1, 0, 0, 1, 0, 1, 1, 0,	/* nul - bel */
  0, 1, 1, 0, 1, 0, 0, 1,	/* bs - si */
  0, 1, 1, 0, 1, 0, 0, 1,	/* dle - etb */
  1, 0, 0, 1, 0, 1, 1, 0,	/* can - us */
  0, 1, 1, 0, 1, 0, 0, 1,	/* sp - ' */
  1, 0, 0, 1, 0, 1, 1, 0,	/* ( - / */
  1, 0, 0, 1, 0, 1, 1, 0,	/* 0 - 7 */
  0, 1, 1, 0, 1, 0, 0, 1,	/* 8 - ? */
  0, 1, 1, 0, 1, 0, 0, 1,	/* @ - G */
  1, 0, 0, 1, 0, 1, 1, 0,	/* H - O */
  1, 0, 0, 1, 0, 1, 1, 0,	/* P - W */
  0, 1, 1, 0, 1, 0, 0, 1,	/* X - _ */
  1, 0, 0, 1, 0, 1, 1, 0,	/* ` - g */
  0, 1, 1, 0, 1, 0, 0, 1,	/* h - o */
  0, 1, 1, 0, 1, 0, 0, 1,	/* p - w */
  1, 0, 0, 1, 0, 1, 1, 0,	/* x - del */
};
#define checkevenpar(c) (((c)&0x80) \
			 ? !char_parity[(c)&0x7f] \
			 : char_parity[(c)&0x7f])
#define checkoddpar(c) (((c)&0x80) \
			 ? char_parity[(c)&0x7f] \
			 : !char_parity[(c)&0x7f])



/* These functions are used by both echo and erase code */

/* Tell if we should echo a character at all */
static inline int
echo_p (char c, int quoted)
{
  if (external_processing)
    return 0;
  return ((termstate.c_lflag & ECHO)
	  || (c == '\n' && (termstate.c_lflag & ECHONL) && !quoted));
}

/* Tell if this character deserves double-width cntrl processing */
static inline int
echo_double (char c, int quoted)
{
  return (iscntrl (c) && (termstate.c_lflag & ECHOCTL)
	  && !((c == '\n' || c == '\t') && !quoted));
}

/* Do a single C-h SPC C-h sequence */
static inline void
write_erase_sequence ()
{
  poutput ('\b');
  poutput (' ');
  poutput ('\b');
}

/* Echo a single character to the output */
/* If this is an echo of a character which is being hard-erased,
   set hderase.  If this is a newline or tab which should not receive
   their normal special processing, set quoted. */
static void
echo_char (char c, int hderase, int quoted)
{
  echo_qsize++;

  if (echo_p (c, quoted))
    {
      if (!hderase && (termflags & INSIDE_HDERASE))
	{
	  write_character ('/');
	  termflags &= ~INSIDE_HDERASE;
	}

      if (hderase && !(termflags & INSIDE_HDERASE))
	{
	  output_character ('\\');
	  termflags |= INSIDE_HDERASE;
	}

      /* Control characters that should use caret-letter */
      if (echo_double (c, quoted))
	{
	  output_character ('^');
	  output_character (c ^ CTRL_BIT);
	}
      else
	output_character (c);
    }
}


/* Re-echo the current rawq preceded by the VREPRINT char. */
static inline void
reprint_line ()
{
  short *cp;

  if (termstate.c_cc[VREPRINT] != _POSIX_VDISABLE
      /* XXX: Remove this -1 compatibility later */
      && termstate.c_cc[VREPRINT] != (unsigned char) -1)
    echo_char (termstate.c_cc[VREPRINT], 0, 0);
  else
    echo_char (CHAR_DC2, 0, 0);
  echo_char ('\n', 0, 0);

  echo_qsize = 0;
  echo_pstart = output_psize;

  for (cp = rawq->cs; cp != rawq->ce; cp++)
    echo_char (unquote_char (*cp), 0, char_quoted_p (*cp));
}

/* Erase a single character off the end of the rawq, and delete
   it from the screen appropriately.  Set ERASE_CHAR if this
   is being done by the VERASE character, to that character. */
static void
erase_1 (char erase_char)
{
  int quoted;
  char c;
  quoted_char cq;

  if (qsize (rawq) == 0)
    return;

  cq = queue_erase (rawq);
  c = unquote_char (cq);
  quoted = char_quoted_p (cq);

  if (!echo_p (c, quoted))
    return;

  /* The code for WERASE knows that we echo erase_char iff
     !ECHOPRT && !ECHO.  */

  if (echo_qsize--)
    {
      if (termstate.c_lflag & ECHOPRT)
	echo_char (c, 1, quoted);
      else if (!(termstate.c_lflag & ECHOE) && erase_char)
	echo_char (erase_char, 0, 0);
      else
	{
	  int nerase;

	  if (echo_double (c, quoted))
	    nerase = 2;
	  else if (c == '\t')
	    {
	      quoted_char *cp;
	      int loc = echo_pstart;

	      for (cp = rawq->ce - echo_qsize; cp != rawq->ce; cp++)
		loc += (echo_double (unquote_char (*cp), char_quoted_p (*cp))
			? 2
			: output_width (*cp, loc));
	      nerase = output_psize - loc;
	    }
	  else
	    nerase = output_width (c, output_psize);

	  while (nerase--)
	    write_erase_sequence ();
	}
      if (echo_qsize == 0)
	assert_backtrace (echo_pstart == output_psize);
    }
  else
    reprint_line ();
}


/* Place newly input character C on the input queue.  If all remaining
   pending input characters should be dropped, return 1; else return
   0.  */
int
input_character (int c)
{
  int lflag = termstate.c_lflag;
  int iflag = termstate.c_iflag;
  int cflag = termstate.c_cflag;
  cc_t *cc = termstate.c_cc;
  struct queue **qp = (lflag & ICANON) ? &rawq : &inputq;
  int flush = 0;

  /* Handle parity errors */
  if ((iflag & INPCK)
      && ((cflag & PARODD) ? checkoddpar (c) : checkevenpar (c)))
    {
      if (iflag & IGNPAR)
	goto alldone;
      else if (iflag & PARMRK)
	{
	  enqueue_quote (qp, CHAR_USER_QUOTE);
	  enqueue_quote (qp, '\0');
	  enqueue_quote (qp, c);
	  goto alldone;
	}
      else
	c = 0;
    }

  /* Check to see if we should send IXOFF */
  if ((iflag & IXOFF)
      && !qavail (*qp)
      && (cc[VSTOP] != _POSIX_VDISABLE)
      /* XXX: Remove this -1 compatibility later */
      && (cc[VSTOP] != (unsigned char) -1))
    {
      poutput (cc[VSTOP]);
      termflags |= SENT_VSTOP;
    }

  /* Character mutations */
  if (!(iflag & ISTRIP) && (iflag & PARMRK) && (c == CHAR_USER_QUOTE))
    enqueue_quote (qp, CHAR_USER_QUOTE); /* cause doubling */

  if (iflag & ISTRIP)
    c &= 0x7f;

  /* Handle LNEXT right away */
  if (!external_processing && (termflags & LAST_LNEXT))
    {
      enqueue_quote (qp, c);
      echo_char (c, 0, 1);
      termflags &= ~LAST_LNEXT;
      goto alldone;
    }

  /* Mutate ILCASE */
  if (!external_processing && (iflag & ILCASE) && isalpha(c))
    {
      if (termflags & LAST_SLASH)
	erase_1 (0);	/* remove the slash from input */
      else
	c = isupper(c) ? tolower (c) : c;
    }

  /* IEXTEN control chars */
  if (!external_processing && (lflag & IEXTEN))
    {
      if (CCEQ (cc[VLNEXT], c))
	{
	  if (lflag & ECHO)
	    {
	      poutput ('^');
	      poutput ('\b');
	    }
	  termflags |= LAST_LNEXT;
	  goto alldone;
	}

      if (CCEQ (cc[VDISCARD], c))
	{
	  if (termflags & FLUSH_OUTPUT)
	    termflags &= ~FLUSH_OUTPUT;
	  else
	    {
	      drop_output ();
	      poutput (cc[VDISCARD]);
	      termflags |= FLUSH_OUTPUT;
	    }
	  goto alldone;
	}
    }

  /* Signals */
  if (!external_processing && (lflag & ISIG))
    {
      if (CCEQ (cc[VINTR], c) || CCEQ (cc[VQUIT], c))
	{
	  if (!(lflag & NOFLSH))
	    {
	      drop_output ();
	      clear_queue (inputq);
	      clear_queue (rawq);
	      flush = 1;
	    }
	  echo_char (c, 0, 0);
	  echo_qsize = 0;
	  echo_pstart = output_psize;
	  send_signal (CCEQ (cc[VINTR], c) ? SIGINT : SIGQUIT);
	  goto alldone;
	}

      if (CCEQ (cc[VSUSP], c))
	{
	  if (!(lflag & NOFLSH))
	    {
	      flush = 1;
	      clear_queue (inputq);
	      clear_queue (rawq);
	    }
	  echo_char (c, 0, 0);
	  echo_qsize = 0;
	  echo_pstart = output_psize;
	  send_signal (SIGTSTP);
	  goto alldone;
	}
    }

  /* IXON */
  if (!external_processing && (iflag & IXON))
    {
      if (CCEQ (cc[VSTOP], c))
	{
	  if (CCEQ(cc[VSTART], c) && (termflags & USER_OUTPUT_SUSP))
	    /* Toggle if VSTART == VSTOP.  Alldone code always turns
	       off USER_OUTPUT_SUSP. */
	    goto alldone;

	  termflags |= USER_OUTPUT_SUSP;
	  (*bottom->suspend_physical_output) ();
	  return flush;
	}
      if (CCEQ (cc[VSTART], c))
	goto alldone;
    }

  if (!external_processing)
    {
      /* Newline and carriage-return frobbing */
      if (c == '\r')
	{
	  if (iflag & ICRNL)
	    c = '\n';
	  else if (iflag & IGNCR)
	    goto alldone;
	}
      else if ((c == '\n') && (iflag & INLCR))
	c = '\r';

    }

  /* Canonical mode processing */
  if (!external_processing && (lflag & ICANON))
    {
      if (CCEQ (cc[VERASE], c))
	{
	  if (qsize(rawq))
	    erase_1 (c);
	  if (!(termflags & LAST_SLASH)
	      || !(lflag & IEXTEN))
	    goto alldone;
	}

      if (CCEQ (cc[VKILL], c))
	{
	  if (!(termflags & LAST_SLASH)
	      || !(lflag & IEXTEN))
	    {
	      if ((lflag & ECHOKE) && !(lflag & ECHOPRT)
		  && (echo_qsize == qsize (rawq)))
		{
		  while (output_psize > echo_pstart)
		    write_erase_sequence ();
		}
	      else
		{
		  echo_char (c, 0, 0);
		  if ((lflag & ECHOK) || (lflag & ECHOKE))
		    echo_char ('\n', 0, 0);
		}
	      clear_queue (rawq);
	      echo_qsize = 0;
	      echo_pstart = output_psize;
	      termflags &= ~(LAST_SLASH|LAST_LNEXT|INSIDE_HDERASE);
	      goto alldone;
	    }
	  else
	    erase_1 (0);	/* remove \ */
	}

      if (CCEQ (cc[VWERASE], c))
	{
	  /* If we are not going to echo the erase, then
	     echo a WERASE character right now.  (If we
	     passed it to erase_1; it would echo it multiple
	     times.) */
	  if (!(lflag & (ECHOPRT|ECHOE)))
	    echo_char (cc[VWERASE], 0, 1);

	  /* Erase whitespace */
	  while (qsize (rawq) && isblank (unquote_char (rawq->ce[-1])))
	    erase_1 (0);

	  /* Erase word. */
	  if (lflag & ALTWERASE)
	    /* For ALTWERASE, we erase back to the first blank */
	    while (qsize (rawq) && !isblank (unquote_char (rawq->ce[-1])))
	      erase_1 (0);
	  else
	    /* For regular WERASE, we erase back to the first nonalpha/_ */
	    while (qsize (rawq) && !isblank (unquote_char (rawq->ce[-1]))
		   && (isalnum (unquote_char (rawq->ce[-1]))
		       || (unquote_char (rawq->ce[-1]) != '_')))
	      erase_1 (0);

	  goto alldone;
	}

      if (CCEQ (cc[VREPRINT], c) && (lflag & IEXTEN))
	{
	  reprint_line ();
	  goto alldone;
	}

      if (CCEQ (cc[VSTATUS], c) && (lflag & ISIG) && (lflag & IEXTEN))
	{
	  send_signal (SIGINFO);
	  goto alldone;
	}
    }

  /* Now we have a character intended as input.  See if it will fit. */
  if (!qavail (*qp))
    {
      if (iflag & IMAXBEL)
	poutput ('\a');
      else
	{
	  /* Drop everything */
	  drop_output ();
	  clear_queue (inputq);
	  clear_queue (rawq);
	  echo_pstart = 0;
	  echo_qsize = 0;
	  flush = 1;
	}
      goto alldone;
    }

  /* Echo */
  echo_char (c, 0, 0);
  if (CCEQ (cc[VEOF], c) && (lflag & ECHO))
    {
      /* Special bizarre echo processing for VEOF character. */
      int n;
      n = echo_double (c, 0) ? 2 : output_width (c, output_psize);
      while (n--)
	poutput ('\b');
    }

  /* Put character on input queue */
  enqueue (qp, c);

  /* Check for break characters in canonical input processing */
  if (lflag & ICANON)
    {
      if (CCEQ (cc[VEOL], c)
	  || CCEQ (cc[VEOL2], c)
	  || CCEQ (cc[VEOF], c)
	  || c == '\n')
	/* Make input available */
	while (qsize (rawq))
	  enqueue (&inputq, dequeue (rawq));
    }

alldone:
  /* Restart output */
  if ((iflag & IXANY) || (CCEQ (cc[VSTART], c)))
    termflags &= ~USER_OUTPUT_SUSP;
  (*bottom->start_output) ();

  return flush;
}


/* This is called by the lower half when a break is received. */
void
input_break ()
{
  struct queue **qp = termstate.c_lflag & ICANON ? &rawq : &inputq;

  /* Don't do anything if IGNBRK is set. */
  if (termstate.c_iflag & IGNBRK)
    return;

  /* If BRKINT is set, then flush queues and send SIGINT. */
  if (termstate.c_iflag & BRKINT)
    {
      drop_output ();
      /* XXX drop pending input How?? */
      send_signal (SIGINT);
      return;
    }

  /* A break is then read as a null byte; marked specially if PARMRK is set. */
  if (termstate.c_iflag & PARMRK)
    {
      enqueue_quote (qp, CHAR_USER_QUOTE);
      enqueue_quote (qp, '\0');
    }
  enqueue_quote (qp, '\0');
}

/* Called when a character is received with a framing error. */
void
input_framing_error (int c)
{
  struct queue **qp = termstate.c_lflag & ICANON ? &rawq : &inputq;

  /* Ignore it if IGNPAR is set. */
  if (termstate.c_iflag & IGNPAR)
    return;

  /* If PARMRK is set, pass it specially marked. */
  if (termstate.c_iflag & PARMRK)
    {
      enqueue_quote (qp, CHAR_USER_QUOTE);
      enqueue_quote (qp, '\0');
      enqueue_quote (qp, c);
    }
  else
    /* Otherwise, it looks like a null. */
    enqueue_quote (qp, '\0');
}

/* Copy the characters in RAWQ to the end of INPUTQ and clear RAWQ. */
void
copy_rawq ()
{
  while (qsize (rawq))
    enqueue (&inputq, dequeue (rawq));
}

/* Process all the characters in INPUTQ as if they had just been read. */
void
rescan_inputq ()
{
  short *buf;
  int i, n;

  n = qsize (inputq);
  buf = alloca (n * sizeof (quoted_char));
  memcpy (buf, inputq->cs, n * sizeof (quoted_char));
  clear_queue (inputq);

  for (i = 0; i < n; i++)
    input_character (unquote_char (buf[i]));
}


error_t
drop_output ()
{
  error_t err = (*bottom->abandon_physical_output) ();
  if (!err)
    clear_queue (outputq);
  return err;
}


error_t
drain_output ()
{
  int cancel = 0;

  while ((qsize (outputq) || (*bottom->pending_output_size) ())
	 && (!(termflags & NO_CARRIER) || (termstate.c_cflag & CLOCAL))
	 && !cancel)
    cancel = pthread_hurd_cond_wait_np (outputq->wait, &global_lock);

  return cancel ? EINTR : 0;
}

/* Create and return a new queue. */
struct queue *
create_queue (int size, int lowat, int hiwat)
{
  struct queue *q;

  q = malloc (sizeof (struct queue) + size * sizeof (quoted_char));
  assert_backtrace (q);

  q->susp = 0;
  q->lowat = lowat;
  q->hiwat = hiwat;
  q->cs = q->ce = q->array;
  q->arraylen = size;
  q->wait = malloc (sizeof (pthread_cond_t));
  assert_backtrace (q->wait);

  pthread_cond_init (q->wait, NULL);
  return q;
}

/* Make Q able to have more characters added to it. */
struct queue *
reallocate_queue (struct queue *q)
{
  int len;
  struct queue *newq;

  len = qsize (q);

  if (len < q->arraylen / 2)
    {
      /* Shift the characters to the front of
	 the queue. */
      memmove (q->array, q->cs, len * sizeof (quoted_char));
      q->cs = q->array;
      q->ce = q->cs + len;
    }
  else
    {
      /* Make the queue twice as large. */
      newq = malloc (sizeof (struct queue)
		     + q->arraylen * 2 * sizeof (quoted_char));
      newq->susp = q->susp;
      newq->lowat = q->lowat;
      newq->hiwat = q->hiwat;
      newq->cs = newq->array;
      newq->ce = newq->array + len;
      newq->arraylen = q->arraylen * 2;
      newq->wait = q->wait;
      memmove (newq->array, q->cs, len * sizeof (quoted_char));
      free (q);
      q = newq;
    }
  return q;
}
