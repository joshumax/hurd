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

#ifndef __AUTH_MIG_DECLS_H__
#define __AUTH_MIG_DECLS_H__

#include "auth.h"

typedef struct authhandle *authhandle_t;

/* Called by server stub functions.  */

static inline struct authhandle * __attribute__ ((unused))
auth_port_to_handle (mach_port_t auth)
{
  return ports_lookup_port (auth_bucket, auth, authhandle_portclass);
}

static inline struct authhandle * __attribute__ ((unused))
auth_payload_to_handle (unsigned long payload)
{
  return ports_lookup_payload (auth_bucket, payload, authhandle_portclass);
}

static inline void __attribute__ ((unused))
end_using_authhandle (struct authhandle *auth)
{
  if (auth)
    ports_port_deref (auth);
}

#endif /* __AUTH_MIG_DECLS_H__ */
