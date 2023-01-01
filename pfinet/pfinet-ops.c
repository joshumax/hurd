/*
   Copyright (C) 2000,02 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

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

#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/rtnetlink.h>
#include <linux/ip.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/ip_fib.h>
#include <net/addrconf.h>

#include "pfinet_S.h"
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <mach/notify.h>
#include <sys/mman.h>

#include <sys/ioctl.h>

#define MAX_ROUTES	255

extern int dev_ifconf(char *arg);

/* Return the list of devices in the format provided by SIOCGIFCONF
   in IFR, but don't return more then AMOUNT bytes. If AMOUNT is
   negative, there is no limit.  */
error_t
S_pfinet_siocgifconf (io_t port,
		      vm_size_t amount,
		      data_t *ifr,
		      mach_msg_type_number_t *len)
{
  error_t err = 0;
  struct ifconf ifc;

  pthread_mutex_lock (&global_lock);
  if (amount == (vm_size_t) -1)
    {
      /* Get the needed buffer length.  */
      ifc.ifc_buf = NULL;
      ifc.ifc_len = 0;
      err = dev_ifconf ((char *) &ifc);
      if (err)
	{
	  pthread_mutex_unlock (&global_lock);
	  return -err;
	}
      amount = ifc.ifc_len;
    }
  else
    ifc.ifc_len = amount;

  if (amount > 0)
    {
      /* Possibly allocate a new buffer. */
      if (*len < amount)
	ifc.ifc_buf = (char *) mmap (0, amount, PROT_READ|PROT_WRITE,
				     MAP_ANON, 0, 0);
      else
	ifc.ifc_buf = *ifr;
      err = dev_ifconf ((char *) &ifc);
    }

  if (err)
    {
      *len = 0;
      if (ifc.ifc_buf != *ifr)
	munmap (ifc.ifc_buf, amount);
    }
  else
    {
      *len = ifc.ifc_len;
      *ifr = ifc.ifc_buf;
    }

  pthread_mutex_unlock (&global_lock);
  return err;
}

int
get_routing_table(int start, int count, ifrtreq_t *routes)
{
  struct fib_table *tb;

  if (!routes)
    return 0;

  if ((tb = fib_get_table(RT_TABLE_MAIN)) == NULL)
    return 0;

  return fn_hash_get_routes(tb, routes, start, count);
}


/* Return the routing table as a series of ifrtreq_t structs
   in routes, but don't return more then AMOUNT number of them.
   If AMOUNT is -1, we get the full table. */
error_t
S_pfinet_getroutes (io_t port,
		    vm_size_t amount,
		    data_t *routes,
		    mach_msg_type_number_t *len,
		    boolean_t *dealloc_data)
{
  error_t err = 0;
  ifrtreq_t rs[MAX_ROUTES];
  int n;
  ifrtreq_t *rtable = NULL;

  pthread_mutex_lock (&global_lock);

  if (dealloc_data)
    *dealloc_data = FALSE;

  if (amount == (vm_size_t) -1)
    {
      /* Get all of them, and return the number we got.  */
      n = get_routing_table (0, MAX_ROUTES, rs);
      amount = n;
    }
  else
    n = amount;

  if (amount > 0)
    {
      /* Possibly allocate a new buffer. */
      if (*len < amount * sizeof(ifrtreq_t))
        {
	  rtable = (ifrtreq_t *) mmap (0, amount * sizeof(ifrtreq_t), PROT_READ|PROT_WRITE,
				       MAP_ANON, 0, 0);
	  if (dealloc_data)
	    *dealloc_data = TRUE;
	}
      else
	rtable = (ifrtreq_t *)*routes;

      n = get_routing_table (0, n, rtable);
      if (amount > n)
        memset(&rtable[n], 0, (amount - n) * sizeof(ifrtreq_t));
    }

  if (rtable == MAP_FAILED)
    {
      /* Should use errno here, but glue headers #undef errno */
      err = ENOMEM;
      *len = 0;
    }
  else
    {
      *len = n * sizeof(ifrtreq_t);
      *routes = (char *)rtable;
    }

  pthread_mutex_unlock (&global_lock);
  return err;
}
