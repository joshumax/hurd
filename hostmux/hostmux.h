/* Multiplexing filesystems by host

   Copyright (C) 1997 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.ai.mit.edu>
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

#ifndef __HOSTMUX_H__
#define __HOSTMUX_H__

#include <hurd/netfs.h>
#include <rwlock.h>
#include <maptime.h>

/* Handy source of time.  */
volatile struct mapped_time_value *hostmux_mapped_time;

/* The state associated with a host multiplexer translator.  */
struct hostmux
{
  /* The host hodes in this mux.  */
  struct hostmux_name *names;
  struct rwlock names_lock;

  /* The next inode number we'll use; protected by NAMES_LOCK.  */
  ino_t next_fileno;

  /* A template argz, which is used to start each host-specific translator
     with the host name appropriately added.  */
  char *trans_template;
  size_t trans_template_len;

  /* What string to replace in TRANS_TEMPLATE with the name of the host; if
     0, or it doesn't occur, the host name is appended as an additional
     argument.  */
  char *host_pat;

  /* Constant fields for host stat entries.  */
  struct stat stat_template;

  /* The file that this translator is sitting on top of; we inherit various
     characteristics from it.  */
  file_t underlying;
};

/* The name of a recently looked up host entry.  */
struct hostmux_name
{
  const char *name;		/* Looked up name (may be a number).  */
  const char *canon;		/* The canonical (fq) host name.  */

  /* A filesystem node associated with NAME.  If NAME = CANON, then this will
     refer to a node with a translator for that host, otherwise, the node
     will be a symbolic link to the canonical name.  */
  struct node *node;

  ino_t fileno;			/* The inode number for this entry.  */

  struct hostmux_name *next;
};

/* The fs specific storage that libnetfs associates with each filesystem
   node.  */
struct netnode
{
  /* The mux this node belongs to (the node can either be the mux root, or
     one of the hosts served by it).  */
  struct hostmux *mux;

  /* For mux nodes, 0, and for leaf nodes, the name under which the node was
     looked up. */
  struct hostmux_name *name;
};

/* Timestamps to change.  */
#define TOUCH_ATIME 0x1
#define TOUCH_MTIME 0x2
#define TOUCH_CTIME 0x4

#ifndef HOSTMUX_EI
# define HOSTMUX_EI extern inline
#endif

/* Change the stat times of NODE as indicated by WHAT (from the set TOUCH_*)
   to the current time.  */
HOSTMUX_EI void touch (struct node *node, unsigned what)
{
  struct timeval tv;

  maptime_read (hostmux_mapped_time, &tv);

  if (what & TOUCH_ATIME)
    {
      netfs_root_node->nn_stat.st_atime = tv.tv_sec;
      netfs_root_node->nn_stat.st_atime_usec = tv.tv_usec;
    }
  if (what & TOUCH_CTIME)
    {
      netfs_root_node->nn_stat.st_ctime = tv.tv_sec;
      netfs_root_node->nn_stat.st_ctime_usec = tv.tv_usec;
    }
  if (what & TOUCH_MTIME)
    {
      netfs_root_node->nn_stat.st_mtime = tv.tv_sec;
      netfs_root_node->nn_stat.st_mtime_usec = tv.tv_usec;
    }
}

#endif /* __HOSTMUX_H__ */
