/* Private data for pager library.
   Copyright (C) 1994 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

struct pager
{
  struct port_info port;
  struct user_pager_info *upi;

  enum
    {
      NOTINIT,			/* before memory_object_init */
      NORMAL,			/* while running */
      SHUTDOWN,			/* ignore all further requests */
    } pager_state;
  
  struct mutex interlock;
  struct condition wakeup;

  struct lock_request *lock_requests; /* pending lock requests */
  
  /* Interface ports */
  memory_object_control_t memobjcntl;
  memory_object_name_t memobjname;
  
  int mscount;
  
  int seqno;

  int noterm;			/* number of threads blocking termination */

  struct pager *next, **pprev;

  int termwaiting:1;
  int waitingforseqno:1;
  
  char *pagemap;
  int pagemapsize;
};

struct lock_request
{
  struct lock_request *next, **prevp;
  vm_address_t start, end;
  int pending_writes;
  int locks_pending;
  int threads_waiting;
};

enum page_errors
{
  PAGE_NOERR,
  PAGE_ENOSPC,
  PAGE_EIO,
  PAGE_EDQUOT,
};

