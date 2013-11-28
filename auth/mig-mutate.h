/*
   Copyright (C) 1991,93,94,2014 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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

/* CPP definitions for MiG processing of auth.defs for auth server.  */

#define AUTH_INTRAN authhandle_t auth_port_to_handle (auth_t)
#define AUTH_INTRAN_PAYLOAD authhandle_t auth_payload_to_handle
#define AUTH_DESTRUCTOR end_using_authhandle (authhandle_t)
#define AUTH_IMPORTS import "mig-decls.h";
