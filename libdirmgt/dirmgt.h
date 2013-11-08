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

/* This library manages directories for users who want to
   write filesystem servers. */

/* Search directory DIR for name NAME.  If NODEP is nonzero, then
   set *NODEP to the node found.  If TYPE is nonzero, then
   set *TYPE to the type of the node found.  */
error_t
dirmgt_lookup (struct directory *dir, char *name, struct node **nodep,
	       int *type);
  
/* Add NODE to DIR under name NAME.  If NAME is already present in
   the directory, the existing entry under that name is replaced without
   further notice.  TYPE is the DT_* name for the type of node.  */
error_t
dirmgt_enter (struct directory *dir, char *name, struct node *node,
	      int type);

/* Add SUBDIR to DIR under name NAME.  If NAME is already present in
   the directory, then EBUSY is returned.  */
error_t
dirmgt_enter_dir (struct directory *dir, char *name, struct directory *subdir);

/* Return directory contents to a user; args are exactly as for
   the fs.defs:dir_readdir RPC. */
error_t
dirmgt_readdir (struct directory *dir, char **data, u_int *datacnt,
		int entry, int nentries, vm_size_t bufsiz, int *amt);

/* If this routine is defined, then it will be called when a lookup on
   a directory fails.  If this routine returns success, the lookup will
   then be repeated.  If it returns an error, then the lookup will fail
   with the reported error. */
error_t
(*dirmgt_find_entry)(struct directory *dir, char *name);
