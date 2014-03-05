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

#ifndef __MACH_DEFPAGER_MIG_DECLS_H__
#define __MACH_DEFPAGER_MIG_DECLS_H__

#include "priv.h"

/* Called by server stub functions.  */

static inline struct dstruct * __attribute__ ((unused))
begin_using_default_pager (mach_port_t port)
{
  if (! MACH_PORT_VALID(port)
      || ((default_pager_t) dnameof(port))->pager != (port))
    return DEFAULT_PAGER_NULL;

  return (default_pager_t) dnameof(port);
}

#endif /* __MACH_DEFPAGER_MIG_DECLS_H__ */
