/* Translation functions for mig.

   Copyright (C) 2013 Free Software Foundation, Inc.

   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

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

#ifndef __MIG_DECLS_H__
#define __MIG_DECLS_H__

#include "proc.h"

typedef struct exc* exc_t;

static inline exc_t __attribute__ ((unused))
begin_using_exc_port (mach_port_t port)
{
  return ports_lookup_port (NULL, port, exc_class);
}

static inline void __attribute__ ((unused))
end_using_exc (exc_t exc)
{
  if (exc != NULL)
    ports_port_deref (exc);
}

#endif
