/* Store I/O

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#ifndef __STORE_H__
#define __STORE_H__

struct store
{
  /* If this store was created using store_create, the file from which we got
     our store.  */
  file_t source;

  enum file_storage_class class;

  off_t *runs;
  unsigned num_runs;

  char *name;
  mach_port_t port;

  void *misc;

  struct store_meths *meths;
};

struct store_meths
{
  error_t (*read)(struct store *store,
		  off_t addr, size_t amount, char **buf, size_t *len);
  error_t (*write)(struct store *store,
		   off_t addr, char *buf, size_t len, size_t *amount);
};

error_t store_create (file_t source, struct store **store);

#endif /* __STORE_H__ */
