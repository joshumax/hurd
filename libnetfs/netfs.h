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

/* This library supports client-side network file system
   implementations.  It is analogous to the diskfs library provided for 
   disk-based filesystems.  */

struct node
{
  struct node *next, **prevp;
  
  struct netnode *nn;

  struct stat nn_stat;
  int nn_stat_valid;
  
  struct mutex lock;
  
  int references;
  int light_references;
  
  mach_port_t sockaddr;
  
  int owner;
  
  struct trans_link translator;

  struct lock_box userlock;

  struct dirmod *dirmod_reqs;
};

/* The user must define this function.  If NP->nn_stat_valid is false,
   then fill N->nn_stat with current information.  CRED identifies
   the user responsible for the operation.  */
error_t netfs_validate_stat (struct node *NP, struct protid *cred);
