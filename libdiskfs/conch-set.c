/* 
   Copyright (C) 1994, 1996 Free Software Foundation

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

#include "priv.h"
#include <hurd/iohelp.h>
#include <fcntl.h>

/* Write current values into the shared page.  Callers must have the
   share lock on the shared page, as well as the inode toplock.
   This is called by the conch management facilities of libioserver
   as well as by us. */
void
iohelp_put_shared_data (void *arg)
{
  struct protid *cred = arg;
  
  cred->mapped->append_mode = (cred->po->openstat & O_APPEND);
  cred->mapped->eof_notify = 0;
  cred->mapped->do_sigio = (cred->po->openstat & O_FSYNC);
  cred->mapped->use_file_size = 1;
  cred->mapped->use_read_size = 0;
  cred->mapped->optimal_transfer_size = cred->po->np->dn_stat.st_blksize;
  cred->mapped->seekable = 1;
  cred->mapped->use_prenotify_size = 1;
  cred->mapped->use_postnotify_size = 0;
  cred->mapped->use_readnotify_size = 0;
  cred->mapped->prenotify_size = cred->po->np->allocsize;
      
  cred->mapped->xx_file_pointer = cred->po->filepointer;
  cred->mapped->rd_file_pointer = -1;
  cred->mapped->wr_file_pointer = -1;
  cred->mapped->file_size = cred->po->np->dn_stat.st_size;
  cred->mapped->written = 0;
  cred->mapped->accessed = 0;
}
