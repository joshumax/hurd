/*
   Copyright (C) 1993, 1994, 1996 Free Software Foundation

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful, 
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include <assert-backtrace.h>
#include <sys/file.h>

#include "fshelp.h"

#define EWOULDBLOCK EAGAIN /* XXX */

/* Remove this when glibc has it.  */
#ifndef __LOCK_ATOMIC
#define __LOCK_ATOMIC 16
#endif

error_t
fshelp_acquire_lock (struct lock_box *box, int *user, pthread_mutex_t *mut,
		     int flags)
{
  int atomic = 0;

  if (!(flags & (LOCK_UN | LOCK_EX | LOCK_SH)))
    return 0;
  
  if ((flags & LOCK_UN)
      && (flags & (LOCK_SH | LOCK_EX)))
    return EINVAL;

  if (flags & __LOCK_ATOMIC)
    {
      atomic = 1;
      flags &= ~__LOCK_ATOMIC;
    }

  if (flags & LOCK_EX)
    flags &= ~LOCK_SH;
  
  /* flags now contains exactly one of LOCK_UN, LOCK_SH, or LOCK_EX.  */

  if (flags & LOCK_UN)
    {
      if (*user & LOCK_UN)
	return 0;

      assert_backtrace (*user == box->type ||
	      (*user == LOCK_SH && box->type == (LOCK_SH | LOCK_EX)));
      assert_backtrace (*user == LOCK_SH || *user == LOCK_EX ||
	      *user == (LOCK_SH | LOCK_EX));

      if (*user == LOCK_SH)
	{
	  if (!--box->shcount)
	    box->type = LOCK_UN;
	}
      else if (*user == LOCK_EX)
	box->type = LOCK_UN;
      
      if (box->type == LOCK_UN && box->waiting)
	{
	  box->waiting = 0;
	  pthread_cond_broadcast (&box->wait);
	}
      if (box->type == (LOCK_SH | LOCK_EX) && box->shcount == 1 && box->waiting)
	{
	  box->waiting = 0;
	  pthread_cond_broadcast (&box->wait);
	}
      *user = LOCK_UN;
    }
  else
    {
      if (atomic && *user == (flags & (LOCK_SH | LOCK_EX)))
	/* Already have it the right way. */
	return 0;

      if (atomic && *user == LOCK_EX && flags & LOCK_SH)
	{
	  /* Easy atomic change. */
	  *user = LOCK_SH;
	  box->type = LOCK_SH;
	  box->shcount = 1;
	  if (box->waiting)
	    {
	      box->waiting = 0;
	      pthread_cond_broadcast (&box->wait);
	    }
	  return 0;
	}

      /* We can not have two people upgrading their lock, this is a deadlock! */
      if (*user == LOCK_SH && atomic && box->type == (LOCK_SH | LOCK_EX))
	return EDEADLK;

      /* If we have an exclusive lock, release it. */
      if (*user == LOCK_EX && !atomic)
	{
	  *user = LOCK_UN;
	  box->type = LOCK_UN;
	  if (box->waiting)
	    {
	      box->waiting = 0;
	      pthread_cond_broadcast (&box->wait);
	    }
	}

      /* If we have a shared lock, release it. */
      if (*user == LOCK_SH && !atomic)
	{
	  *user = LOCK_UN;
	  if (!--box->shcount)
	    {
	      box->type = LOCK_UN;
	      if (box->waiting)
		{
		  box->waiting = 0;
		  pthread_cond_broadcast (&box->wait);
		}
	    }
	  if (box->type == (LOCK_SH | LOCK_EX) && box->shcount == 1 &&
	      box->waiting)
	    {
	      box->waiting = 0;
	      pthread_cond_broadcast (&box->wait);
	    }
	}

      /* If there is another exclusive lock or a pending upgrade, wait for it to
	 end. */
      while (box->type & LOCK_EX)
	{
	  if (flags & LOCK_NB)
	    return EWOULDBLOCK;
	  box->waiting = 1;
	  if (pthread_hurd_cond_wait_np (&box->wait, mut))
	    return EINTR;
	}

      assert_backtrace ((flags & LOCK_SH) || (flags & LOCK_EX));
      if (flags & LOCK_SH)
	{
	  assert_backtrace (!(box->type & LOCK_EX));
	  *user = LOCK_SH;
	  box->type = LOCK_SH;
	  box->shcount++;
	}
      else if (flags & LOCK_EX)
	{
	  /* Wait for any shared (and exclusive) locks to finish. */
	  while ((*user == LOCK_SH && box->shcount > 1) ||
		 (*user == LOCK_UN && box->type != LOCK_UN))
	    {
	      if (flags & LOCK_NB)
		return EWOULDBLOCK;
	      else
		{
		  /* Tell others that we are upgrading.  */
		  if (*user == LOCK_SH && atomic)
		    box->type = LOCK_SH | LOCK_EX;

		  box->waiting = 1;
		  if (pthread_hurd_cond_wait_np (&box->wait, mut))
		    return EINTR;
		}
	    }
	  if (*user == LOCK_SH)
	    {
	      assert_backtrace (box->shcount == 1);
	      box->shcount = 0;
	    }
	  box->type = LOCK_EX;
	  *user = LOCK_EX;
	}
    }
  return 0;
}
