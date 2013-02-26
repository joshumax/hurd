/* trans.h -- Control a translator node for the repeaters.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Written by Marco Gerards.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <hurd/netfs.h>

struct consnode
{
  /* The filename of the node.  */
  char *name;

  /* The id of the node.  */
  int id;
  
  /* Cached if the node is already opened.  */
  struct node *node;
  
  /* Read data from a node.  This is exactly the same as io_read
     does.  */
  error_t (*read) (struct protid *user, char **data,
		   mach_msg_type_number_t *datalen, off_t offset,
		   mach_msg_type_number_t amount);

  /* Read data to a node.  This is exactly the same as io_write
     does.  */
  error_t (*write) (struct protid *user, char *data,
		    mach_msg_type_number_t datalen, off_t offset,
		    mach_msg_type_number_t *amount);

  /* This is exactly the same as io_select{,_timeout} do.  */
  error_t (*select) (struct protid *user, mach_port_t reply,
		     mach_msg_type_name_t replytype,
		     struct timespec *tsp, int *type);

  /* Called when the node is opened.  */
  void (*open) (void);
  
  /* Called when the node is closed.  */
  void (*close) (void);
  
  /* The demuxer used for this node.  */
  int (*demuxer) (mach_msg_header_t *inp, mach_msg_header_t *outp);

  /* Called when the symlink is read */
  error_t (*readlink) (struct iouser *user, struct node *np, char *buf);

  /* Called when the symlink is written */
  error_t (*mksymlink) (struct iouser *cred, struct node *np, char *name);
  
  struct consnode *next;
};

typedef struct consnode *consnode_t;

/* Register the node CN with the translator.  */
void console_register_consnode (consnode_t cn);

/* Unregister the node CN from the translator.  */
void console_unregister_consnode (consnode_t cn);

/* Create a node with the name NAME and return it in *CN.  */
error_t console_create_consnode (const char *name, consnode_t *cn);

/* Destroy the node CN.  */
void console_destroy_consnode (consnode_t cn);

/* Setup the translator for console client nodes on PATH.  */
error_t console_setup_node (char *path);
