/*
   Copyright (C) 1993, 1994, 1995 Free Software Foundation

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

#include "priv.h"
#include <stdio.h>
#include <device/device.h>
#include <string.h>
#include <sys/reboot.h>
#include <mach/mig_support.h>

int diskfs_bootflags;
char *diskfs_bootflagarg;

/* Call this if the bootstrap port is null and you want to support
   being a bootstrap filesystem.  ARGC and ARGV should be as passed
   to main.  If the arguments are not in the proper format, an
   error message will be printed on stderr and exit called.  Otherwise,
   diskfs_priv_host, diskfs_master_device, and diskfs_bootflags will be
   set and the Mach kernel name of the bootstrap device will be
   returned.  */
char *
diskfs_parse_bootargs (int argc, char **argv)
{
  char *devname;
  device_t con;
  mach_port_t bootstrap;

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap != MACH_PORT_NULL)
    {
      /* We were started not by the kernel, but by the CMU default_pager.
	 It passes us args: -<flags> root_name server_dir_name.  We ignore
	 the last one.  An RPC on our bootstrap port fetches the privileged
	 ports.  */

      struct
	{
	  mach_msg_header_t Head;
	  mach_msg_type_t priv_hostType;
	  mach_port_t priv_host;
	  mach_msg_type_t priv_deviceType;
	  mach_port_t priv_device;
	} msg;
      mach_msg_return_t msg_result;

      static const mach_msg_type_t portCheck = {
	/* msgt_name = */		MACH_MSG_TYPE_MOVE_SEND,
					/* msgt_size = */		32,
					/* msgt_number = */		1,
					/* msgt_inline = */		TRUE,
					/* msgt_longform = */		FALSE,
					/* msgt_deallocate = */		FALSE,
					/* msgt_unused = */		0
      };

      msg.Head.msgh_bits =
	MACH_MSGH_BITS (MACH_MSG_TYPE_MOVE_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
      /* msgh_size passed as argument */
      msg.Head.msgh_remote_port = bootstrap;
      msg.Head.msgh_local_port = mig_get_reply_port ();
      msg.Head.msgh_seqno = 0;
      msg.Head.msgh_id = 999999;

      msg_result = mach_msg (&msg.Head, MACH_SEND_MSG|MACH_RCV_MSG,
			     sizeof msg.Head, sizeof msg,
			     msg.Head.msgh_local_port,
			     MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
      if (msg_result != MACH_MSG_SUCCESS)
	{
	  if ((msg_result == MACH_SEND_INVALID_REPLY) ||
	      (msg_result == MACH_SEND_INVALID_MEMORY) ||
	      (msg_result == MACH_SEND_INVALID_RIGHT) ||
	      (msg_result == MACH_SEND_INVALID_TYPE) ||
	      (msg_result == MACH_SEND_MSG_TOO_SMALL) ||
	      (msg_result == MACH_RCV_INVALID_NAME))
	    mig_dealloc_reply_port (msg.Head.msgh_local_port);
	  else
	    mig_put_reply_port (msg.Head.msgh_local_port);
	  assert_perror (msg_result);
	}
      mig_put_reply_port (msg.Head.msgh_local_port);

      assert (msg.Head.msgh_id == 999999 + 100);
      assert (msg.Head.msgh_size == sizeof msg);
      assert (msg.Head.msgh_bits & MACH_MSGH_BITS_COMPLEX);
      assert (*(int *) &msg.priv_hostType == *(int *) &portCheck);
      assert (*(int *) &msg.priv_deviceType == *(int *) &portCheck);
      diskfs_host_priv = msg.priv_host;
      diskfs_master_device = msg.priv_device;

      /* bootstrap_privileged_ports (bootstrap,
	 &diskfs_host_priv,
	 &diskfs_master_device); */

      /* Clear our bootstrap port, to appear as if run by the kernel.  */
      task_set_bootstrap_port (mach_task_self (), MACH_PORT_NULL);

      devname = argv[2];
    }
  else
    {
      /* The arguments, as passed by the kernel, are as follows:
	 -<flags> hostport deviceport rootname  */

      if (argc != 5 || argv[1][0] != '-')
	{
	  fprintf (stderr,
		   "Usage: %s: -[qsdnx] hostport deviceport rootname\n",
		   program_invocation_name);
	  exit (1);
	}
      diskfs_host_priv = atoi (argv[2]);
      diskfs_master_device = atoi (argv[3]);
      devname = argv[4];
    }

  (void) device_open (diskfs_master_device, D_READ|D_WRITE, "console", &con);
  stderr = stdout = mach_open_devstream (con, "w");
  setlinebuf (stdout);
  stdin = mach_open_devstream (con, "r");

  /* For now... */
  /*      readonly = 1; */
      
  /* The possible flags are 
     q  --  RB_ASKNAME
     s  --  RB_SINGLE
     d  --  RB_KDB
     n  --  RB_INITNAME */
  /* q tells us to ask about what device to use, n 
     about what to run as init. */
	
  diskfs_bootflags = 0;
  if (index (argv[1], 'q'))
    diskfs_bootflags |= RB_ASKNAME;
  if (index (argv[1], 's'))
    diskfs_bootflags |= RB_SINGLE;
  if (index (argv[1], 'd'))
    diskfs_bootflags |= RB_KDB;
  if (index (argv[1], 'n'))
    diskfs_bootflags |= RB_INITNAME;
  
  if (diskfs_bootflags & RB_ASKNAME)
    {
      char *line = NULL;
      size_t linesz = 0;
      ssize_t len;
      printf ("Bootstrap filesystem device name [%s]: ", devname);
      switch (len = getline (&line, &linesz, stdin))
	{
	case -1:
	  perror ("getline");
	  printf ("Using default of `%s'.\n", devname);
	case 0:			/* Hmm.  */
	case 1:			/* Empty line, just a newline.  */
	  /* Use default.  */
	  free (line);
	  break;
	default:
	  line[len - 1] = '\0';	/* Remove the newline.  */
	  devname = line;
	  break;
	}
    }

  printf ("\nInitial bootstrap: %s", argv[0]);
  fflush (stdout);

  diskfs_bootflagarg = argv[1];

  return devname;
}

