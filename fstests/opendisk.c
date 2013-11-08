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


#include <device/device.h>
#include <errno.h>
#include <hurd.h>
#include <stdio.h>

/* Boneheaded CMU people decided to gratuitously screw us. */
#include "/gd/gnu/mach/sys/ioctl.h"

#define DKTYPENAMES
#include <device/disk_status.h>

int
main (int argc, char **argv)
{
  mach_port_t hostpriv, devicemaster;
  mach_port_t device;
  int sizes[DEV_GET_SIZE_COUNT];
  int sizescnt = DEV_GET_SIZE_COUNT;
  struct disklabel label;
  int labelcnt = sizeof label / sizeof (int);
  int i;

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


  errno = device_get_status (device, DIOCGDINFO, &label, &labelcnt);

  if (errno)
    {
      perror ("reading disk label");
      exit (1);
    }
  
  printf ("Magic: %#x", label.d_magic);
  if (label.d_magic != DISKMAGIC)
    printf ("Should be %#x\n", DISKMAGIC);
  else
    printf ("\n");
  
  printf ("Type %s\tSubtype %d\nTypename %s\n",
	  dktypenames[label.d_type], label.d_subtype, label.d_typename);
  
  printf ("Pack name %s\n", label.d_packname);
  
  printf ("Secsize %d\tnsect %d\tntrack %d\tncyl %d\tspc %d\tspu %d\n",
	  label.d_secsize, label.d_nsectors, label.d_ntracks, 
	  label.d_ncylinders, label.d_secpercyl, label.d_secperunit);
  
  printf ("Spares per track %d\tSpares per cyl %d\tAlternates %d\n",
	  label.d_sparespertrack, label.d_sparespercyl, 
	  label.d_acylinders);
  
  printf ("RPM %d\tileave %d\ttskew %d\tcskew %d\theadsw %d\ttrkseek %d\n",
	  label.d_rpm, label.d_interleave, label.d_trackskew,
	  label.d_cylskew, label.d_headswitch, label.d_trkseek);
  
  printf ("flags: %d\n", label.d_flags);
  
  printf ("npartitions: %d\n", label.d_npartitions);

  printf ("bbsize %d\tsbsize %d\n", label.d_bbsize, label.d_sbsize);
  
  printf ("part\tsize\toff\tfsize\tfstype\tfrag\tcpg\n");
  for (i = 0; i < label.d_npartitions; i++)
    {
      printf ("%c:\t%d\t%d\t%d\t%s\t%d\t%d\n",
	      'a' + i,
	      label.d_partitions[i].p_size,
	      label.d_partitions[i].p_offset,
	      label.d_partitions[i].p_fsize,
	      fstypenames[label.d_partitions[i].p_fstype],
	      label.d_partitions[i].p_frag,
	      label.d_partitions[i].p_cpg);
    }
  exit (0);
}
