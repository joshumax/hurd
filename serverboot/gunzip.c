/* Modified by okuji@kuicr.kyoto-u.ac.jp for use in serverboot. */
/* Decompressing store backend

   Copyright (C) 1997 Free Software Foundation, Inc.
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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <cthreads.h>
#include <errno.h>

#include <file_io.h>

/* gzip.h makes several annoying defines & decls, which we have to work
   around. */
#define file_t gzip_file_t
#include "gzip.h"
#undef file_t
#undef head

#define IN_BUFFERING  (256*1024)
#define OUT_BUFFERING (512*1024)

static struct mutex unzip_lock = MUTEX_INITIALIZER;

/* Uncompress the contents of FROM, which should contain a valid gzip file,
   into memory, returning the result buffer in BUF & BUF_LEN.  */
int
serverboot_gunzip (struct file *from, void **buf, size_t *buf_len)
{
  /* Entry points to unzip engine.  */
  int get_method (int);
  extern long int bytes_out;
  /* Callbacks from unzip for I/O and error interface.  */
  extern int (*unzip_read) (char *buf, size_t maxread);
  extern void (*unzip_write) (const char *buf, size_t nwrite);
  extern void (*unzip_read_error) (void);
  extern void (*unzip_error) (const char *msg);

  /* How we return errors from our hook functions.  */
  jmp_buf zerr_jmp_buf;
  int zerr;

  size_t offset = 0;	/* Offset of read point in FROM.  */

  /* Read at most MAXREAD (or 0 if eof) bytes into BUF from our current
     position in FROM.  */
  int zread (char *buf, size_t maxread)
    {
      vm_size_t resid;
      size_t did_read;
      
      if (from->f_size - offset < maxread)
	did_read = from->f_size - offset;
      else
	did_read = maxread;

      zerr = read_file (from, offset, buf, did_read, &resid);
      if (zerr)
	longjmp (zerr_jmp_buf, 1);

      did_read -= resid;
      offset += did_read;
      
      return did_read;
    }

  size_t out_buf_offs = 0;	/* Position in the output buffer.  */

  /* Write uncompress data to our output buffer.  */
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
	      new_buf = old_buf;
	      zerr =
		vm_allocate (mach_task_self (),
			     (vm_address_t *)&new_buf, new_buf_len, 1);
	      if (zerr)
		longjmp (zerr_jmp_buf, 1);

	      if (out_buf_offs > 0)
		/* Copy the old buffer into the start of the new & free it. */
		bcopy (old_buf, new_buf, out_buf_offs);

	      vm_deallocate (mach_task_self (),
			     (vm_address_t)old_buf, old_buf_len);

	      *buf = new_buf;
	    }

	  *buf_len = new_buf_len;
	}

      bcopy (wbuf, *buf + out_buf_offs, nwrite);
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
  *buf_len = round_page (from->f_size * 2);
  zerr = vm_allocate (mach_task_self (), (vm_address_t *)buf, *buf_len, 1);
  if (zerr)
    return zerr;

  mutex_lock (&unzip_lock);

  unzip_read = zread;
  unzip_write = zwrite;
  unzip_read_error = zreaderr;
  unzip_error = zerror;

  if (! setjmp (zerr_jmp_buf))
    {
      if (get_method (0) != 0)
	/* Not a happy gzip file.  */
	zerr = EINVAL;
      else
	/* Matched gzip magic number.  Ready to unzip.
	   Set up the output stream and let 'er rip.  */
	{
	  /* Call the gunzip engine.  */
	  bytes_out = 0;
	  unzip (17, 23);	/* Arguments ignored.  */
	  zerr = 0;
	}
    }

  mutex_unlock (&unzip_lock);

  if (zerr)
    {
      if (*buf_len > 0)
	vm_deallocate (mach_task_self (), (vm_address_t)*buf, *buf_len);
    }
  else if (out_buf_offs < *buf_len)
    /* Trim the output buffer to be the right length.  */
    {
      size_t end = round_page (out_buf_offs);
      if (end < *buf_len)
	vm_deallocate (mach_task_self (),
		       (vm_address_t)(*buf + end), *buf_len - end);
      *buf_len = out_buf_offs;
    }

  return zerr;
}
