/* Load a task using the single server, and then run it
   as if we were the kernel. 
   Copyright (C) 1993 Free Software Foundation

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

/* Written by Michael I. Bushnell.  */

#include <mach.h>
#include <mach/notify.h>
#include <errno.h>
#include <device/device.h>
#include <a.out.h>

#include "notify_S.h"
#include "exec_S.h"
#include "device_S.h"

mach_port_t privileged_host_port, master_device_port;
mach_port_t pseudo_master_device_port;
mach_port_t receive_set;
mach_port_t pseudo_console;

mach_port_t php_child_name, psmdp_child_name;

task_t child_task;
mach_port_t bootport;

/* These will prevent the Hurd-ish versions from being used */

int
task_by_pid (int pid)
{
  return syscall (-33, pid);
}

int
write (int fd,
       void *buf,
       int buflen)
{
  return syscall (4, fd, buf, buflen);
}

int
read (int fd,
      void *buf,
      int buflen)
{
  return syscall (3, fd, buf, buflen);
}

int
open (char *name,
      int flags,
      int mode)
{
  return syscall (5, name, flags, mode);
}

int
lseek (int fd,
       int off,
       int whence)
{
  return syscall (19, fd, off, whence);
}

int
_exit (int code)
{
  return syscall (1, code);
}


int
request_server (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  return (exec_server (inp, outp)
	  || device_server (inp, outp)
	  || notify_server (inp, outp));
}

vm_address_t
load_image (task_t t,
	    char *file)
{
  int fd;
  struct exec x;
  char *buf;
  int headercruft;
  vm_address_t base = 0x10000;
  int amount;
  vm_address_t bsspagestart, bssstart, stackaddr;
  int magic;

  fd = open (file, 0, 0);
  
  read (fd, &x, sizeof (struct exec));
  magic = N_MAGIC (x);

  headercruft = sizeof (struct exec) * (magic == ZMAGIC);
  
  amount = round_page (headercruft + x.a_text + x.a_data);
  vm_allocate (mach_task_self (), (u_int *)&buf, amount, 1);
  lseek (fd, -headercruft, 1);
  read (fd, buf, amount);
  vm_allocate (t, &base, amount, 0);
  vm_write (t, base, (u_int) buf, amount);
  if (magic != OMAGIC)
    vm_protect (t, base, trunc_page (headercruft + x.a_text),
		0, VM_PROT_READ | VM_PROT_EXECUTE);
  vm_deallocate (mach_task_self (), (u_int)buf, amount);

  bssstart = base + x.a_text + x.a_data + headercruft;
  bsspagestart = round_page (bssstart);
  vm_allocate (t, &bsspagestart, x.a_bss - (bsspagestart - bssstart), 0);
  
  return x.a_entry;
}


int
main (int argc, char **argv, char **envp)
{
  task_t newtask;
  thread_t newthread;
  mach_port_t foo;
  vm_address_t startpc;
  char msg[] = "Boot is here.\n";
  char c;

  write (1, msg, sizeof msg);

  privileged_host_port = task_by_pid (-1);
  master_device_port = task_by_pid (-2);

  task_create (mach_task_self (), 0, &newtask);
  
  startpc = load_image (newtask, argv[1]);
  
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET,
		      &receive_set);
  
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, 
		      &pseudo_master_device_port);
  mach_port_move_member (mach_task_self (), pseudo_master_device_port,
			 receive_set);

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &pseudo_console);
  mach_port_move_member (mach_task_self (), pseudo_console, receive_set);
  mach_port_insert_right (mach_task_self (), pseudo_console, pseudo_console,
			  MACH_MSG_TYPE_MAKE_SEND);

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, &bootport);
  mach_port_move_member (mach_task_self (), bootport, receive_set);

  mach_port_insert_right (mach_task_self (), bootport, bootport, 
			  MACH_MSG_TYPE_MAKE_SEND);
  task_set_bootstrap_port (newtask, bootport);
  mach_port_deallocate (mach_task_self (), bootport);

  mach_port_request_notification (mach_task_self (), newtask, 
				  MACH_NOTIFY_DEAD_NAME, 1, bootport, 
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
  if (foo)
    mach_port_deallocate (mach_task_self (), foo);
  child_task = newtask;

  php_child_name = 100;
  psmdp_child_name = 101;
  mach_port_insert_right (newtask, php_child_name, privileged_host_port,
			  MACH_MSG_TYPE_COPY_SEND);
  mach_port_insert_right (newtask, psmdp_child_name, pseudo_master_device_port,
			  MACH_MSG_TYPE_MAKE_SEND);
  
  thread_create (newtask, &newthread);
  start_thread (newtask, newthread, startpc);

  write (1, "pausing\n", 8);
  read (0, &c, 1);
  thread_resume (newthread);
  
  mach_msg_server (request_server, __vm_page_size * 2, receive_set);
}


/* Implementation of exec interface */


S_exec_exec (
	mach_port_t execserver,
	mach_port_t file,
	mach_port_t oldtask,
	int flags,
	data_t argv,
	mach_msg_type_number_t argvCnt,
	boolean_t argvSCopy,
	data_t envp,
	mach_msg_type_number_t envpCnt,
	boolean_t envpSCopy,
	portarray_t dtable,
	mach_msg_type_number_t dtableCnt,
	portarray_t portarray,
	mach_msg_type_number_t portarrayCnt,
	intarray_t intarray,
	mach_msg_type_number_t intarrayCnt,
	mach_port_array_t deallocnames,
	mach_msg_type_number_t deallocnamesCnt,
	mach_port_array_t destroynames,
	mach_msg_type_number_t destroynamesCnt)
{
  return EOPNOTSUPP;
}

S_exec_init (
	mach_port_t execserver,
	auth_t auth_handle,
	process_t proc_server)
{
  return EOPNOTSUPP;
}

S_exec_setexecdata (
	mach_port_t execserver,
	portarray_t ports,
	mach_msg_type_number_t portsCnt,
	intarray_t ints,
	mach_msg_type_number_t intsCnt)
{
  return EOPNOTSUPP;
}


error_t
S_exec_startup (mach_port_t port,
		u_int *base_addr,
		vm_size_t *stack_size,
		int *flags,
		char **argvP,
		u_int *argvlen,
		char **envpP,
		u_int *envplen,
		mach_port_t **dtableP,
		mach_msg_type_name_t *dtablepoly,
		u_int *dtablelen,
		mach_port_t **portarrayP,
		mach_msg_type_name_t *portarraypoly,
		u_int *portarraylen,
		int **intarrayP,
		u_int *intarraylen)
{
  mach_port_t *portarray, *dtable;
  int *intarray, nc;
  char argv[100];

  /* The argv string has nulls in it; so we use %c for the nulls
     and fill with constant zero. */
  nc = sprintf (argv, "[BOOTSTRAP]%c-x%c%d%c%d%c%s%c", '\0', '\0',
		php_child_name, '\0', psmdp_child_name, '\0', "hd0e", '\0');

  if (nc > *argvlen)
    vm_allocate (mach_task_self (), (vm_address_t *)argvP, nc, 1);
  bcopy (argv, *argvP, nc);
  *argvlen = nc;
  
  *base_addr = *stack_size = 0;

  *flags = 0;
  
  *envplen = 0;

  if (*portarraylen < INIT_PORT_MAX)
    vm_allocate (mach_task_self (), (u_int *)portarrayP,
		 (INIT_PORT_MAX * sizeof (mach_port_t)), 1);
  portarray = *portarrayP;
  *portarraylen = INIT_PORT_MAX;
  *portarraypoly = MACH_MSG_TYPE_COPY_SEND;

  *dtablelen = 0;
  *dtablepoly = MACH_MSG_TYPE_COPY_SEND;
  
  if (*intarraylen < INIT_INT_MAX)
    vm_allocate (mach_task_self (), (u_int *)intarrayP,
		 (INIT_INT_MAX * sizeof (mach_port_t)), 1);
  intarray = *intarrayP;
  *intarraylen = INIT_INT_MAX;
  
  bzero (portarray, INIT_PORT_MAX * sizeof (mach_port_t));
  bzero (intarray, INIT_INT_MAX * sizeof (int));
  
  return 0;
}



/* Implementation of device interface */

ds_device_open (mach_port_t master_port,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		dev_mode_t mode,
		dev_name_t name,
		device_t *device)
{
  if (master_port != pseudo_master_device_port)
    return D_INVALID_OPERATION;
  
  if (!strcmp (name, "console"))
    {
      *device = pseudo_console;
      return 0;
    }
  
  return device_open (master_device_port, mode, name, device);
}

ds_device_close (device_t device)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return 0;
}

ds_device_write (device_t device,
		 mach_port_t reply_port,
		 mach_msg_type_name_t reply_type,
		 dev_mode_t mode,
		 recnum_t recnum,
		 io_buf_ptr_t data,
		 unsigned int datalen,
		 int *bytes_written)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;

  *bytes_written = write (1, (void *)*data, datalen);
  
  return (*bytes_written == -1 ? D_IO_ERROR : D_SUCCESS);
}

ds_device_write_inband (device_t device,
			mach_port_t reply_port,
			mach_msg_type_name_t reply_type,
			dev_mode_t mode,
			recnum_t recnum,
			io_buf_ptr_inband_t data,
			unsigned int datalen,
			int *bytes_written)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;

  *bytes_written = write (1, data, datalen);
  
  return (*bytes_written == -1 ? D_IO_ERROR : D_SUCCESS);
}

ds_device_read (device_t device,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		dev_mode_t mode,
		recnum_t recnum,
		int bytes_wanted,
		io_buf_ptr_t *data,
		unsigned int *datalen)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  
  vm_allocate (mach_task_self (), (pointer_t *)data, bytes_wanted, 1);
  *datalen = read (0, *data, bytes_wanted);

  return (*datalen == -1 ? D_IO_ERROR : D_SUCCESS);
}

ds_device_read_inband (device_t device,
		       mach_port_t reply_port,
		       mach_msg_type_name_t reply_type,
		       dev_mode_t mode,
		       recnum_t recnum,
		       int bytes_wanted,
		       io_buf_ptr_inband_t data,
		       unsigned int *datalen)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  
  *datalen = read (0, data, bytes_wanted);
  
  return (*datalen == -1 ? D_IO_ERROR : D_SUCCESS);
}

ds_xxx_device_set_status (device_t device,
			  dev_flavor_t flavor,
			  dev_status_t status,
			  u_int statu_cnt)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

ds_xxx_device_get_status (device_t device,
			  dev_flavor_t flavor,
			  dev_status_t status,
			  u_int *statuscnt)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

ds_xxx_device_set_filter (device_t device,
			  mach_port_t rec,
			  int pri,
			  filter_array_t filt,
			  unsigned int len)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

ds_device_map (device_t device,
	       vm_prot_t prot,
	       vm_offset_t offset,
	       vm_size_t size,
	       memory_object_t *pager,
	       int unmap)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

ds_device_set_status (device_t device,
		      dev_flavor_t flavor,
		      dev_status_t status,
		      unsigned int statuslen)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

ds_device_get_status (device_t device,
		      dev_flavor_t flavor,
		      dev_status_t status,
		      unsigned int *statuslen)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

ds_device_set_filter (device_t device,
		      mach_port_t receive_port,
		      int priority,
		      filter_array_t filter,
		      unsigned int filterlen)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}


/* Implementation of notify interface */
do_mach_notify_port_deleted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

do_mach_notify_msg_accepted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

do_mach_notify_port_destroyed (mach_port_t notify,
			       mach_port_t port)
{
  return EOPNOTSUPP;
}

do_mach_notify_no_senders (mach_port_t notify,
			   mach_port_mscount_t mscount)
{
  return EOPNOTSUPP;
}

do_mach_notify_send_once (mach_port_t notify)
{
  return EOPNOTSUPP;
}

do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t name)
{
  if (name == child_task && notify == bootport)
    exit ();
}

