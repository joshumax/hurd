/* Definitions for shared IO control pages
   Copyright (C) 1992 Free Software Foundation

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

struct shared_io
{
  int shared_page_magic;

  /* This lock protects against modification to it_status. */
  spin_lock_t lock;

  enum
    {
      USER_IT,			/* User is it */
      USER_POTENTIALLY_IT,	/* User can become it */
      USER_RELEASE_IT,		/* User is it, should release it promptly */
      USER_NOT_IT,		/* User is not it */
    } it_status;


  /* These values are set by the IO server only: */

  int append_mode;		/* append on each write */

  int eof_notify;		/* notify filesystem upon read of eof */
  int do_sigio;			/* call io_sigio after each operation */

  int use_file_size;		/* file_size is meaningful */

  int use_read_size;		/* read_size is meaningful */
  int read_size;

  enum
    { 
      /* This means that there isn't any data to be read */
      RBR_NO_DATA,

      /* This means that more data cannot be added to the buffer.  If 
	 the rd_file_pointer is advanced, then more data might become 
	 readable.  This condition has priority over NO_DATA: protocols
	 might refuse to receive data when the buffer is full; then this
	 will be BUFFER_FULL.  If file pointer gets advanced, then the
	 protocol will tell the sender to go ahead, and the read_block_reason 
	 will be NO_DATA until the first data arrives.  
	 */
      RBR_BUFFER_FULL,

      /* These conditions are generally only meaningful for nonseekable
	 objects.  */
    }
  read_block_reason;		/* identifies what holds up reading */
  
  int seekable;			/* the file pointer can be reduced */

  int use_prenotify_size;	/* prenotify_size is meaningful */
  int use_postnotify_size;	/* postnotify_size is meaningful */
  
  off_t prenotify_size;
  off_t postnotify_size;


  /* These are set by both the IO server and the user: */

  /* These have meanings just like io_map; -1 is used to indicate an
     impossible value.  */
  off_t rd_file_pointer; 
  off_t wr_file_pointer;
  off_t xx_file_pointer;

  off_t file_size;

  /* These two indicate that the appropriate times need updated */
  int written;
  int accessed;

  
  /* File structuring: */

  /* If the file is not seekable and read data is separate from write
     data, then the read data might be structured.  Each record is
     identified by one of these structures.  The "auxil" field
     contains extra data which might be of interest to some readers,
     but is not part of the data proper (for example, UDP and raw IP
     put the internet headers there).

     The IO server guarantees that these will be consecutive, and that
     the file_pointer_start of each record will be that of the last
     plus its data_length.  The last valid structure might grow
     whenever the server is it.	 All previous records from the
     rd_file_pointer to the current read_size/file_size will not
     change.  Records before that can be dropped and the valid records
     moved forward in the array (when the server is it); if this
     happens indexes_changed will be set to the number of records 
     dropped.  */

  int indexes_changed;		/* users can clear this when they want */

  /* Users should not modify the rest of this: */
  int use_structure;		/* structure is being used */
  struct iomap_structure
    {
      int file_pointer_start;	/* file pointer offset of data */
      int object_start;		/* offset of auxil in memory object */
      int auxil_length;		/* length of auxil data */
      int data_length;		/* length of real data */
    } structure[0];
};

/* Look at this value to determine the byte order the server is using,
   and then use it.  */
#define SHARED_PAGE_MAGIC 0xaabbccdd

