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


struct protid
{
  struct port_info pi;

  /* User identification */
  uid_t *uids, *gids;
  int nuids, ngids;
  
  mach_port_t realnode;

  /* Object this refers to */
  struct peropen *po;
};

/* One of these is created for each open */
struct peropen
{
  off_t filepointer;
  int refcnt;
  int openstat;
  int lock_status;
};

struct trans_link trivfs_translator;

struct port_info *trivfs_control_port;


/* The user must define these variables. */
extern int trivfs_fstype;
extern int trivfs_fsid;

extern int trivfs_support_read;
extern int trivfs_support_write;
extern int trivfs_support_exec;
