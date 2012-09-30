/* Map the disk image and handle faults accessing it.
   Copyright (C) 1996, 1997 Free Software Foundation, Inc.
   Written by Roland McGrath.

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

#ifndef _HURD_DISKFS_PAGER_H
#define _HURD_DISKFS_PAGER_H 1

#include <hurd/pager.h>
#include <hurd/ports.h>
#include <setjmp.h>
#include <cthreads.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

/* Start a pager for the whole disk, and store it in DISKFS_DISK_PAGER,
   preparing a signal preemptor so that the `diskfs_catch_exception' macro
   below works.  SIZE should be the size of the image to map, and the address
   mapped is returned in IMAGE.  INFO, PAGER_BUCKET, & MAY_CACHE are passed
   to `pager_create'.  */
extern void diskfs_start_disk_pager (struct pager_ops *ops, size_t upi_size,
				     struct port_bucket *pager_bucket,
				     int may_cache,
				     size_t size, void **image);

extern struct pager *diskfs_disk_pager;

struct disk_image_user
  {
    jmp_buf env;
    struct disk_image_user *next;
  };

/* Return zero now.  Return a second time with a nonzero error_t
   if this thread faults accessing `disk_image' before calling
   `diskfs_end_catch_exception' (below).  */
#define diskfs_catch_exception()					      \
({									      \
    struct disk_image_user *diu = alloca (sizeof *diu);			      \
    error_t err;							      \
    diu->next = (void *) cthread_data (cthread_self ());		      \
    err = setjmp (diu->env);						      \
    if (err == 0)							      \
      cthread_set_data (cthread_self (), diu);				      \
    err;								      \
})

/* No longer handle faults on `disk_image' in this thread.
   Any unexpected fault hereafter will crash the program.  */
#define diskfs_end_catch_exception()					      \
({									      \
    struct disk_image_user *diu = (void *) cthread_data (cthread_self ());    \
    cthread_set_data (cthread_self (), diu->next);			      \
})


#endif	/* hurd/diskfs-pager.h */
