/*
   Copyright (C) 2017 Free Software Foundation, Inc.
   Written by Joan Lledó.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Common interface for all kinds of devices */

#include <netif/ifcommon.h>

#include <net/if.h>
#include <errno.h>

#include <lwip/tcpip.h>

/* Open the device and set the interface up */
static error_t
if_open (struct netif *netif)
{
  error_t err = 0;
  struct ifcommon *ifc = netif_get_state (netif);

  if (ifc->open)
    err = ifc->open (netif);
  if (!err)
    {
      /* Up the inerface */
      ifc->flags |= IFF_UP | IFF_RUNNING;
      netif_set_up (netif);
    }

  return err;
}

/* Close the device and set the interface down */
static error_t
if_close (struct netif *netif)
{
  error_t err = 0;
  struct ifcommon *ifc = netif_get_state (netif);

  if (ifc->close)
    err = ifc->close (netif);
  if (!err)
    {
      /* Down the inerface */
      ifc->flags &= ~(IFF_UP | IFF_RUNNING);
      netif_set_down (netif);
    }

  return err;
}

/*
 * Common initialization callback for all kinds of devices.
 *
 * This function doesn't assume there's a device nor tries to open it.
 * If a device is present, it must be opened from the ifc->init() callback.
 */
err_t
if_init (struct netif *netif)
{
  struct ifcommon *ifc = netif_get_state (netif);

  if (ifc == NULL)
    /* The user provided no interface */
    return -1;

  return ifc->init (netif);
}

/* Tries to close the device and frees allocated resources */
error_t
if_terminate (struct netif * netif)
{
  error_t err;
  struct ifcommon *ifc = netif_get_state (netif);

  if (ifc == NULL)
    /* The user provided no interface */
    return -1;

  err = if_close (netif);
  if (err)
    return err;

  return ifc->terminate (netif);
}

/* Args for _if_change_flags() */
struct if_change_flags_args
{
  struct netif *netif;
  uint16_t flags;
  error_t err;
};

/*
 * Implementation of if_change_flags(), called inside a thread-safe context
 */
static void
_if_change_flags (void *arg)
{
  error_t err;
  struct ifcommon *ifc;
  uint16_t oldflags;
  struct if_change_flags_args *args = arg;

  ifc = netif_get_state (args->netif);

  if (ifc == NULL)
    {
      /* The user provided no interface */
      errno = EINVAL;
      return;
    }

  oldflags = ifc->flags;

  err = ifc->change_flags (args->netif, args->flags);

  if (!err && ((oldflags ^ args->flags) & IFF_UP))	/* Bit is different  ? */
    err = ((oldflags & IFF_UP) ? if_close : if_open) (args->netif);

  args->err = err;

  return;
}

/*
 * Change device flags.
 *
 * If IFF_UP changes, it opens/closes the device accordingly.
 */
error_t
if_change_flags (struct netif * netif, uint16_t flags)
{
  error_t err;

  /*
   * Call _if_change_flags() inside the tcpip_thread and wait for it to finish.
   */
  struct if_change_flags_args *args =
    calloc (1, sizeof (struct if_change_flags_args));
  args->netif = netif;
  args->flags = flags;
  err = tcpip_callback_wait(_if_change_flags, args);

  if(!err)
    /* Get the return value */
    err = args->err;

  free (args);

  return err;
}
