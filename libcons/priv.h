/* Private declarations for cons library
   Copyright (C) 2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef _CONS_PRIV_H
#define _CONS_PRIV_H

#include "cons.h"


/* Number of records the client is allowed to lag behind the
   server.  */
extern int _cons_slack;

/* The filename of the console server.  */
extern char *_cons_file;


/* Called by MiG to translate ports into cons_notify_t.  mutations.h
   arranges for this to happen for the fs_notify interfaces. */
static inline cons_notify_t
begin_using_notify_port (fs_notify_t port)
{
  return ports_lookup_port (cons_port_bucket, port, cons_port_class);
}

/* Called by MiG after server routines have been run; this balances
   begin_using_notify_port, and is arranged for the fs_notify
   interfaces by mutations.h. */
static inline void
end_using_notify_port (cons_notify_t cred)
{
  if (cred)
    ports_port_deref (cred);
}

#endif	/* _CONS_PRIV_H */
