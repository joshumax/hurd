/* Types used in RPC definitions corresponding to ioctls.
   Copyright (C) 1994, 1996 Free Software Foundation

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

#ifndef _HURD_IOCTL_TYPES_H
#define _HURD_IOCTL_TYPES_H

#include <termios.h>
typedef tcflag_t modes_t[4];
typedef speed_t speeds_t[2];
typedef cc_t ccs_t[NCCS];

#include <sys/ioctl.h>
typedef struct winsize winsize_t;

#include <net/if.h>
typedef struct sockaddr sockaddr_t;
typedef char ifname_t[16];

#endif	/* hurd/ioctl_types.h */
