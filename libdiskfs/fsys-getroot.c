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
#include "fsys_S.h"
#include <hurd/fsys.h>
#include <fcntl.h>

/* Implement fsys_getroot as described in <hurd/fsys.defs>. */
kern_return_t
diskfs_S_fsys_getroot (fsys_t controlport,
		       uid_t *uids,
		       u_int nuids,
		       uid_t *gids,
		       u_int ngids,
		       int flags,
		       retry_type *retry,
		       char *retryname,
		       file_t *returned_port,
		       mach_msg_type_name_t *returned_port_poly)
{
  struct port_info *pt = ports_check_port_type (controlport, PT_CTL);
  error_t error;
  mode_t type;
  struct protid pseudocred;
  
  if (!pt)
    return EOPNOTSUPP;

  mutex_lock (&diskfs_root_node->lock);
    
  /* This code is similar (but not the same as) the code in
     dir-pathtrans.c that does the same thing.  Perhaps a way should
     be found to share the logic.  */

  type = diskfs_root_node->dn_stat.st_mode & S_IFMT;

  if ((diskfs_node_translated (diskfs_root_node)
       || diskfs_root_node->translator.control != MACH_PORT_NULL)
      && !(flags & O_NOTRANS))
    {
      /* If this is translated, start the translator (if necessary) 
	 and use it. */
      mach_port_t childcontrol = diskfs_root_node->translator.control;

      if (childcontrol == MACH_PORT_NULL)
	{
	  if (error = diskfs_start_translator (diskfs_root_node, 
					       diskfs_dotdot_file))
	    {
	      mutex_unlock (&diskfs_root_node->lock);
	      return error;
	    }
	}
      mutex_unlock (&diskfs_root_node->lock);
      
      error = fsys_getroot (childcontrol, uids, nuids, gids, ngids,
			    flags, retry, retryname, returned_port);
      if (!error && returned_port != MACH_PORT_NULL)
	*returned_port_poly = MACH_MSG_TYPE_MOVE_SEND;
      else
	*returned_port_poly = MACH_MSG_TYPE_COPY_SEND;
      
      return error;
    }
  
  if (type == S_IFLNK && !(flags & (O_NOLINK | O_NOTRANS)))
    {
      /* Handle symlink interpretation */
      char pathbuf[diskfs_root_node->dn_stat.st_size + 1];
      int amt;
      
      error = diskfs_node_rdwr (diskfs_root_node, pathbuf, 0,
				diskfs_root_node->dn_stat.st_size, 0,
				0, &amt);
      pathbuf[amt] = '\0';

      mutex_unlock (&diskfs_root_node->lock);
      if (error)
	return error;
      
      if (pathbuf[0] == '/')
	{
	  *retry = FS_RETRY_NORMAL;
	  *returned_port = MACH_PORT_NULL;
	  *returned_port_poly = MACH_MSG_TYPE_COPY_SEND;
	  strcpy (retryname, pathbuf);
	  return 0;
	}
      else
	{
	  *retry = FS_RETRY_REAUTH;
	  *returned_port = diskfs_dotdot_file;
	  *returned_port_poly = MACH_MSG_TYPE_COPY_SEND;
	  strcpy (retryname, pathbuf);
	  return 0;
	}
    }

  if ((type == S_IFSOCK || type == S_IFBLK 
       || type == S_IFCHR || type == S_IFIFO)
      && (flags & (O_READ|O_WRITE|O_EXEC)))
    error = EOPNOTSUPP;
  
  /* diskfs_access requires a cred; so we give it one. */
  pseudocred.uids = uids;
  pseudocred.gids = gids;
  pseudocred.nuids = nuids;
  pseudocred.ngids = ngids;
      
  if (!error && (flags & O_READ))
    error = diskfs_access (diskfs_root_node, S_IREAD, &pseudocred);
  
  if (!error && (flags & O_EXEC))
    error = diskfs_access (diskfs_root_node, S_IEXEC, &pseudocred);
  
  if (!error && (flags & (O_WRITE)))
    {
      if (type == S_IFDIR)
	error = EISDIR;
      else if (diskfs_readonly)
	error = EROFS;
      else 
	error = diskfs_access (diskfs_root_node, S_IWRITE, &pseudocred);
    }

  if (error)
    {
      mutex_unlock (&diskfs_root_node->lock);
      return error;
    }
  
  flags &= ~(O_READ | O_WRITE | O_EXEC); /* XXX wrong  */

  *returned_port = (ports_get_right 
		    (diskfs_make_protid
		     (diskfs_make_peropen (diskfs_root_node, flags),
		      uids, nuids, gids, ngids)));
  *returned_port_poly = MACH_MSG_TYPE_MAKE_SEND;

  mutex_unlock (&diskfs_root_node->lock);

  ports_done_with_port (pt);

  return 0;
}
