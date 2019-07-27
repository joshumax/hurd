/* Definitions for shared IO control pages
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

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

#include <pthread.h>
#include <sys/types.h>		/* Defines `off_t'.  */

struct shared_io
{
  int shared_page_magic;

  /* This lock protects against modification to conch_status. */
  pthread_spinlock_t lock;

  enum
    {
      USER_HAS_CONCH,		/* User is it */
      USER_COULD_HAVE_CONCH,	/* User can become it */
      USER_RELEASE_CONCH,	/* User is it, should release it promptly */
      USER_HAS_NOT_CONCH,	/* User is not it */
    } conch_status;


  /*  While you hold the conch, the shared page will not change (except the
      conch-status word might be changed from USER_HAS_CONCH to
      USER_RELEASE_CONCH).  In addition, cooperating users will not change
      the contents of the file.  The I/O server is a cooperating user itself
      in its implementation of io_read, io_write, and so forth.  The I/O
      server is a separate user from all the shared I/O users.  If a user
      does not release the conch "promptly" then the conch may be stolen
      from that user by the I/O server.  "Promptly" will probably mean a few
      seconds.

      As a consequence of these rules, if you hold the shared page, io_read
      and so forth will block until you release the conch.  You cannot
      reliably predict what I/O operations in the server (in the io.defs
      preceding the comment `Definitions for mapped I/O') might need the
      conch, as a consequence, you should normally not call such functions
      while you are holding the conch if that could cause a deadlock. */


  /* These values are set by the IO server only: */

  int append_mode;		/* append on each write */

  int eof_notify;		/* notify filesystem upon read of eof */
  int do_sigio;			/* call io_sigio after each operation */

  int use_file_size;		/* file_size is meaningful */

  int use_read_size;		/* read_size is meaningful */
  loff_t read_size;

  blksize_t optimal_transfer_size; /* users should try to have the
				   arguments to io_prenotify, etc. be
				   multiples of this value if it is
				   nonzero. */ 

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
  int use_readnotify_size;	/* readnotify_size is meaningful */

  loff_t prenotify_size;
  loff_t postnotify_size;
  loff_t readnotify_size;


  /* These are set by both the IO server and the user: */

  /* If the read and write objects returned by io_map are the same,
     then use the xx_file_pointer for read, write, and seek.  If the
     read and write objects are not the same, then use the 
     rd_file_pointer for read and the wr_file_pointer for write.
     Normally in this case the seekable value will be false.  
     The unused file pointers will be set to -1 by the I/O server. */
  loff_t rd_file_pointer;
  loff_t wr_file_pointer;
  loff_t xx_file_pointer;

  loff_t file_size;

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
