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


/* Implement the netfs_validate_stat callback as described in
   <hurd/netfs.h>. */
error_t
netfs_validate_stat (struct node *np, struct protid *cred)
{
  struct vfsv2_fattr *fattr;
  size_t hsize;
  char *rpc;

  /* The only arg is the file handle. */

  hsize = rpc_compute_header_size (cred->nc.auth);
  rpc = alloca (hsize + sizeof (nfsv2_fhandle_t));
  
  /* Fill in request arg */
  bcopy (&np->nn.handle, rpc + hsize, NFSV2_FHSIZE);
  
  /* Transmit */
  rpc_send (
