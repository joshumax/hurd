/* Private data for pager library.
   Copyright (C) 1994-1997, 1999, 2000, 2011 Free Software Foundation, Inc.

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

#ifndef _LIBPAGER_PRIV_H
#define _LIBPAGER_PRIV_H

#include <mach.h>
#include <hurd.h>
#include <sys/mman.h>
#include <pthread.h>
#include "pager.h"
#include <hurd/ports.h>

/* Define this if you think the kernel is sending memory_object_init
   out of sequence with memory_object_terminate. */
/* #undef KERNEL_INIT_RACE */

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

  pthread_mutex_t interlock;
  pthread_cond_t wakeup;

  struct lock_request *lock_requests; /* pending lock requests */
  struct attribute_request *attribute_requests; /* pending attr requests */

  boolean_t may_cache;
  memory_object_copy_strategy_t copy_strategy;
  boolean_t notify_on_evict;

  /* Interface ports */
  memory_object_control_t memobjcntl;
  memory_object_name_t memobjname;

  int noterm;			/* number of threads blocking termination */

  int termwaiting:1;

#ifdef KERNEL_INIT_RACE
  /* Out of sequence object_init calls waiting for
     terminations. */
  struct pending_init *init_head, *init_tail;
#endif

  short *pagemap;
  int pagemapsize;		/* number of elements in PAGEMAP */
};

struct lock_request
{
  struct lock_request *next, **prevp;
  vm_address_t start, end;
  int pending_writes;
  int locks_pending;
  int threads_waiting;
};

struct attribute_request
{
  struct attribute_request *next, **prevp;
  boolean_t may_cache;
  memory_object_copy_strategy_t copy_strategy;
  int threads_waiting;
  int attrs_pending;
};

#ifdef KERNEL_INIT_RACE
struct pending_init
{
  mach_port_t control;
  mach_port_t name;
  struct pending_init *next;
};
#endif

enum page_errors
{
  PAGE_NOERR,
  PAGE_ENOSPC,
  PAGE_EIO,
  PAGE_EDQUOT,
};

extern int _pager_page_errors[];

/* Pagemap format */
/* These are binary state bits */
#define PM_WRITEWAIT  0x0200	/* queue wakeup once write is done */
#define PM_INIT       0x0100    /* data has been written */
#define PM_INCORE     0x0080	/* kernel might have a copy */
#define PM_PAGINGOUT  0x0040	/* being written to disk */
#define PM_PAGEINWAIT 0x0020	/* provide data back when write done */
#define PM_INVALID    0x0010	/* data on disk is irrevocably wrong */

/* These take values of enum page_errors */

/* Doesn't belong here; this is the error that should have been passed
   through m_o_data_error to the user but isn't; this lets internal use
   of the pager know what the error is.  */
#define PM_ERROR(byte) (((byte) & 0xc) >> 2)
#define SET_PM_ERROR(byte,err) (((byte) & ~0xc) | ((err) << 2))

/* Issue this error on next data_request, but only if it asks for
   write access.  */
#define PM_NEXTERROR(byte) ((byte) & 0x3)
#define SET_PM_NEXTERROR(byte,err) (((byte) & ~0x3) | (err))

struct port_class *_pager_class;


void _pager_block_termination (struct pager *);
void _pager_allow_termination (struct pager *);
error_t _pager_pagemap_resize (struct pager *, vm_address_t);
void _pager_mark_next_request_error (struct pager *, vm_address_t,
				     vm_size_t, error_t);
void _pager_mark_object_error (struct pager *, vm_address_t,
			       vm_size_t, error_t);
void _pager_lock_object (struct pager *, vm_offset_t, vm_size_t, int, int,
			 vm_prot_t, int);
void _pager_free_structure (struct pager *);
void _pager_clean (void *arg);
void _pager_real_dropweak (void *arg);
#endif
