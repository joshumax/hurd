/* Definitions for shared IO control pages
   Copyright (C) 1991 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 1, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

struct shared_io
{
  int shared_page_version;

  /* This lock protects against modification to it_status. */
  spin_lock_t lock;

  enum
    {
      USER_IT,			/* User is it */
      USER_POTENTIALLY_IT,	/* User can become it */
      USER_RELEASE_IT,		/* User is it, should release it promptly */
      USER_NOT_IT,		/* User is not it */
    } it_status;

  int eof_notify;		/* notify filesystem upon read of eof */
  int do_sigio;			/* call io_sigio after each operation */

  int use_file_size;		/* file_size is meaningful */
  off_t file_size;

  int use_read_size;		/* read_size is meaningful */
  int read_size;
  
  int seekable;			/* the file pointer can be reduced */
  off_t file_pointer;

  /* These two indicate that the appropriate times need updated */
  int written;
  int accessed;

  int use_prenotify_size;	/* prenotify_size is meaningful */
  int use_postnotify_size;	/* postnotify_size is meaningful */
  
  off_t prenotify_size;
  off_t postnotify_size;

  /* Reserved for future extensions */
  int reserved [32];
  
  /* Owned by the user from here to the end of the page */
  int user_owned[0];
};


