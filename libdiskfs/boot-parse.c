/*
   Copyright (C) 1993, 1994 Free Software Foundation

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
  
  /* The arguments, as passed by the kernel, are as follows:
     -<flags> hostport deviceport rootname  */

  if (argc != 5 || argv[1][0] != '-')
    {
      fprintf (stderr, "Usage: %s: -[qsdnx] hostport deviceport rootname\n",
	       program_invocation_name);
      exit (1);
    }
  diskfs_host_priv = atoi (argv[2]);
  diskfs_master_device = atoi (argv[3]);
  devname = argv[4];

  (void) device_open (diskfs_master_device, D_WRITE, "console", &con);
  stderr = stdout = mach_open_devstream (con, "w");
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
      char *tmp;
      printf ("Bootstrap filesystem device name [%s]: ", devname);
      fflush (stdout);
      scanf ("%as\n", &tmp);
      if (*tmp)
	devname = tmp;
    }

  printf ("\nInitial bootstrap: %s", argv[0]);
  fflush (stdout);

  diskfs_bootflagarg = argv[1];

  return devname;
}

