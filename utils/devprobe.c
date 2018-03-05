/* Check the existence of mach devices

   Copyright (C) 1996, 1997, 2002 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <argp.h>

#include <hurd.h>
#include <mach.h>
#include <device/device.h>
#include <version.h>

const char *argp_program_version = STANDARD_HURD_VERSION (devprobe);

static const struct argp_option options[] = {
  {"silent", 's', 0, 0, "Don't print devices found"},
  {"quiet", 0, 0, OPTION_ALIAS},
  {"first", 'f', 0, 0, "Stop after the first device found"},
  {"master-device", 'M', "FILE", 0, "Get a pseudo master device port"},
  {0}
};
static const char *args_doc = "DEVNAME...";
static const char *doc = "Test for the existence of mach device DEVNAME..."
  "\vThe exit status is 0 if any devices were found.";

int
main (int argc, char **argv)
{
  /* Print devices found?  (otherwise only exit status matters)  */
  int print = 1;
  /* If printing, print all devices on the command line that are found. 
     Otherwise, just the first one found is printed.  */
  int all = 1;
  int found_one = 0;
  mach_port_t device_master = MACH_PORT_NULL;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	  error_t err;
	  device_t device;  

	case 's': case 'q':
	  /* Don't print any output.  Since our exit status only reflects
	     whether any one of the devices exists, there's no point in
	     probing past the first one.  */
	  print = all = 0; break;

	case 'f':
	  all = 0; break;

	case 'M':
	  if (device_master != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), device_master);

	  device_master = file_name_lookup (arg, O_READ | O_WRITE, 0);
	  if (device_master == MACH_PORT_NULL)
	    argp_failure (state, 3, errno, "Can't open device master port %s",
			  arg);

	  break;

	case ARGP_KEY_ARG:
	  if (device_master == MACH_PORT_NULL)
	    {
	      err = get_privileged_ports (0, &device_master);
	      if (err)
		argp_failure (state, 3, err, "Can't get device master port");
	    }

	  err = device_open (device_master, D_READ, arg, &device);
	  if (err == 0)
	    /* Got it.  */
	    {
	      device_close (device);

	      /* Free the device port we got.  */
	      mach_port_deallocate (mach_task_self (), device);

	      if (print)
		puts (arg);

	      if (! all)
		/* Only care about the first device found, so declare success
		   and...  */
		exit (0);

	      found_one = 1;
	    }
	  else if (err != ED_NO_SUCH_DEVICE)
	    /* Complain about unexpected errors.  */
	    argp_failure (state, 0, err, "%s", arg);
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  const struct argp argp = { options, parse_opt, args_doc, doc };

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  exit (found_one ? 0 : 1);
}
