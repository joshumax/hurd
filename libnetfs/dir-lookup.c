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

#include <fcntl.h>
#include "netfs.h"
#include "fs_S.h"


error_t
netfs_S_dir_lookup (struct protid *diruser,
		    char *filename,
		    int flags,
		    mode_t mode,
		    retry_type *do_retry,
		    char *retry_name,
		    mach_port_t *retry_port,
		    mach_msg_type_name_t *retry_port_type)
{
  int create;			/* true if O_CREAT flag set */
  int excl;			/* true if O_EXCL flag set */
  int mustbedir;		/* true if the result must be S_IFDIR */
  int lastcomp;			/* true if we are at the last component */
  int newnode;			/* true if this node is newly created */
  struct node *dnp, *np;
  char *nextname;

  if (!diruser)
    return EOPNOTSUPP;
  
  create = (flags & O_CREAT);
  excl = (flags & O_EXCL);
  
  /* Skip leading slashes */
  while (*filename == '/')
    filename++;
  
  *retry_poly = MACH_MSG_TYPE_MAKE_SEND;
  *retry = FS_RETRY_NORMAL;
  *retry_name = '\0';
  
  if (*filename == '\0')
    {
      mustbedir = 1;
      
      /* Set things up in the state expected by the code from gotit: on. */
      dnp = 0;
      np = diruser->po->np;
      mutex_lock (&np->lock);
      netfs_nref (np);
      goto gotit;
    }
  
  dnp = diruser->po->np;
  mutex_lock (&dnp->lock);
  np = 0;
  
  netfs_nref (dnp);		/* acquire a reference for later netfs_nput */
  
  do
    {
      assert (!lastcomp);
      
      /* Find the name of the next pathname component */
      nextname = index (filename, '/');
      
      if (nextname)
	{
	  *nextname++ = '\0';
	  while (*nextname == '/')
	    nextname++;
	  if (*nextname == '\0')
	    {
	      /* These are the rules for filenames ending in /. */
	      nextname = 0;
	      lastcomp = 1;
	      mustbedir = 1;
	      create = 0;
	    }
	  else
	    lastcomp = 1;
	  
	  np = 0;
	  
	  /* Attempt a lookup on the next pathname component. */
	  error = netfs_attempt_lookup (user, dnp, nextname, &np);

	  /* Implement O_EXCL flag here */
	  if (lastcomp && create && excl && (!error || error == EAGAIN))
	    error = EEXIST;
	  
	  /* If we get an error, we're done */
	  if (error == EAGAIN)
	    {
	      /* This really means .. from root */
	      if (diruser->po->dotdotport != MACH_PORT_NULL)
		{
		  *retry = FS_RETRY_REAUTH;
		  *retry_port = dircred->po->dotdotport;
		  *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
		  if (!lastcomp)
		    strcpy (retry_name, nextname);
		  error = 0;
		  goto out;
		}
	      else
		{
		  /* We are the global root; .. from our root is
		     just our root again. */
		  error = 0;
		  np = dnp;
		  netfs_nref (np);
		}
	    }
	  
	  /* Create the new node if necessary */
	  if (lastcomp && create)
	    {
	      if (error == ENOENT)
		{
		  mode &= ~(S_IFMT | S_ISPARE | S_ISVTX);
		  mode |= S_IFREG;
		  error = netfs_attempt_create_file (diruser, dnp, 
						     filename, mode, &np);
		  newnode = 1;
		}
	      
