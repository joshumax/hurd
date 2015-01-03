/* input.c - Input component of a virtual console.
   Copyright (C) 2002 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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

#include <iconv.h>
#include <error.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <sys/types.h>

#include <pthread.h>

#include "input.h"

struct input
{
  pthread_mutex_t lock;

  pthread_cond_t data_available;
  pthread_cond_t space_available;
#define INPUT_QUEUE_SIZE 300
  char buffer[INPUT_QUEUE_SIZE];
  int full;
  size_t size;

  /* The state of the conversion of input characters.  */
  iconv_t cd;
  /* The conversion routine might refuse to handle some incomplete
     multi-byte or composed character at the end of the buffer, so we
     have to keep them around.  */
  char *cd_buffer;
  size_t cd_size;
  size_t cd_allocated;
};

/* Create a new virtual console input queue, with the system encoding
   being ENCODING.  */
error_t input_create (input_t *r_input, const char *encoding)
{
  input_t input = calloc (1, sizeof *input);
  if (!input)
    return ENOMEM;

  pthread_mutex_init (&input->lock, NULL);
  pthread_cond_init (&input->data_available, NULL);
  pthread_cond_init (&input->space_available, NULL);

  input->cd = iconv_open (encoding, "UTF-8");
  if (input->cd == (iconv_t) -1)
    {
      free (input);
      return errno;
    }

  *r_input = input;
  return 0;
}

/* Destroy the input queue INPUT.  */
void input_destroy (input_t input)
{
  iconv_close (input->cd);
  free (input);
}

/* Enter DATALEN characters from the buffer DATA into the input queue
   INPUT.  The DATA must be supplied in UTF-8 encoding (XXX UCS-4
   would be nice, too, but it requires knowledge of endianness).  The
   function returns the amount of bytes written (might be smaller than
   DATALEN) or -1 and the error number in errno.  If NONBLOCK is not
   zero, return with -1 and set errno to EWOULDBLOCK if operation
   would block for a long time.  */
ssize_t input_enqueue (input_t input, int nonblock, char *data,
		       size_t datalen)
{
  error_t err = 0;
  int was_empty;
  int enqueued = 0;
  char *buffer;
  size_t buffer_size;
  ssize_t amount;
  size_t nconv;
  char *outbuf;
  size_t outbuf_size;
  
  error_t ensure_cd_buffer_size (size_t new_size)
    {
      /* Must be a power of two.  */
#define CD_ALLOCSIZE 32

      if (input->cd_allocated < new_size)
	{
	  char *new_buffer;
	  new_size = (new_size + CD_ALLOCSIZE - 1)
	    & ~(CD_ALLOCSIZE - 1);
	  new_buffer = realloc (input->cd_buffer, new_size);
	  if (!new_buffer)
	    return ENOMEM;
	  input->cd_buffer = new_buffer;
	  input->cd_allocated = new_size;
	}
      return 0;
    }

  pthread_mutex_lock (&input->lock);
  was_empty = !input->size;

  while (datalen)
    {
      /* Make sure we are ready for writing (or at least can make a
	 honest attempt at it).  */
      while (input->full)
	{
	  if (nonblock)
	    {
	      err = EWOULDBLOCK;
	      goto out;
	    }
	  if (pthread_hurd_cond_wait_np (&input->space_available, &input->lock))
	    {
	      err = EINTR;
	      goto out;
	    }
	  was_empty = !input->size;
	}

      /* Prepare the input buffer for iconv.  */
      if (input->cd_size)
	{
	  err = ensure_cd_buffer_size (input->cd_size + datalen);
	  if (err)
	    goto out;
	  buffer = input->cd_buffer;
	  buffer_size = input->cd_size;
	  memcpy (buffer + buffer_size, data, datalen);
	  buffer_size += datalen;
	}
      else
	{
	  buffer = data;
	  buffer_size = datalen;
	}
      /* Prepare output buffer for iconv.  */
      outbuf = &input->buffer[input->size];
      outbuf_size = INPUT_QUEUE_SIZE - input->size;

      amount = buffer_size;
      nconv = iconv (input->cd, &buffer, &buffer_size, &outbuf, &outbuf_size);
      amount -= buffer_size;

      /* Calculate buffer progress.  */
      enqueued += amount;
      data = buffer;
      datalen = buffer_size;
      input->size = INPUT_QUEUE_SIZE - outbuf_size;

      if (nconv == (size_t) -1)
	{
	  if (errno == E2BIG)
	    {
	      /* There is not enough space for more data in the outbuf
		 buffer.  Mark the buffer as full, awake waiting
		 readers and go to sleep (above).  */
	      input->full = 1;
	      if (was_empty)
		pthread_cond_broadcast (&input->data_available);
	      /* Prevent calling pthread_cond_broadcast again if nonblock.  */
	      was_empty = 0;
	    }
	  else
	    break;
	}
    }

  /* XXX What should be done with invalid characters etc?  */
  if (errno == EINVAL && datalen)
    {
      /* The conversion stopped because of an incomplete byte sequence
	 at the end of the buffer.  */
      /* If we used the caller's buffer DATA, the remaining bytes
	 might not fit in our internal output buffer.  In this case we
	 can reallocate the buffer in INPUT without needing to update
	 CD_BUFFER (as it points into DATA). */
      err = ensure_cd_buffer_size (datalen);
      if (err)
        {
          pthread_mutex_unlock (&input->lock);
	  errno = err;
          return enqueued ?: -1;
        }
      memmove (input->cd_buffer, data, datalen);
    }

 out:
  if (enqueued)
    {
      if (was_empty)
	pthread_cond_broadcast (&input->data_available);
    }
  else
    errno = err;
  pthread_mutex_unlock (&input->lock);
  return enqueued ?: -1;
}

/* Remove DATALEN characters from the input queue and put them in the
   buffer DATA.  The data will be supplied in the local encoding.  The
   function returns the amount of bytes removed (might be smaller than
   DATALEN) or -1 and the error number in errno.  If NONBLOCK is not
   zero, return with -1 and set errno to EWOULDBLOCK if operation
   would block for a long time.  */
ssize_t input_dequeue (input_t input, int nonblock, char *data,
		       size_t datalen)
{
  size_t amount = datalen;

  pthread_mutex_lock (&input->lock);
  while (!input->size)
    {
      if (nonblock)
	{
	  pthread_mutex_unlock (&input->lock);
	  errno = EWOULDBLOCK;
	  return -1;
	}
      if (pthread_hurd_cond_wait_np (&input->data_available, &input->lock))
	{
	  pthread_mutex_unlock (&input->lock);
	  errno = EINTR;
	  return -1;
	}
    }

  if (amount > input->size)
    amount = input->size;
  memcpy (data, input->buffer, amount);
  memmove (input->buffer, input->buffer + amount, input->size - amount);
  input->size -= amount;
  if (amount && input->full)
    {
      input->full = 0;
      pthread_cond_broadcast (&input->space_available);
    }
  pthread_mutex_unlock (&input->lock);
  return amount;
}


/* Flush the input buffer, discarding all pending data.  */
void input_flush (input_t input)
{
  pthread_mutex_lock (&input->lock);
  input->size = 0;
  if (input->full)
    {
      input->full = 0;
      pthread_cond_broadcast (&input->space_available);
    }
  pthread_mutex_unlock (&input->lock);
}
