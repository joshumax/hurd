/* Load a task using the single server, and then run it
   as if we were the kernel. */
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


/* These will prevent the Hurd-ish versions from being used */

#define task_by_pid(foo) syscall (-33, foo)
#define myfork() syscall (2)
#define execve(foo1, foo2, foo3) syscall (59, foo1, foo2, foo3)
#define write(foo1, foo2, foo3) syscall (4, foo1, foo2, foo3)
#define read(foo1, foo2, foo3) syscall (3, foo1, foo2, foo3)
#define getpid() syscall (20)

int
main (int argc, char **argv, char **envp)
{
  task_t newtask;
  thread_t newthread;
  mach_port_t bootport;
  vm_address_t startpc;

  task_create (mach_task_self (), 0, &newtask);
  
  startpc = load_image (newtask, argv[1]);
  
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, &bootport);
  mach_port_insert_right (mach_task_self (), bootport, bootport, 
			  MACH_MSG_TYPE_MAKE_SEND);
  task_set_bootstrap_port (newtask, bootport);
  mach_port_deallocate (mach_task_self (), bootport);
  
  thread_create (newtask, &newthread);
  start_thread (newtask, newthread, startpc);
  
  request_server ();
}


  mach_port_t boot, recset;
  int parpid;
  int otherpid;
  int err;
  int i;

  err = mach_port_allocate (mach_task_self (),
			    MACH_PORT_RIGHT_RECEIVE,
			    &boot);
  
  if (err)
    write (1, "err1\n", 5);

  device_master = task_by_pid (-2);

  parpid = getpid ();

  otherpid = myfork ();

  write (1, "after fork\n", 11);
  if (otherpid == -1)
    {
      write (1, "error!\n", 7);
      /*       perror ("fork"); */
      exit (1);
    }
  else if (otherpid == parpid)
    {
      __mach_init ();
      for (i = 0; i < 100000; i++);
      write (1, "execing ", 8);
      write (1, argv[1], strlen (argv[1]));
      write (1, "\n", 1);
      execve (argv[1], &argv[1], envp);
      write (1, "exec failed\n", 12);
      /*       perror (argv[1]);*/
      exit (1);
    }
  else
    {
      write (1, "parent?\n", 8);
      err = mach_port_insert_right (mach_task_self (), boot, boot,
				    MACH_MSG_TYPE_MAKE_SEND);
      
      if (err)
	write (1, "err2\n", 5);
      err = task_set_bootstrap_port (task_by_pid (otherpid), boot);
      if (err)
	write (1, "err3\n", 5);
      
      write (1, "parent ", 7);
      writenum (parpid);
      write (1, "\nchild ", 7);
      writenum (otherpid);
      write (1, "\n", 1);

      mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			  &pseudo_master);
      mach_port_insert_right (mach_task_self (), pseudo_master,
			      pseudo_master, MACH_MSG_TYPE_MAKE_SEND);
      mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			  &pseudo_console);
      mach_port_insert_right (mach_task_self (), pseudo_console,
			      pseudo_console, MACH_MSG_TYPE_MAKE_SEND);
      mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET,
			  &recset);
      mach_port_move_member (mach_task_self (), pseudo_master, recset);
      mach_port_move_member (mach_task_self (), pseudo_console, recset);
      mach_port_move_member (mach_task_self (), boot, recset);

      mach_msg_server (my_server, 10000, recset);
    }
}
