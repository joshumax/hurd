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


struct trivfs_protid
{
  struct port_info pi;
  int isroot;
  mach_port_t realnode;		/* restricted permissions */
  struct control *cntl;
};

struct trivfs_control
{
  struct port_info pi;
  mach_port_t underlying;
};

/* The user must define these variables. */
extern int trivfs_fstype;
extern int trivfs_fsid;

extern int trivfs_support_read;
extern int trivfs_support_write;
extern int trivfs_support_exec;

extern int trivfs_protid_porttype;
extern int trivfs_cntl_porttype;

/* The user must define this function.  This should modify a struct 
   stat (as returned from the underlying node) for presentation to
   callers of io_stat.  It is permissable for this function to do
   nothing.  */
void trivfs_modify_stat (struct stat *);

/* Call this to create a new control port and return a receive right
   for it; exactly one send right must be created from the returned
   receive right.  UNDERLYING is the underlying port, such as fsys_startup
   returns as the realnode.  */
mach_port_t trivfs_handle_port (mach_port_t realnode);

/* Install these as libports cleanroutines for trivfs_protid_porttype
   and trivfs_cntl_porttype respectively. */
void trivfs_clean_protid (void *);
void trivfs_clean_cntl (void *);

/* This demultiplees messages for trivfs ports. */
int trivfs_demuxer (mach_msg_header_t *, mach_msg_header_t *);

error_t trivfs_goaway (int);
