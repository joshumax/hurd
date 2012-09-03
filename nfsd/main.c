/* Main NFS server program
   Copyright (C) 1996, 2002 Free Software Foundation, Inc.
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

#include "nfsd.h"
#include <stdio.h>
#include <unistd.h>
#include <rpc/pmap_prot.h>
#include <maptime.h>
#include <hurd.h>
#include <pthread.h>
#include <error.h>

int main_udp_socket, pmap_udp_socket;
struct sockaddr_in main_address, pmap_address;
static char index_file[] = LOCALSTATEDIR "/state/misc/nfsd.index";
char *index_file_name = index_file;

/* Launch a server loop thread */
static void
create_server_thread (int socket)
{
  pthread_t thread;
  int fail;

  fail = pthread_create (&thread, NULL, server_loop, (void *) socket);
  if (fail)
    error (1, fail, "Creating main server thread");

  fail = pthread_detach (thread);
  if (fail)
    error (1, fail, "Detaching main server thread");
}

int
main (int argc, char **argv)
{
  int nthreads;
  int fail;

  if (argc > 2)
    {
      fprintf (stderr, "%s [num-threads]\n", argv[0]);
      exit (1);
    }
  if (argc == 1)
    nthreads = 4;
  else
    nthreads = atoi (argv[1]);
  if (!nthreads)
    nthreads = 4;

  authserver = getauth ();
  maptime_map (0, 0, &mapped_time);

  main_address.sin_family = AF_INET;
  main_address.sin_port = htons (NFS_PORT);
  main_address.sin_addr.s_addr = INADDR_ANY;
  pmap_address.sin_family = AF_INET;
  pmap_address.sin_port = htons (PMAPPORT);
  pmap_address.sin_addr.s_addr = INADDR_ANY;

  main_udp_socket = socket (PF_INET, SOCK_DGRAM, 0);
  pmap_udp_socket = socket (PF_INET, SOCK_DGRAM, 0);
  fail = bind (main_udp_socket, (struct sockaddr *)&main_address,
	       sizeof (struct sockaddr_in));
  if (fail)
    error (1, errno, "Binding NFS socket");

  fail = bind (pmap_udp_socket, (struct sockaddr *)&pmap_address,
	       sizeof (struct sockaddr_in));
  if (fail)
    error (1, errno, "Binding PMAP socket");

  init_filesystems ();

  create_server_thread (pmap_udp_socket);

  while (nthreads--)
    create_server_thread (main_udp_socket);

  for (;;)
    {
      sleep (1);
      scan_fhs ();
      scan_creds ();
      scan_replies ();
    }
}
