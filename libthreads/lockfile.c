/* lockfile - Handle locking and unlocking of stream.  Hurd cthreads version.
   Copyright (C) 2000 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include <cthreads.h>		/* Must come before <stdio.h>! */

#define _IO_MTSAFE_IO
#include <stdio.h>

#include <assert.h>


#ifdef _STDIO_USES_IOSTREAM

void
_cthreads_flockfile (_IO_FILE *fp)
{
  cthread_t self = cthread_self ();

  if (fp->_lock->owner == self)
    {
      assert (fp->_lock->count != 0);
      ++fp->_lock->count;
    }
  else
    {
      mutex_lock (&fp->_lock->mutex);
      assert (fp->_lock->owner == 0);
      fp->_lock->owner = self;
      assert (fp->_lock->count == 0);
      fp->_lock->count = 1;
    }
}

void
_cthreads_funlockfile (_IO_FILE *fp)
{
  if (--fp->_lock->count == 0)
    {
      assert (fp->_lock->owner == cthread_self ());
      fp->_lock->owner = 0;
      mutex_unlock (&fp->_lock->mutex);
    }
}

int
_cthreads_ftrylockfile (_IO_FILE *fp)
{
  cthread_t self = cthread_self ();

  if (fp->_lock->owner == self)
    {
      assert (fp->_lock->count != 0);
      ++fp->_lock->count;
    }
  else if (mutex_try_lock (&fp->_lock->mutex))
    {
      assert (fp->_lock->owner == 0);
      fp->_lock->owner = self;
      assert (fp->_lock->count == 0);
      fp->_lock->count = 1;
    }
  else
    return 0;			/* No lock for us.  */

  /* We got the lock. */
  return 1;
}


# undef 	_IO_flockfile
# undef 	_IO_funlockfile
# undef 	_IO_ftrylockfile
# pragma weak	_IO_flockfile	= _cthreads_flockfile
# pragma weak	_IO_funlockfile	= _cthreads_funlockfile
# pragma weak	_IO_ftrylockfile= _cthreads_ftrylockfile

# undef		flockfile
# undef		funlockfile
# undef		ftrylockfile
# pragma weak	flockfile	= _cthreads_flockfile
# pragma weak	funlockfile	= _cthreads_funlockfile
# pragma weak	ftrylockfile	= _cthreads_ftrylockfile


#endif /* _STDIO_USES_IOSTREAM */
