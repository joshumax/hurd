/* Attempt to open a disk device
   Copyright (C) 1994 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */



main (argc, argv)
{
  mach_port_t hostpriv, devicemaster;
  mach_port_t device;
  int sizes[DEV_GET_SIZE_COUNT];
  int sizescnt = DEV_GET_SIZE_COUNT;

  errno = get_privileged_ports (&hostpriv, &devicemaster);
  
  if (errno)
    {
      perror ("Cannot get privileged ports");
      exit (1);
    }
  
  errno = device_open (devicemaster, D_READ, argv[1], &device);
  
  if (errno)
    {
      perror (argv[1]);
      exit (1);
    }
  
  errno = device_get_status (device, DEV_GET_SIZE, sizes, &sizescnt);
  
  if (errno)
    {
      perror ("device_get_status");
      exit (1);
    }
  
  printf ("Record size: %d\nDevice size: %d\n", 
	  sizes[DEV_GET_SIZE_RECORD_SIZE], sizes[DEV_GET_SIZE_DEVICE_SIZE]);
  exit (0);
}


