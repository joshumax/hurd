/* 
   Copyright (C) 1994 Free Software Foundation

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

/* Implement io_set_all_openmodes as described in <hurd/io.defs>. */
error_t
S_io_set_all_openmodes (struct protid *cred,
			int newbits)
{
  if (!cred)
    return EOPNOTSUPP;
  
  mutex_lock (&cred->po->ip->lock);
  err = ioserver_get_conch (&np->conch);
  if (!err)
    cred->po->openstat = (modes & HONORED_STATE_MODES);
  mutex_unlock (&cred->po->ip->lock);
  return err;
}
