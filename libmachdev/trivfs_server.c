/*
   Copyright (C) 2009 Free Software Foundation, Inc.
   Written by Zheng Da.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* This manages the master ports obtained when opening the libmachdev-based
   translator node. */

#include <stdio.h>
#include <fcntl.h>
#include <error.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd.h>
#include <device/device.h> /* mach console */

#include "libdiskfs/diskfs.h"
#include "device_S.h"
#include "notify_S.h"
#include "fsys_S.h"
#include "mach_i386_S.h"

static struct port_bucket *port_bucket;

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = O_READ | O_WRITE;

/* Our port classes.  */
struct port_class *trivfs_protid_class;
struct port_class *trivfs_cntl_class;

/* Implementation of notify interface */
kern_return_t
do_mach_notify_port_deleted (struct port_info *pi,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_msg_accepted (struct port_info *pi,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_port_destroyed (struct port_info *pi,
			       mach_port_t port)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_no_senders (struct port_info *pi,
			   mach_port_mscount_t mscount)
{
  return ports_do_mach_notify_no_senders (pi, mscount);
}

kern_return_t
do_mach_notify_send_once (struct port_info *pi)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_dead_name (struct port_info *pi,
			  mach_port_t name)
{
  return EOPNOTSUPP;
}

boolean_t
machdev_is_master_device (mach_port_t port)
{
  struct port_info *pi0 = ports_lookup_port (port_bucket, port,
					    trivfs_protid_class);
  struct port_info *pi1 = ports_lookup_port (port_bucket, port,
					    trivfs_cntl_class);
  boolean_t ret;

  ret = pi0 != NULL || pi1 != NULL;

  if (pi0 != NULL)
    ports_port_deref (pi0);
  if (pi1 != NULL)
    ports_port_deref (pi1);

  return ret;
}

error_t
trivfs_append_args (struct trivfs_control *fsys, char **argz, size_t *argz_len)
{
  error_t err = 0;

#define ADD_OPT(fmt, args...)						\
  do { char buf[100];							\
       if (! err) {							\
         snprintf (buf, sizeof buf, fmt , ##args);			\
         err = argz_add (argz, argz_len, buf); } } while (0)

#undef ADD_OPT
  return err;
}


kern_return_t
S_i386_set_ldt (mach_port_t target_thread,
                       int first_selector,
                       descriptor_list_t desc_list,
                       mach_msg_type_number_t desc_listCnt,
                       boolean_t desc_listSCopy)
{
  return EOPNOTSUPP;
}

kern_return_t
S_i386_get_ldt (mach_port_t target_thread,
                       int first_selector,
                       int selector_count,
                       descriptor_list_t *desc_list,
                       mach_msg_type_number_t *desc_listCnt)
{
  return EOPNOTSUPP;
}

kern_return_t
S_i386_io_perm_modify (mach_port_t target_task,
                              mach_port_t io_perm,
                              boolean_t enable)
{
  return EOPNOTSUPP;
}

kern_return_t
S_i386_set_gdt (mach_port_t target_thread,
                       int *selector,
                       descriptor_t desc)
{
  return EOPNOTSUPP;
}

kern_return_t
S_i386_get_gdt (mach_port_t target_thread,
                       int selector,
                       descriptor_t *desc)
{
  return EOPNOTSUPP;
}

kern_return_t
S_i386_io_perm_create (mach_port_t master_port,
                              io_port_t from,
                              io_port_t to,
                              io_perm_t *io_perm)
{
  /* Make sure we are the master device */
  if (! machdev_is_master_device(master_port))
    return EINVAL;

  return i386_io_perm_create (_hurd_device_master, from, to, io_perm);
}

/* This is fraud */
kern_return_t
trivfs_S_fsys_startup (mach_port_t bootport,
                       mach_port_t reply,
                       mach_msg_type_name_t replytype,
                       int flags,
                       mach_port_t cntl,
                       mach_port_t *realnode,
                       mach_msg_type_name_t *realnodetype)
{
  *realnode = MACH_PORT_NULL;
  *realnodetype = MACH_MSG_TYPE_MOVE_SEND;
  return 0;
}

/* Override the privileged ports for booting the system */
kern_return_t
trivfs_S_fsys_getpriv (struct diskfs_control *init_bootstrap_port,
                       mach_port_t reply, mach_msg_type_name_t reply_type,
                       mach_port_t *host_priv, mach_msg_type_name_t *hp_type,
                       mach_port_t *dev_master, mach_msg_type_name_t *dm_type,
                       mach_port_t *fstask, mach_msg_type_name_t *task_type)
{
  error_t err;
  mach_port_t right;
  struct port_info *server_info;

  err = ports_create_port (trivfs_protid_class, port_bucket,
                           sizeof (struct port_info), &server_info);
  assert_perror_backtrace (err);
  right = ports_get_send_right (server_info);
  ports_port_deref (server_info);

  err = get_privileged_ports (host_priv, NULL);
  if (!err)
    {
      *dev_master = right;
      *fstask = mach_task_self ();
      *hp_type = *dm_type = MACH_MSG_TYPE_COPY_SEND;
      *task_type = MACH_MSG_TYPE_COPY_SEND;
    }
  return err;
}

static void
resume_bootstrap_server(mach_port_t server_task, const char *server_name)
{
  error_t err;
  mach_port_t right;
  mach_port_t dev, cons;
  struct port_info *server_info;

  assert_backtrace (server_task != MACH_PORT_NULL);

  err = ports_create_port (trivfs_cntl_class, port_bucket,
                           sizeof (struct port_info), &server_info);
  assert_perror_backtrace (err);
  right = ports_get_send_right (server_info);
  ports_port_deref (server_info);
  err = task_set_special_port (server_task, TASK_BOOTSTRAP_PORT, right);
  assert_perror_backtrace (err);
  err = mach_port_deallocate (mach_task_self (), right);
  assert_perror_backtrace (err);

  err = task_resume (server_task);
  assert_perror_backtrace (err);

  /* Make sure we have a console */
  err = get_privileged_ports (NULL, &dev);
  assert_perror_backtrace (err);
  err = device_open (dev, D_READ|D_WRITE, "console", &cons);
  mach_port_deallocate (mach_task_self (), dev);
  assert_perror_backtrace (err);
  stdin = mach_open_devstream (cons, "r");
  stdout = stderr = mach_open_devstream (cons, "w");
  mach_port_deallocate (mach_task_self (), cons);

  printf (" %s", server_name);
  fflush (stdout);
}

int
machdev_trivfs_init(mach_port_t bootstrap_resume_task, const char *name, mach_port_t *bootstrap)
{
  port_bucket = ports_create_bucket ();
  trivfs_cntl_class = ports_create_class (trivfs_clean_cntl, 0);
  trivfs_protid_class = ports_create_class (trivfs_clean_protid, 0);

  if (bootstrap_resume_task != MACH_PORT_NULL)
    {
      resume_bootstrap_server(bootstrap_resume_task, name);
      *bootstrap = MACH_PORT_NULL;
    }
  else
    {
      task_get_bootstrap_port (mach_task_self (), bootstrap);
      if (*bootstrap == MACH_PORT_NULL)
        error (1, 0, "must be started as a translator");
    }

  return 0;
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  int count;

  /* Stop new requests.  */
  ports_inhibit_class_rpcs (trivfs_cntl_class);
  ports_inhibit_class_rpcs (trivfs_protid_class);

  count = ports_count_class (trivfs_protid_class);

  if (count && !(flags & FSYS_GOAWAY_FORCE))
    {
      /* We won't go away, so start things going again...  */
      ports_enable_class (trivfs_protid_class);
      ports_resume_class_rpcs (trivfs_cntl_class);
      ports_resume_class_rpcs (trivfs_protid_class);
      return EBUSY;
    }

  exit (0);
}

static int
demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  mig_routine_t routine;
  if ((routine = device_server_routine (inp)) ||
      (routine = notify_server_routine (inp)) ||
      (routine = mach_i386_server_routine (inp)) ||
      (routine = NULL, trivfs_demuxer (inp, outp)))
    {
      if (routine)
        (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

void
trivfs_modify_stat (struct trivfs_protid *cred, io_statbuf_t *stat)
{
}

void
machdev_trivfs_server(mach_port_t bootstrap)
{
  struct trivfs_control *fsys = NULL;
  int err;

  if (bootstrap != MACH_PORT_NULL)
    {
      err = trivfs_startup (bootstrap, 0,
                            trivfs_cntl_class, port_bucket,
                            trivfs_protid_class, port_bucket, &fsys);
      mach_port_deallocate (mach_task_self (), bootstrap);
      if (err)
        error (1, err, "Contacting parent");
    }

  /* Launch.  */
  do
    {
      ports_manage_port_operations_one_thread (port_bucket, demuxer, 0);
    } while (trivfs_goaway (fsys, 0));
}
