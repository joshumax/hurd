/*
   Copyright (C) 1993, 1994 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include "priv.h"
#include "fs_S.h"

error_t 
diskfs_S_file_exec (struct protid *cred,
	     task_t task,
	     int flags,
	     char *argv,
	     u_int argvlen,
	     char *envp,
	     u_int envplen,
	     mach_port_t *fds,
	     u_int fdslen,
	     mach_port_t *portarray,
	     u_int portarraylen,
	     int *intarray,
	     u_int intarraylen,
	     mach_port_t *deallocnames,
	     u_int deallocnameslen,
	     mach_port_t *destroynames,
	     u_int destroynameslen)
{
  return EOPNOTSUPP;
}
