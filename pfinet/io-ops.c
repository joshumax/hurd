/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

error_t
S_io_write (struct sock_user *user,
	    char *data,
	    u_int datalen,
	    off_t offset,
	    mach_msg_type_number_t *amount)
{
  error_t err;
  
  if (!user)
    return EOPNOTSUPP;

  mutex_lock (&global_lock);
  /* O_NONBLOCK for fourth arg? XXX */
  err = - (*user->sock->ops->write) (user->sock, data, datalen, 0);
  mutex_unlock (&global_lock);
  
  return err;
}

error_t
S_io_read (struct sock_user *user,
	   char **data,
	   u_int *datalen,
	   off_t offset,
	   mach_msg_type_number_t amount)
{
  
	   
