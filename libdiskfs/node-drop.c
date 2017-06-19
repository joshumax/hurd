/* 
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

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

#include "priv.h"

/* Free the list of modification requests MR */
static void
free_modreqs (struct modreq *mr)
{
  struct modreq *tmp;
  for (; mr; mr = tmp)
    {
      mach_port_deallocate (mach_task_self (), mr->port);
      tmp = mr->next;
      free (mr);
    }
}


/* Node NP now has no more references; clean all state.  NP must be
   locked.  */
void
diskfs_drop_node (struct node *np)
{
  mode_t savemode;

  if (np->dn_stat.st_nlink == 0)
    {
      diskfs_check_readonly ();
      assert_backtrace (!diskfs_readonly);

      if (np->dn_stat.st_mode & S_IPTRANS)
	diskfs_set_translator (np, 0, 0, 0);

      if (np->allocsize != 0
	  || (diskfs_create_symlink_hook 
	      && S_ISLNK (np->dn_stat.st_mode)
	      && np->dn_stat.st_size))
	{
	  /* If the node needs to be truncated, then a complication
	     arises, because truncation might require gaining
	     new references to the node.  So, we give ourselves
	     a reference back, unlock the refcnt lock.  Then
	     we are in the state of a normal user, and do the truncate
	     and an nput.  The next time through, this routine
	     will notice that the size is zero, and not have to
	     do anything. */
	  refcounts_unsafe_ref (&np->refcounts, NULL);
	  diskfs_truncate (np, 0);
	  
	  /* Force allocsize to zero; if truncate consistently fails this
	     will at least prevent an infinite loop in this routine. */
	  np->allocsize = 0;
	  
	  diskfs_nput (np);
	  return;
	}

      assert_backtrace (np->dn_stat.st_size == 0);

      savemode = np->dn_stat.st_mode;
      np->dn_stat.st_mode = 0;
      np->dn_stat.st_rdev = 0;
      np->dn_set_ctime = np->dn_set_atime = 1;
      diskfs_node_update (np, diskfs_synchronous);
      diskfs_free_node (np, savemode);
    }
  else
    diskfs_node_update (np, diskfs_synchronous);

  fshelp_drop_transbox (&np->transbox);

  if (np->dirmod_reqs)
    free_modreqs (np->dirmod_reqs);
  if (np->filemod_reqs)
    free_modreqs (np->filemod_reqs);

  assert_backtrace (!np->sockaddr);

  pthread_mutex_unlock(&np->lock);
  pthread_mutex_destroy(&np->lock);
  diskfs_node_norefs (np);
}
