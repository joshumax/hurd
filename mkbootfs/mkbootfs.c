/* Make a bootstrap filesystem from a filesystem and an exec server
   Copyright (C) 1993 Free Software Foundation

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

/* The job is to write a .o corresponding to the exec server.  This
   .o contains the following symbols:

   _execserver_text_size
   _execserver_data_size
   _execserver_bss_size
   _execserver_text
   _execserver_data
   _execserver_start

   The .o will then be linked along with the rest of the filesystem, which
   will spawn an execserver with the right bits when it starts.  */

/* This is non-general, and only intended for the i386 Mach 3.0 with its
   own weird format.  */

/* Usage: mkbootfs execserver */

main (int argc, char **argv)
{
  if (argc != 
     
