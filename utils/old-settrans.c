/* Set a passive translator on a file
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

#include <hurd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>

int
main (int argc, char **argv)
{
  /* This is pretty kludgy for now.  */
  mach_port_t file;
  
  char *buf, *bp;
  int buflen;
  int i;
  
  if (argc < 2)
    {
      fprintf (stderr, "\
Usage: %s FILE TRANSLATOR [ARG ...]\n\
   or: %s FILE -show\n", argv[0], argv[0]);
      exit (1);
    }

  /* Use the O_NOTRANS flag so that we replace any existing
     translator rather than piggybacking on top of it.  */
  file = file_name_lookup (argv[1], O_NOTRANS, 0);
  if (file == MACH_PORT_NULL)
    {
      perror (argv[1]);
      exit (1);
    }
  
  if (argc == 3 && !strcmp (argv[2], "-show"))
    {
      char buf[1024], *trans = buf;
      unsigned int len = sizeof buf;
      errno = file_get_translator (file, &trans, &len);
      if (errno)
	{
	  perror ("file_get_translator");
	  exit (1);
	}
      printf ("%s translated by:", argv[1]);
      do
	{
	  char *next;
	  printf (" %.*s", (int) len, trans);
	  next = memchr (trans, '\0', len);
	  if (! next)
	    {
	      printf ("<unterminated>");
	      break;
	    }
	  len -= ++next - trans;
	  trans = next;
	} while (len > 0);
      putchar ('\n');
      exit (0);
    }

  buflen = 0;
  for (i = 2; i < argc; i++)
    buflen += strlen (argv[i]) + 1;
  
  bp = buf = alloca (buflen);
  
  for (i = 2; i < argc; i++)
    bp = stpcpy (bp, argv[i]) + 1;
  
  errno = file_set_translator (file, FS_TRANS_SET, 0, 0,
			       buf, buflen, MACH_PORT_NULL, 
			       MACH_MSG_TYPE_COPY_SEND);
  if (errno != 0)
    {
      perror ("Setting translator");
      exit (1);
    }
  
  exit (0);
}

 
  
