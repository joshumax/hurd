/* Decompressing store backend (common code for gunzip and bunzip2)

   Copyright (C) 1998, 1999, 2002 Free Software Foundation, Inc.
   Written by okuji@kuicr.kyoto-u.ac.jp <okuji@kuicr.kyoto-u.ac.jp>
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <pthread.h>
#include <sys/mman.h>

#include "store.h"

#define IN_BUFFERING  (256*1024)
#define OUT_BUFFERING (512*1024)

static pthread_mutex_t unzip_lock = PTHREAD_MUTEX_INITIALIZER;

#define STORE_UNZIP(name)		STORE_UNZIP_1 (UNZIP, name)
#define STORE_UNZIP_1(unzip,name)	STORE_UNZIP_2 (unzip, name)
#define STORE_UNZIP_2(unzip,name)	store_##unzip##_##name
#define STORE_STD_CLASS_1(name) STORE_STD_CLASS(name)
#define STRINGIFY(name) STRINGIFY_1(name)
#define STRINGIFY_1(name) #name


/* Uncompress the contents of FROM, which should contain a valid bzip2 file,
   into memory, returning the result buffer in BUF & BUF_LEN.  */
static error_t
unzip_store (struct store *from, void **buf, size_t *buf_len)
{
  /* Callbacks from decompression engine for I/O and error interface.  */
  extern int (*unzip_read) (char *buf, size_t maxread);
  extern void (*unzip_write) (const char *buf, size_t nwrite);
  extern void (*unzip_read_error) (void);
  extern void (*unzip_error) (const char *msg);

  /* How we return errors from our hook functions.  */
  jmp_buf zerr_jmp_buf;
  error_t zerr;

  /* vm_alloced buffer for the input store.  */
  void *in_buf = 0;
  size_t in_buf_len = 0;	/* Amount of valid data in IN_BUF.  */
  size_t in_buf_size = 0;	/* Allocated space for IN_BUF.  */
  size_t in_buf_offs = 0;	/* Offset of read point in IN_BUF.  */
  off_t in_buf_addr = 0;	/* Address in FROM of *next* IN_BUF.  */

  /* Buffer input in units that are least IN_BUFFERING bytes, but are also a
     multiple of FROM's block size.  */
  size_t in_addr_mask = ((1 << from->log2_block_size) - 1);
  size_t in_buffering = ((IN_BUFFERING + in_addr_mask) & ~in_addr_mask);

  /* Read at most MAXREAD (or 0 if eof) bytes into BUF from our current
     position in FROM.  */
  int zread (char *buf, size_t maxread)
    {
      size_t did_read = 0;

      while (maxread > 0)
	{
	  size_t left = in_buf_len - in_buf_offs;

	  if (left > 0)
	    /* Fill BUF with what we can from IN_BUF.  */
	    {
	      if (left > maxread)
		left = maxread;
	      memcpy (buf, in_buf + in_buf_offs, left);
	      in_buf_offs += left;
	      buf += left;
	      maxread -= left;
	      did_read += left;
	    }

	  /* Limit MAXREAD to the number of bytes left in the input.  */
	  if (maxread > (from->size - (in_buf_addr << from->log2_block_size)))
	    maxread = from->size - (in_buf_addr << from->log2_block_size);

	  if (maxread > 0)
	    /* Have to fill IN_BUF again.  */
	    {
	      void *new_in_buf = in_buf;
	      size_t new_in_buf_len = in_buf_len;

	      zerr = store_read (from, in_buf_addr, in_buffering,
				 &new_in_buf, &new_in_buf_len);
	      if (zerr)
		longjmp (zerr_jmp_buf, 1);

	      in_buf_addr += (new_in_buf_len >> from->log2_block_size);

	      if (new_in_buf != in_buf)
		{
		  if (in_buf_size > 0)
		    munmap (in_buf, in_buf_size);
		  in_buf = new_in_buf;
		  in_buf_size = new_in_buf_len;
		}

	      in_buf_len = new_in_buf_len;
	      in_buf_offs = 0;
	    }
	}
      return did_read;
    }

  size_t out_buf_offs = 0;	/* Position in the output buffer.  */

  /* Write compress data to our output buffer.  */
  void zwrite (const char *wbuf, size_t nwrite)
    {
      size_t old_buf_len = *buf_len;

      if (out_buf_offs + nwrite > old_buf_len)
	/* Have to grow the output buffer.  */
	{
	  void *old_buf = *buf;
	  void *new_buf = old_buf + old_buf_len; /* First try.  */
	  size_t new_buf_len = round_page (old_buf_len + old_buf_len + nwrite);

	  /* Try to grow the buffer.  */
	  zerr =
	    vm_allocate (mach_task_self (),
			 (vm_address_t *)&new_buf, new_buf_len - old_buf_len,
			 0);
	  if (zerr)
	    /* Can't do that, try to make a bigger buffer elsewhere.  */
	    {
	      new_buf = mmap (0, new_buf_len, PROT_READ|PROT_WRITE,
			      MAP_ANON, 0, 0);
	      zerr = (new_buf == (void *) -1) ? errno : 0;
	      if (zerr)
		longjmp (zerr_jmp_buf, 1);

	      if (out_buf_offs > 0)
		/* Copy the old buffer into the start of the new & free it. */
		memcpy (new_buf, old_buf, out_buf_offs);

	      munmap (old_buf, old_buf_len);

	      *buf = new_buf;
	    }

	  *buf_len = new_buf_len;
	}

      memcpy (*buf + out_buf_offs, wbuf, nwrite);
      out_buf_offs += nwrite;
    }

  void zreaderr (void)
    {
      zerr = EIO;
      longjmp (zerr_jmp_buf, 1);
    }
  void zerror (const char *msg)
    {
      zerr = EINVAL;
      longjmp (zerr_jmp_buf, 2);
    }

  /* Try to guess a reasonable output buffer size.  */
  *buf_len = round_page (from->size * 2);
  *buf = mmap (0, *buf_len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  zerr = (*buf == (void *) -1) ? errno : 0;
  if (zerr)
    return zerr;

  pthread_mutex_lock (&unzip_lock);

  unzip_read = zread;
  unzip_write = zwrite;
  unzip_read_error = zreaderr;
  unzip_error = zerror;

  if (! setjmp (zerr_jmp_buf))
    {
      /* Call the decompression engine.  */
      zerr = DO_UNZIP ();
    }

  pthread_mutex_unlock (&unzip_lock);

  if (in_buf_size > 0)
    munmap (in_buf, in_buf_size);

  if (zerr)
    {
      if (*buf_len > 0)
	munmap (*buf, *buf_len);
    }
  else if (out_buf_offs < *buf_len)
    /* Trim the output buffer to be the right length.  */
    {
      size_t end = round_page (out_buf_offs);
      if (end < *buf_len)
	munmap (*buf + end, *buf_len - end);
      *buf_len = out_buf_offs;
    }

  return zerr;
}


/* Return a new store in STORE which contains a snapshot of the uncompressed
   contents of the store FROM; FROM is consumed.  */
error_t
STORE_UNZIP(create) (struct store *from, int flags, struct store **store)
{
  void *buf;
  size_t buf_len;
  error_t err = unzip_store (from, &buf, &buf_len);

  if (! err)
    {
      err = store_buffer_create (buf, buf_len, flags, store);
      if (err)
	munmap (buf, buf_len);
      else
	store_free (from);
    }

  return err;
}

/* Open the compressed store NAME -- which consists of another store-class
   name, a ':', and a name for that store class to open -- and return the
   corresponding store in STORE.  CLASSES is used to select classes
   specified by the type name; if it is 0, STORE_STD_CLASSES is used.  */
error_t
STORE_UNZIP(open) (const char *name, int flags,
		    const struct store_class *const *classes,
		    struct store **store)
{
  struct store *from;
  error_t err =
    store_typed_open (name, flags | STORE_HARD_READONLY, classes, &from);

  if (! err)
    {
      err = STORE_UNZIP(create) (from, flags, store);
      if (err)
	store_free (from);
    }

  return err;
}

const struct store_class STORE_UNZIP(class) =
{ -1, STRINGIFY(UNZIP), open: STORE_UNZIP(open) };
STORE_STD_CLASS_1 (UNZIP);
