/* Multiplexing filesystems by user

   Copyright (C) 1997, 2000 Free Software Foundation, Inc.
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

#ifndef __USERMUX_H__
#define __USERMUX_H__

#include <hurd/netfs.h>
#include <pthread.h>
#include <maptime.h>

struct passwd;

/* Filenos (aka inode numbers) for user nodes are the uid + this.  */
#define USERMUX_FILENO_UID_OFFSET	10

/* Handy source of time.  */
volatile struct mapped_time_value *usermux_maptime;

/* The state associated with a user multiplexer translator.  */
struct usermux
{
  /* The user nodes in this mux.  */
  struct usermux_name *names;
  pthread_rwlock_t names_lock;

  /* A template argz, which is used to start each user-specific translator
     with the user name appropriately added.  */
  char *trans_template;
  size_t trans_template_len;

  /* What string to replace in TRANS_TEMPLATE with the name of the various
     user params; if none occur in the template, the user's home dir is
     appended as an additional argument.  */
  char *user_pat;		/* User name */
  char *home_pat;		/* Home directory */
  char *uid_pat;		/* Numeric user id */

  /* Constant fields for user stat entries.  */
  struct stat stat_template;

  /* The file that this translator is sitting on top of; we inherit various
     characteristics from it.  */
  file_t underlying;
};

/* The name of a recently looked up user entry.  */
struct usermux_name
{
  const char *name;		/* Looked up name.  */

  /* A filesystem node associated with NAME.  */
  struct node *node;

  struct usermux_name *next;
};

/* The fs specific storage that libnetfs associates with each filesystem
   node.  */
struct netnode
{
  /* The mux this node belongs to (the node can either be the mux root, or
     one of the users served by it).  */
  struct usermux *mux;

  /* For mux nodes, 0, and for leaf nodes, the name under which the node was
     looked up. */
  struct usermux_name *name;

  /* The translator associated with node, or if its a symlink, just the link
     target.  */
  char *trans;
  size_t trans_len;
};

error_t create_user_node (struct usermux *mux, struct usermux_name *name,
			  struct passwd *pw, struct node **node);

#ifndef USERMUX_EI
# define USERMUX_EI extern inline
#endif

#endif /* __USERMUX_H__ */
