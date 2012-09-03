/* Remote file contents caching

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#ifndef __CCACHE_H__
#define __CCACHE_H__

#include "ftpfs.h"

struct ccache
{
  /* The filesystem node this is a cache of.  */
  struct node *node;

  /* In memory file image, alloced using vm_allocate.  */
  char *image;

  /* Size of data.  */
  off_t size;

  /* Upper bounds of fetched image.  */
  off_t max;

  /* Amount of IMAGE that has been allocated.  */
  size_t alloced;

  pthread_mutex_t lock;

  /* People can wait for a reading thread on this condition.  */
  pthread_cond_t wakeup;

  /* True if some thread is now fetching data.  Only that thread should
     modify the DATA_CONN, DATA_CONN_POS, and MAX fields.  */
  int fetching_active;

  /* Ftp connection over which data is being fetched, or 0.  */
  struct ftp_conn *conn;
  /* File descriptor over which data is being fetched.  */
  int data_conn;
  /* Where DATA_CONN points in the file.  */
  off_t data_conn_pos;
};

/* Read LEN bytes at OFFS in the file referred to by CC into DATA, or return
   an error.  */
error_t ccache_read (struct ccache *cc, off_t offs, size_t len, void *data);

/* Discard any cached contents in CC.  */
error_t ccache_invalidate (struct ccache *cc);

/* Return a ccache object for NODE in CC.  */
error_t ccache_create (struct node *node, struct ccache **cc);

/* Free all resources used by CC.  */
void ccache_free (struct ccache *cc);

#endif /* __CCACHE_H__ */
