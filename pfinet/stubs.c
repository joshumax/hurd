/* Stub functions replacing things called from the Linux code
   Copyright (C) 2000,02 Free Software Foundation, Inc.

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

#include <stdlib.h>
#include <unistd.h>

#include <linux/types.h>
#include <linux/socket.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

int qdisc_restart(struct device *dev)
{
  return 0;
}

void qdisc_run_queues(void)
{
}

struct Qdisc_head qdisc_head;
struct Qdisc qdisc_stub;

void
dev_init_scheduler (struct device *dev)
{
  dev->qdisc = &qdisc_stub;
}
void dev_shutdown (struct device *)
     __attribute__ ((alias ("dev_init_scheduler")));
void dev_activate (struct device *)
     __attribute__ ((alias ("dev_init_scheduler")));
void dev_deactivate (struct device *)
     __attribute__ ((alias ("dev_init_scheduler")));
void tcp_ioctl () __attribute__ ((alias ("dev_init_scheduler")));

/* This isn't quite a stub, but it's not quite right either.  */
__u32 secure_tcp_sequence_number(__u32 saddr, __u32 daddr,
				 __u16 sport, __u16 dport)
{
  static u32 tcp_iss;
  static time_t last;
  struct timeval now;

  do_gettimeofday (&now);

  if (now.tv_sec - last > 300)
    {
      last = now.tv_sec;
      srandom (getpid () ^ now.tv_sec ^ now.tv_usec);
      tcp_iss = random ();
    }

  return tcp_iss + (now.tv_sec * 1000000) + now.tv_usec;
}
