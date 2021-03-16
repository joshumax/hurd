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

#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <error.h>
#include <sys/mman.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd/fsys.h>
#include <hurd/paths.h>
#include <hurd/startup.h>
#include <hurd.h>
#include <mach/i386/mach_i386.h>
#include <device/device.h> /* mach console */

#include "libdiskfs/diskfs.h"
#include "startup_notify_S.h"
#include "device_S.h"
#include "notify_S.h"
#include "fsys_S.h"
#include "mach_i386_S.h"

#include "trivfs_server.h"
#include "libmachdev/machdev.h"

struct port_bucket *port_bucket;

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;
int trivfs_allow_open = O_READ | O_WRITE;

/* Our port classes */
struct port_class *trivfs_cntl_class;
struct port_class *trivfs_protid_class;

/* Our control struct */
struct trivfs_control *control;

/* Are we providing bootstrap translator? */
static boolean_t bootstrapping;

/* Our underlying node in the FS for bootstrap */
static mach_port_t underlying;

/* The FS control port */
static mach_port_t control_port;

/* Our device path for injecting bootstrapped translator onto */
static char *devnode;

/* Startup and shutdown notifications management */
struct port_class *machdev_shutdown_notify_class;

static void arrange_shutdown_notification (void);

static void
install_as_translator (mach_port_t bootport)
{
  error_t err;

  underlying = file_name_lookup (devnode, O_NOTRANS, 0);
  if (! MACH_PORT_VALID (underlying))
    return;

  /* Install translator */
  err = file_set_translator (underlying,
                             0, FS_TRANS_SET, 0,
                             NULL, 0,
                             bootport, MACH_MSG_TYPE_COPY_SEND);
  assert_perror_backtrace (err);
}

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

kern_return_t
trivfs_S_fsys_startup (mach_port_t bootport,
                       mach_port_t reply,
                       mach_msg_type_name_t replytype,
                       int flags,
                       mach_port_t cntl,
                       mach_port_t *realnode,
                       mach_msg_type_name_t *realnodetype)
{
  mach_port_t mybootport;

  control_port = cntl;
  *realnode = MACH_PORT_NULL;
  *realnodetype = MACH_MSG_TYPE_COPY_SEND;

  task_get_bootstrap_port (mach_task_self (), &mybootport);
  if (mybootport)
    fsys_startup (mybootport, flags, control_port, MACH_MSG_TYPE_COPY_SEND, realnode);
  return 0;
}

static void
essential_task (void)
{
  mach_port_t host, startup;

  get_privileged_ports (&host, 0);
  startup = file_name_lookup (_SERVERS_STARTUP, 0, 0);
  if (startup == MACH_PORT_NULL)
    {
      mach_print ("WARNING: Cannot register as essential task\n");
      mach_port_deallocate (mach_task_self (), host);
      return;
    }
  startup_essential_task (startup, mach_task_self (), MACH_PORT_NULL,
                          program_invocation_short_name, host);
  mach_port_deallocate (mach_task_self (), startup);
  mach_port_deallocate (mach_task_self (), host);
}

kern_return_t
trivfs_S_fsys_init (struct trivfs_control *fsys,
                    mach_port_t reply, mach_msg_type_name_t replytype,
                    mach_port_t procserver,
                    mach_port_t authhandle)
{
  error_t err;
  mach_port_t *portarray;
  unsigned int i;
  uid_t idlist[] = {0, 0, 0};
  mach_port_t root, bootstrap;
  retry_type retry;
  string_t retry_name;
  mach_port_t right = MACH_PORT_NULL;
  process_t proc;

  /* Traverse to the bootstrapping server first */
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap)
    {
      err = fsys_init (bootstrap, procserver, MACH_MSG_TYPE_COPY_SEND, authhandle);
      assert_perror_backtrace (err);
    }
  err = fsys_getroot (control_port, MACH_PORT_NULL, MACH_MSG_TYPE_COPY_SEND,
                      idlist, 3, idlist, 3, 0,
                      &retry, retry_name, &root);
  assert_perror_backtrace (err);
  assert_backtrace (retry == FS_RETRY_NORMAL);
  assert_backtrace (retry_name[0] == '\0');
  assert_backtrace (root != MACH_PORT_NULL);

  portarray = mmap (0, INIT_PORT_MAX * sizeof *portarray,
                    PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  for (i = 0; i < INIT_PORT_MAX; ++i)
    portarray[i] = MACH_PORT_NULL;
  portarray[INIT_PORT_PROC] = procserver;
  portarray[INIT_PORT_AUTH] = authhandle;
  portarray[INIT_PORT_CRDIR] = root;
  portarray[INIT_PORT_CWDIR] = root;
  _hurd_init (0, NULL, portarray, INIT_PORT_MAX, NULL, 0);

  /* Mark us as important.  */
  proc = getproc ();
  assert_backtrace (proc);
  err = proc_mark_important (proc);
  assert_perror_backtrace (err);
  err = proc_mark_exec (proc);
  assert_perror_backtrace (err);
  proc_set_exe (proc, program_invocation_short_name);
  mach_port_deallocate (mach_task_self (), proc);

  if (bootstrapping)
    {
      if (devnode)
        {
          /* Install the bootstrap port on /dev/something so users
           * can still access the bootstrapped device */
          right = ports_get_send_right (&control->pi);
          install_as_translator (right);
          control->underlying = underlying;
        }
      /* Mark us as essential if bootstrapping */
      essential_task ();
    }

  arrange_shutdown_notification ();

  return 0;
}

static void
arrange_shutdown_notification (void)
{
  error_t err;
  mach_port_t initport, notify;
  struct port_info *pi;

  machdev_shutdown_notify_class = ports_create_class (0, 0);

  /* Arrange to get notified when the system goes down */
  err = ports_create_port (machdev_shutdown_notify_class, port_bucket,
			   sizeof (struct port_info), &pi);
  if (err)
    return;

  initport = file_name_lookup (_SERVERS_STARTUP, 0, 0);
  if (initport == MACH_PORT_NULL)
    {
      mach_print ("WARNING: machdev not registered for shutdown\n");
      return;
    }

  notify = ports_get_send_right (pi);
  ports_port_deref (pi);
  startup_request_notification (initport, notify,
				MACH_MSG_TYPE_MAKE_SEND,
				program_invocation_short_name);
  mach_port_deallocate (mach_task_self (), notify);
  mach_port_deallocate (mach_task_self (), initport);
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

  right = ports_get_send_right (&control->pi);
  err = get_privileged_ports (host_priv, NULL);
  if (!err)
    {
      *dev_master = right;
      *fstask = mach_task_self ();
      *hp_type = *dm_type = MACH_MSG_TYPE_MOVE_SEND;
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

  assert_backtrace (server_task != MACH_PORT_NULL);

  right = ports_get_send_right (&control->pi);
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

  printf ("%s ", server_name);
  fflush (stdout);
}

int
machdev_trivfs_init(mach_port_t bootstrap_resume_task, const char *name, const char *path,
                    mach_port_t *bootstrap)
{
  mach_port_t mybootstrap = MACH_PORT_NULL;
  task_t parent_task;
  port_bucket = ports_create_bucket ();
  trivfs_cntl_class = ports_create_class (trivfs_clean_cntl, 0);
  trivfs_protid_class = ports_create_class (trivfs_clean_protid, 0);
  trivfs_create_control (MACH_PORT_NULL, trivfs_cntl_class, port_bucket,
                         trivfs_protid_class, 0, &control);

  *bootstrap = MACH_PORT_NULL;

  task_get_bootstrap_port (mach_task_self (), &mybootstrap);
  if (mybootstrap)
    {
      *bootstrap = mybootstrap;
      fsys_getpriv (*bootstrap, &_hurd_host_priv, &_hurd_device_master, &parent_task);
    }

  if (bootstrap_resume_task != MACH_PORT_NULL)
    {
      if (path)
	devnode = strdup(path);
      resume_bootstrap_server(bootstrap_resume_task, name);

      /* We need to install as a translator later */
      bootstrapping = TRUE;
    }
  else
    {
      if (*bootstrap == MACH_PORT_NULL)
        error (1, 0, "must be started as a translator");

      /* We do not need to install as a translator later */
      bootstrapping = FALSE;
    }

  return 0;
}

/* The system is going down. Sync data, then call trivfs_goaway() */
error_t
S_startup_dosync (mach_port_t handle)
{
  struct port_info *inpi = ports_lookup_port (port_bucket, handle,
					      machdev_shutdown_notify_class);

  if (!inpi)
    return EOPNOTSUPP;

  ports_port_deref (inpi);

  /* Sync and close device(s) */
  machdev_device_shutdown (handle);

  return trivfs_goaway (NULL, FSYS_GOAWAY_FORCE);
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
      (routine = startup_notify_server_routine (inp)) ||
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

static void *
machdev_trivfs_loop(void *arg)
{
  struct trivfs_control *fsys = (struct trivfs_control *)arg;

  /* Launch.  */
  do
    {
      ports_manage_port_operations_one_thread (port_bucket, demuxer, 0);
    } while (trivfs_goaway (fsys, 0));

  /* Never reached */
  return 0;
}

void
machdev_trivfs_server(mach_port_t bootstrap)
{
  struct trivfs_control *fsys = NULL;
  int err;
  pthread_t t;

  if (bootstrapping == FALSE)
    {
      /* This path is executed when a parent exists */
      err = trivfs_startup (bootstrap, 0,
                            trivfs_cntl_class, port_bucket,
                            trivfs_protid_class, port_bucket, &fsys);
      mach_port_deallocate (mach_task_self (), bootstrap);
      if (err)
        error (1, err, "Contacting parent");
    }
  else
    {
      fsys = control;
    }

  err = pthread_create (&t, NULL, machdev_trivfs_loop, (void *)fsys);
  if (err)
    error (1, err, "Creating machdev server thread");
  pthread_detach (t);
}
