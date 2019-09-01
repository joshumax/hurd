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

#include "pfinet_S.h"
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <mach/notify.h>
#include <sys/mman.h>

#include <sys/ioctl.h>
#include <net/if.h>

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
