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
  int isroot;
  mach_port_t realnode;
};

mach_port_t trivfs_underlying_node;

struct port_info *trivfs_control_port;

/* The user must define these variables. */
extern int trivfs_fstype;
extern int trivfs_fsid;

extern int trivfs_support_read;
extern int trivfs_support_write;
extern int trivfs_support_exec;

extern char *trivfs_server_name;
extern int trivfs_major_version;
extern int trivfs_minor_version;
extern int trivfs_edit_version;

/* The user must define this function.  This should modify a struct 
   stat (as returned from the underlying node) for presentation to
   callers of io_stat.  It is permissable for this function to do
   nothing.  */
void trivfs_modify_stat (struct stat *);

error_t trivfs_goaway (int);
