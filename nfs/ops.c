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
  struct nfsv2_attrstat *reply;
  error_t err;

  /* The only arg is the file handle. */

  hsize = rpc_compute_header_size (cred->nc.auth);
  rpc = alloca (hsize + sizeof (nfsv2_fhandle_t));
  
  /* Fill in request arg */
  bcopy (&np->nn.handle, rpc + hsize, NFSV2_FHSIZE);
  
  /* Transmit */
  err = nfs_rpc_send (NFSV2_GETATTR, rpc, hsize + NFSV2_FHSIZE,
		      &reply, sizeof (struct nfsv2_attrstat));
  if (err)
    return err;
  
  xdr_to_native_attrstat (reply);

  if (reply->status != NFSV2_OK)
    err = nfsv2err_to_hurderr (reply->status);
  else
    {
      np->nn_stat.st_mode = nfsv2mode_to_hurdmode (reply->attributes.type,
						   reply->attributes.mode);
      np->nn_stat.st_nlink = reply->attributes.nlink;
      np->nn_stat.st_uid = reply->attributes.uid;
      np->nn_stat.st_gid = reply->attributes.gid;
      np->nn_stat.st_size = reply->attributes.size;
      np->nn_stat.st_blksize = reply->attributes.blocksize;
      np->nn_stat.st_rdev = reply->attributes.rdev;
      np->nn_stat.st_blocks = reply->attributes.blocks;
      np->nn_stat.st_fstype = FSTYPE_NFS;
      np->nn_stat.st_fsid = reply->attributes.fsid; /* surely wrong XXX */
      np->nn_stat.st_fileid = reply->attributes.fileid;
      np->nn_stat.st_atime = reply->attributes.atime.seconds;
      np->nn_stat.st_atime_usec = reply->attributes.atime.useconds;
      np->nn_stat.st_mtime = reply->attributes.mtime.seconds;
      np->nn_stat.st_mtime_usec = reply->attributes.mtime.useconds;
      np->nn_stat.st_ctime = reply->attributes.ctime.seconds;
      np->nn_stat.st_ctime_usec = reply->attributes.ctime.useconds;
      /* Deal with other values appropriately? */

      err = 0;
    }
  
  deallocate_reply_buffer (reply);
  return err;
}

