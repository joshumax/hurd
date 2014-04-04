/*
   Copyright (C) 2014 Free Software Foundation, Inc.
   Written by Justus Winter.

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef __LIBPAGER_MIG_DECLS_H__
#define __LIBPAGER_MIG_DECLS_H__

#include "priv.h"

typedef struct pager *pager_t;

/* Called by server stub functions.  */

static inline struct pager * __attribute__ ((unused))
begin_using_pager (mach_port_t port)
{
  return ports_lookup_port (0, port, _pager_class);
}

static inline struct pager * __attribute__ ((unused))
begin_using_pager_payload (unsigned long payload)
{
  return ports_lookup_payload (0, payload, _pager_class);
}

static inline void __attribute__ ((unused))
end_using_pager (struct pager *p)
{
  if (p)
    ports_port_deref (p);
}

#endif /* __LIBPAGER_MIG_DECLS_H__ */
