/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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

#include "term.h"
#include <hurd.h>
#include <fcntl.h>
#include <hurd/trivfs.h>
#include <stdio.h>
#include <hurd/fsys.h>
#include <string.h>

int trivfs_fstype = FSTYPE_TERM;
int trivfs_fsid = 0;		/* pid?? */
int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ|O_WRITE;

struct port_class *trivfs_protid_portclasses[1];
struct port_class *trivfs_cntl_portclasses[1];
int trivfs_protid_nportclasses = 1;
int trivfs_cntl_nportclasses = 1;

int
demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  extern int term_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int tioctl_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int device_reply_server (mach_msg_header_t *, mach_msg_header_t *);

  return (trivfs_demuxer (inp, outp)
	  || term_server (inp, outp)
	  || tioctl_server (inp, outp)
	  || device_reply_server (inp, outp));
}

int
main (int argc, char **argv)
{
  file_t file;
  mach_port_t ctlport, bootstrap;

  term_bucket = ports_create_bucket ();
  
  tty_cntl_class = ports_create_class (trivfs_clean_cntl, 0);
  tty_class = ports_create_class (trivfs_clean_protid, 0);
  cttyid_class = ports_create_class (0, 0);
  
  trivfs_protid_portclasses[0] = tty_class;
  trivfs_cntl_portclasses[0] = tty_cntl_class;

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  
  if (bootstrap == MACH_PORT_NULL)
    {
      if (argc != 3)
	{
	  fprintf (stderr, "Usage: term mach-dev-name ttyname\n");
	  exit (1);
	}
      pterm_name = argv[1];
      nodename = argv[2];
      
      /* Install control port in filesystem */
      file = file_name_lookup (nodename, O_CREAT|O_NOTRANS, 0666);
      if (file == MACH_PORT_NULL)
	{
	  perror (nodename);
	  exit (1);
	}
  
      ctlport = trivfs_handle_port (file, tty_cntl_class, term_bucket,
				    tty_class, term_bucket);
      termctl = ports_lookup_port (term_bucket, ctlport, tty_cntl_class);
      assert (termctl);

      errno = file_set_translator (file, 0, FS_TRANS_EXCL | FS_TRANS_SET,
				   0, 0, 0, ctlport, MACH_MSG_TYPE_MAKE_SEND);
      if (errno)
	{
	  perror ("setting translator");
	  exit (1);
	}
    }
  else
    {
      if (argc < 2 || argc > 3)
	{
	  fprintf (stderr, "Usage (as translator): term mach-dev-name [ttyname]\n");
	  exit (1);
	}
      pterm_name = argv[1];
      nodename = (argv[2] ?: "");

      ctlport = trivfs_handle_port (MACH_PORT_NULL, tty_cntl_class, 
				    term_bucket, tty_class, term_bucket);
      errno = fsys_startup (bootstrap, ctlport, MACH_MSG_TYPE_MAKE_SEND,
			    &file);
      if (errno)
	{
	  perror ("Starting translator");
	  exit (1);
	}
      termctl = ports_lookup_port (term_bucket, ctlport, 0);
      assert (termctl);
      termctl->underlying = file;
    }      

  bzero (&termstate, sizeof (termstate));
  termflags = NO_CARRIER;
  output_psize = 0;
  mutex_init (&global_lock);

  inputq = create_queue (256, 100, 300);
  rawq = create_queue (256, 100, 300);
  outputq = create_queue (256, 100, 300);

  condition_init (&carrier_alert);
  condition_init (&select_alert);
  condition_implies (inputq->wait, &select_alert);
  condition_implies (outputq->wait, &select_alert);

  /* Launch */
  ports_manage_port_operations_multithread (term_bucket, demuxer, 0, 0,
					    0, MACH_PORT_NULL);

  return 0;
}  

