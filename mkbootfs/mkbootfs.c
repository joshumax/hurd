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
   will spawn an execserver with the right bits when it starts.  

   The text should be loaded at 0x10000 and the data at 
   0x10000 + _execserver_text_size and the bss cleared at 
   0x10000 + _execserver_text_size + _execserver_data_size.
   */

/* This is non-general, and only intended for the i386 Mach 3.0 with
   its own weird format.  It expects the header files to be from such a
   Mach system as CMU sets them up. */

#include <a.out.h>
#include <stdio.h>
#include <sys/file.h>

/* Usage: mkbootfs execserver newdoto */

main (int argc, char **argv)
{
  int execserver, newdoto;
  struct exec a, newa;
  unsigned long foo;
  void *buf;
  struct nlist n;

  if (argc != 3)
    {
      fprintf (stderr, "Usage: %s execserver newdoto\n", argv[0]);
      exit (1);
    }  

  execserver = open (argv[1], O_RDONLY);
  if (execserver == -1)
    {
      perror (argv[1]);
      exit (1);
    }
  
  newdoto = open (argv[2], O_WRONLY | O_CREAT, 0666);
  if (newdoto == -1)
    {
      perror (argv[2]);
      exit (1);
    }
  
  read (execserver, &a, sizeof (struct exec));
  
  /* Write the new data segment to the new file. */
  lseek (newdoto, sizeof (struct exec), L_SET);

  /* First, _execserver_text_size */
  foo = a.a_text + sizeof (struct exec);
  write (newdoto, &foo, sizeof foo);

  /* Next, _execserver_data_size */
  write (newdoto, &a.a_data, sizeof a.a_data);
  
  /* Next, _execserver_bss_size */
  write (newdoto, &a.a_bss, sizeof a.a_bss);

  /* Next, _execserver_text */
  buf = malloc (a.a_text + sizeof (struct exec));
  lseek (execserver, 0, L_SET);
  read (execserver, buf, a.a_text + sizeof (struct exec));
  write (newdoto, buf, a.a_text + sizeof (struct exec));
  free (buf);
  
  /* Next, _execserver_data */
  buf = malloc (a.a_data);
  read (execserver, buf, a.a_data);
  write (newdoto, buf, a.a_data);
  free (buf);
  
  /* Finally, _execserver_start */
  write (newdoto, &a.a_entry, sizeof a.a_entry);
  
  /* We have no relocation information */

  /* Now, write the symbol table */
  n.n_un.n_strx = 50;
  n.n_type = N_DATA | N_EXT;
  n.n_value = 0;

  /* First, _execserver_text_size */
  write (newdoto, &n, sizeof (n));
  n.n_value += sizeof (foo);
  
  /* Now, _execserver_data_size */
  n.n_un.n_strx += 50;
  write (newdoto, &n, sizeof (n));
  n.n_value += sizeof (foo);
  
  /* Now, _execserver_bss_size */
  n.n_un.n_strx += 50;
  write (newdoto, &n, sizeof (n));
  n.n_value += sizeof (foo);
  
  /* Now, _execserver_text */
  n.n_un.n_strx += 50;
  write (newdoto, &n, sizeof (n));
  n.n_value += a.a_text + sizeof (struct exec);
  
  /* Now, _execserver_data */
  n.n_un.n_strx += 50;
  write (newdoto, &n, sizeof (n));
  n.n_value += a.a_data;
  
  /* Now, _execserver_start */
  n.n_un.n_strx += 50;
  write (newdoto, &n, sizeof (n));
  n.n_value += sizeof (foo);
  
  /* Now, we have to write out the string table */
#define DOSTRING(x) \
  write (newdoto, x, strlen (x) + 1); \
  lseek (newdoto, 50 - strlen (x) - 1, L_INCR);

  foo = 350;			/* six strings and the beginning */
  write (newdoto, &foo, sizeof foo);
  lseek (newdoto, 50 - sizeof foo, L_INCR);

  DOSTRING ("_execserver_text_size");
  DOSTRING ("_execserver_data_size");
  DOSTRING ("_execserver_bss_size");
  DOSTRING ("_execserver_text");
  DOSTRING ("_execserver_data");
  DOSTRING ("_execserver_start");

  lseek (newdoto, -1, L_INCR);
  foo = 0;
  write (newdoto, &foo, 1);

  /* Now write out the header */
  a.a_data = 
    (4 * sizeof (int) + a.a_text + a.a_data + sizeof (struct exec));
  a.a_text = 0;
  a.a_bss = 0;
  a.a_syms = 6 * sizeof n;
  a.a_entry = 0;
  a.a_trsize = 0;
  a.a_drsize = 0;
  lseek (newdoto, 0, L_SET);
  write (newdoto, &a, sizeof a);
  
  exit (0);
}

  
  
