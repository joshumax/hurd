/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
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

main ()
{
  char addr[4];
  
  setup_ethernet_device ();
  
  /* Call initialization routines */
  init_proto_init ();

  /* Simulate SIOCSIFADDR call. */

  /* 128.52.46.37 is turing.gnu.ai.mit.edu. */
  addr[0] = 128;
  addr[1] = 52;
  addr[2] = 46;
  addr[3] = 37;
  ether_dev.pa_addr = *(u_long *)addr;
  
  /* Mask is 255.255.255.0. */
  addr[0] = addr[1] = addr[2] = 255;
  addr[3] = 0;
  ether_dev.pa_mask = *(u_long *)addr;
  
  ether_dev.family = AF_INET;
  ether_dev.pa_brdaddr = ether_dev->pa_addr | ~ether_dev->pa_mask;
  
  /* Turn on device. */
  dev_open (&ether_dev);
}
