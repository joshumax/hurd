/*
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

#include "pfinet.h"
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = 0;
struct port_class *trivfs_protid_portclasses[1];
int trivfs_protid_nportclasses = 1;
struct port_class *trivfs_cntl_portclasses[1];
int trivfs_cntl_nportclasses = 1;

int
pfinet_demuxer (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  extern int io_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int socket_server (mach_msg_header_t *, mach_msg_header_t *);

  return (io_server (inp, outp)
	  || socket_server (inp, outp)
	  || trivfs_demuxer (inp, outp));
}

int
main (int argc,
      char **argv)
{
  mach_port_t bootstrap;
  error_t err;

  if (argc < 3 || argc > 4)
    {
      fprintf (stderr,
	       "Usage: %s host-addr ether-device-name [gateway-addr]\n",
	       argv[0]);
      exit (1);
    }

  /* Talk to parent and link us in. */
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    {
      fprintf (stderr, "%s: Must be started as a translator\n",
	       argv[0]);
      exit (1);
    }

  pfinet_bucket = ports_create_bucket ();
  trivfs_protid_portclasses[0] = ports_create_class (trivfs_clean_protid, 0);
  trivfs_cntl_portclasses[0] = ports_create_class (trivfs_clean_cntl, 0);
  addrport_class = ports_create_class (clean_addrport, 0);
  socketport_class = ports_create_class (clean_socketport, 0);
  trivfs_fsid = getpid ();

  err = trivfs_startup (bootstrap, 0,
			trivfs_cntl_portclasses[0], pfinet_bucket,
			trivfs_protid_portclasses[0], pfinet_bucket, 0);
  if (err)
    {
      perror ("contacting parent");
      exit (1);
    }

  /* Generic initialization */

  init_devices ();
  init_time ();
  setup_ethernet_device (argv[2]);
  inet_proto_init (0);

  /* XXX Simulate what should be user-level initialization */

  /* Simulate SIOCSIFADDR call. */
  {
    char addr[4];

    ether_dev.pa_addr = inet_addr (argv[1]);

    /* Mask is 255.255.255.0. */
    addr[0] = addr[1] = addr[2] = 255;
    addr[3] = 0;
    ether_dev.pa_mask = *(u_long *)addr;

    ether_dev.family = AF_INET;
    ether_dev.pa_brdaddr = ether_dev.pa_addr | ~ether_dev.pa_mask;
  }

  /* Simulate SIOCADDRT call */
  {
    ip_rt_add (0, ether_dev.pa_addr & ether_dev.pa_mask, ether_dev.pa_mask,
	       0, &ether_dev, 0, 0);
  }

  if (argv[3])
    ip_rt_add (RTF_GATEWAY, 0, 0, inet_addr (argv[3]), &ether_dev, 0, 0);

  /* Turn on device. */
  dev_open (&ether_dev);

  /* Launch */
  ports_manage_port_operations_multithread (pfinet_bucket,
					    pfinet_demuxer,
					    0, 0, 1, 0);
  return 0;
}

void
trivfs_modify_stat (struct trivfs_protid *cred,
		    struct stat *st)
{
}

error_t
trivfs_goaway (struct trivfs_control *cntl, int flags)
{
  return EBUSY;
}
