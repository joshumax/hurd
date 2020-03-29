/*
 * Copyright (C) 2020 Free Software Foundation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "libmachdev/machdev.h"
#include "block-rump.h"
#include <pthread.h>
#include <mach.h>

int
main ()
{
  int err;
  pthread_t t;

  rump_register_block ();
  machdev_device_init ();
  machdev_trivfs_init ();
  err = pthread_create (&t, NULL, machdev_server, NULL);
  if (err)
    return err;
  pthread_detach (t);
  machdev_trivfs_server ();
  return 0;
}
