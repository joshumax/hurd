/* Load a task using the single server, and then run it
   as if we were the kernel. 
   Copyright (C) 1993, 1994 Free Software Foundation

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
#include <fcntlbits.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <cthreads.h>
#include <varargs.h>

#include "notify_S.h"
#include "exec_S.h"
#include "ourdevice_S.h"
#include "io_S.h"
#include "device_reply_U.h"
#include "io_reply_U.h"
#include "term_S.h"
#include "bootstrap_S.h"
/* #include "tioctl_S.h" */

#include <hurd/auth.h>

mach_port_t privileged_host_port, master_device_port;
mach_port_t pseudo_master_device_port;
mach_port_t receive_set;
mach_port_t pseudo_console;
auth_t authserver;

spin_lock_t queuelock = SPIN_LOCK_INITIALIZER;

mach_port_t php_child_name, psmdp_child_name;

task_t child_task;
mach_port_t bootport;

int boot_like_kernel;
int boot_like_cmudef;
int boot_like_hurd;

int console_mscount;

vm_address_t fs_stack_base;
vm_size_t fs_stack_size;

void init_termstate ();

char *fsname;

char *bootstrap_args;
char *bootdevice = DEFAULT_BOOTDEVICE;

/* We can't include <unistd.h> for this, because that will fight witho
   our definitions of syscalls below. */
int syscall (int, ...);

void set_mach_stack_args ();

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
uxexit (int code)
{
  return syscall (1, code);
}

int
getpid ()
{
  return syscall (20);
}

int
ioctl (int fd, int code, void *buf)
{
  return syscall (54, fd, code, buf);
}
#define IOCPARM_MASK 0x7f
#define IOC_OUT 0x40000000
#define IOC_IN 0x80000000
#define _IOR(x,y,t) (IOC_OUT|((sizeof(t)&IOCPARM_MASK)<<16)|(x<<8)|y)
#define _IOW(x,y,t) (IOC_IN|((sizeof(t)&IOCPARM_MASK)<<16)|(x<<8)|y)
#define FIONREAD _IOR('f', 127, int)
#define FIOASYNC _IOW('f', 125, int)
#define TIOCGETP _IOR('t', 8, struct sgttyb)
#define TIOCLGET _IOR('t', 124, int)
#define TIOCLSET _IOW('t', 125, int)
#define TIOCSETN _IOW('t', 10, struct sgttyb)
#define LDECCTQ 0x4000
#define LLITOUT 0x0020
#define LPASS8  0x0800
#define LNOFLSH 0x8000
#define RAW 0x0020
#define ANYP 0x00c0
#define ECHO 8


struct sgttyb
{
  char unused[4];
  short flags;
};
  

#define SIGIO 23
#define SIGEMSG 30
#define SIGMSG 31
struct sigvec
{
  void (*sv_handler)();
  int sv_mask;
  int sv_flags;
};

int
sigpause (int mask)
{
  return syscall (111, mask);
}


#if 0
void
sigreturn ()
{
  asm volatile ("movl $0x67,%eax\n"
		"lcall $0x7, $0x0\n"
		"ret");
}

void
_sigreturn ()
{
  asm volatile ("addl $0xc, %%esp\n"
		"call %0\n"
		"ret"::"m" (sigreturn));
}

int
sigvec (int sig, struct sigvec *vec, struct sigvec *ovec)
{
  asm volatile ("movl $0x6c,%%eax\n"
		"movl %0, %%edx\n"
		"orl $0x80000000, %%edx\n"
		"lcall $0x7,$0x0\n"
		"ret"::"g" (_sigreturn));
}
#else
int sigvec ();
#endif

int
request_server (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  extern int exec_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int io_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int device_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int notify_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int term_server (mach_msg_header_t *, mach_msg_header_t *);
/*  extern int tioctl_server (mach_msg_header_t *, mach_msg_header_t *); */
  extern int bootstrap_server (mach_msg_header_t *, mach_msg_header_t *);
  extern void bootstrap_compat ();
  
  if (inp->msgh_local_port == bootport && boot_like_cmudef)
    {
      if (inp->msgh_id == 999999)
	{
	  bootstrap_compat (inp, outp);
	  return 1;
	}
      else
	return bootstrap_server (inp, outp);
    }
  else
    return (exec_server (inp, outp)
	    || io_server (inp, outp)
	    || device_server (inp, outp)
	    || notify_server (inp, outp)
	    || term_server (inp, outp)
/*	    || tioctl_server (inp, outp) */);
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
  int rndamount, amount;
  vm_address_t bsspagestart, bssstart;
  char msg[] = "cannot open bootstrap file";
  int magic;

  fd = open (file, 0, 0);

  if (fd == -1)
    {
      write (2, msg, sizeof (msg));
      task_terminate (t);
      uxexit (1);
    }
  
  read (fd, &x, sizeof (struct exec));
  magic = N_MAGIC (x);

  headercruft = sizeof (struct exec) * (magic == ZMAGIC);
  
  amount = headercruft + x.a_text + x.a_data;
  rndamount = round_page (amount);
  vm_allocate (mach_task_self (), (u_int *)&buf, rndamount, 1);
  lseek (fd, -headercruft, 1);
  read (fd, buf, amount);
  vm_allocate (t, &base, rndamount, 0);
  vm_write (t, base, (u_int) buf, rndamount);
  if (magic != OMAGIC)
    vm_protect (t, base, trunc_page (headercruft + x.a_text),
		0, VM_PROT_READ | VM_PROT_EXECUTE);
  vm_deallocate (mach_task_self (), (u_int)buf, rndamount);

  bssstart = base + x.a_text + x.a_data + headercruft;
  bsspagestart = round_page (bssstart);
  vm_allocate (t, &bsspagestart, x.a_bss - (bsspagestart - bssstart), 0);
  
  return x.a_entry;
}


void read_reply ();
void msg_thread ();

int
main (int argc, char **argv, char **envp)
{
  task_t newtask;
  thread_t newthread;
  mach_port_t foo;
  vm_address_t startpc;
  char usagemsg[] = "Usage: boot [-qsdn] first-task [disk]";
  char *bootfile;
  char c;
  struct sigvec vec = { read_reply, 0, 0};
  char *newargs;

  privileged_host_port = task_by_pid (-1);
  master_device_port = task_by_pid (-2);

  if (argc < 2
      || argc > 4
      || (argv[1][0] == '-' && argc < 3)
      || (argv[1][0] != '-' && argc > 3))
    {
      write (2, usagemsg, sizeof usagemsg);
      uxexit (1);
    }

  if (argv[1][0] != '-')
    {
      bootstrap_args = "-x";
      if (argc == 3)
	bootdevice = argv[2];
      bootfile = argv[1];
    }
  else
    {
      bootstrap_args = argv[1];
      bootfile = argv[2];
      if (argc == 4)
	bootdevice = argv[3];
    }

  newargs = malloc (strlen (bootstrap_args) + 2);
  strcpy (newargs, bootstrap_args);
  strcat (newargs, "f");
  bootstrap_args = newargs;

  if (index (bootstrap_args, 'k'))
    boot_like_kernel = 1;
  else if (index (bootstrap_args, 'p'))
    boot_like_cmudef = 1;
  else
    boot_like_hurd = 1;

  task_create (mach_task_self (), 0, &newtask);
  
  startpc = load_image (newtask, bootfile);
  
  fsname = bootfile;
  
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET,
		      &receive_set);
  
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE, 
		      &pseudo_master_device_port);
  mach_port_move_member (mach_task_self (), pseudo_master_device_port,
			 receive_set);

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &pseudo_console);
  mach_port_move_member (mach_task_self (), pseudo_console, receive_set);
  mach_port_request_notification (mach_task_self (), pseudo_console,
				  MACH_NOTIFY_NO_SENDERS, 1, pseudo_console, 
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
  if (foo != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), foo);

  if (boot_like_cmudef || boot_like_hurd)
    {
      mach_port_allocate (mach_task_self (), 
			  MACH_PORT_RIGHT_RECEIVE, &bootport);
      mach_port_move_member (mach_task_self (), bootport, receive_set);

      mach_port_insert_right (mach_task_self (), bootport, bootport, 
			      MACH_MSG_TYPE_MAKE_SEND);
      task_set_bootstrap_port (newtask, bootport);
      mach_port_deallocate (mach_task_self (), bootport);

#if 0
      mach_port_request_notification (mach_task_self (), newtask, 
				      MACH_NOTIFY_DEAD_NAME, 1, bootport, 
				      MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
      if (foo)
	mach_port_deallocate (mach_task_self (), foo);
#endif
    }
  else
    /* Remove inherited port.  The kernel gives none.  */
    task_set_bootstrap_port (newtask, MACH_PORT_NULL);

  child_task = newtask;

  if (boot_like_kernel || boot_like_hurd)
    {
      php_child_name = 100;
      psmdp_child_name = 101;
      mach_port_insert_right (newtask, php_child_name, privileged_host_port,
			      MACH_MSG_TYPE_COPY_SEND);
      mach_port_insert_right (newtask, psmdp_child_name,
			      pseudo_master_device_port,
			      MACH_MSG_TYPE_MAKE_SEND);
    }

  foo = 1;
  init_termstate ();
  ioctl (0, FIOASYNC, &foo);
  sigvec (SIGIO, &vec, 0);
  sigvec (SIGMSG, &vec, 0);
  sigvec (SIGEMSG, &vec, 0);
  
  thread_create (newtask, &newthread);

  if (boot_like_hurd)
    __mach_setup_thread (newtask, newthread, (char *)startpc, &fs_stack_base,
			 &fs_stack_size);
  else if (boot_like_kernel) 
    {
      char hp[20], mdp[20];
      sprintf (hp, "%d", (int) php_child_name);
      sprintf (mdp, "%d", (int) psmdp_child_name);
      set_mach_stack_args (newtask, newthread, (void *) startpc,
			   "[BOOTSTRAP fs]", bootstrap_args,
			   hp, mdp, bootdevice, 0);
    }
  else
    set_mach_stack_args (newtask, newthread, (char *)startpc,
			 "[BOOTSTRAP fs]", bootstrap_args,
			 bootdevice, "/", 0);

  if (index (bootstrap_args, 'd'))
    {
      write (1, "pausing\n", 8);
      read (0, &c, 1);
    }
  thread_resume (newthread);
  
  cthread_detach (cthread_fork ((cthread_fn_t) msg_thread,
				(any_t) 0));
  
  while (1)
    {
      sigpause (0);
    }
  
/*  mach_msg_server (request_server, __vm_page_size * 2, receive_set); */
}

/* Set up stack args the Mach way */
void
set_mach_stack_args (user_task,
		     user_thread,
		     startpc,
		     va_alist)
     task_t user_task;
     thread_t user_thread;
     char *startpc;
     va_dcl
{
  /* This code is lifted from .../mk/bootstrap/load.c. */
  va_list			argv_ptr;
  char *			arg_ptr;
  
  int			arg_len;
  int			arg_count;
  char *			arg_pos;
  unsigned int		arg_item_len;

	/*
	 * Calculate the size of the argument list.
	 */
	va_start(argv_ptr);
	arg_len = 0;
	arg_count = 0;
	for (;;) {
	    arg_ptr = va_arg(argv_ptr, char *);
	    if (arg_ptr == (char *)0)
		break;
	    arg_count++;
	    arg_len += strlen(arg_ptr) + 1;	/* space for '\0' */
	}
	va_end(argv_ptr);
	/*
	 * Add space for:
	 *    arg_count
	 *    pointers to arguments
	 *    trailing 0 pointer
	 *    dummy 0 pointer to environment variables
	 *    and align to integer boundary
	 */
	arg_len += sizeof(integer_t) + (2 + arg_count) * sizeof(char *);
	arg_len = (arg_len + (sizeof(integer_t) - 1)) & ~(sizeof(integer_t)-1);

  
  /* This small piece is from .../mk/bootstrap/i386/exec.c. */
  {
	vm_offset_t	stack_start;
	vm_offset_t	stack_end;
	struct i386_thread_state	regs;
	unsigned int		reg_size;

#define STACK_SIZE (1024 * 1024 * 16)

	/*
	 * Add space for 5 ints to arguments, for
	 * PS program. XXX
	 */
	arg_len += 5 * sizeof(int);

	/*
	 * Allocate stack.
	 */
	stack_end = VM_MAX_ADDRESS;
	stack_start = VM_MAX_ADDRESS - STACK_SIZE;
	(void)vm_allocate(user_task,
			  &stack_start,
			  (vm_size_t)(stack_end - stack_start),
			  FALSE);

	reg_size = i386_THREAD_STATE_COUNT;
	(void)thread_get_state(user_thread,
				i386_THREAD_STATE,
				(thread_state_t)&regs,
				&reg_size);

	regs.eip = (int) startpc;
	regs.uesp = (int)((stack_end - arg_len) & ~(sizeof(int)-1));

	(void)thread_set_state(user_thread,
				i386_THREAD_STATE,
				(thread_state_t)&regs,
				reg_size);

	arg_pos = (void *) regs.uesp;
      }    

	/*
	 * Copy out the arguments.
	 */
	{
	    vm_offset_t	u_arg_start;
				/* user start of argument list block */
	    vm_offset_t	k_arg_start;
				/* kernel start of argument list block */
	    vm_offset_t u_arg_page_start;
				/* user start of args, page-aligned */
	    vm_size_t	arg_page_size;
				/* page_aligned size of args */
	    vm_offset_t	k_arg_page_start;
				/* kernel start of args, page-aligned */

	    register
	    char **	k_ap;	/* kernel arglist address */
	    char *	u_cp;	/* user argument string address */
	    register
	    char *	k_cp;	/* kernel argument string address */
	    register
	    int		i;

	    /*
	     * Get address of argument list in user space
	     */
	    u_arg_start = (vm_offset_t)arg_pos;

	    /*
	     * Round to page boundaries, and allocate kernel copy
	     */
	    u_arg_page_start = trunc_page(u_arg_start);
	    arg_page_size = (vm_size_t)(round_page(u_arg_start + arg_len)
					- u_arg_page_start);

	    vm_allocate(mach_task_self(),
				 &k_arg_page_start,
				 (vm_size_t)arg_page_size,
				 TRUE);

	    /*
	     * Set up addresses corresponding to user pointers
	     * in the kernel block
	     */
	    k_arg_start = k_arg_page_start + (u_arg_start - u_arg_page_start);

	    k_ap = (char **)k_arg_start;

	    /*
	     * Start the strings after the arg-count and pointers
	     */
	    u_cp = (char *)u_arg_start + arg_count * sizeof(char *)
					+ 2 * sizeof(char *)
					+ sizeof(integer_t);
	    k_cp = (char *)k_arg_start + arg_count * sizeof(char *)
					+ 2 * sizeof(char *)
					+ sizeof(integer_t);

	    /*
	     * first the argument count
	     */
	    *k_ap++ = (char *)(natural_t)arg_count;

	    /*
	     * Then the strings and string pointers for each argument
	     */
	    va_start(argv_ptr);
	    for (i = 0; i < arg_count; i++) {
		arg_ptr = va_arg(argv_ptr, char *);
		arg_item_len = strlen(arg_ptr) + 1; /* include trailing 0 */

		/* set string pointer */
		*k_ap++ = u_cp;

		/* copy string */
		bcopy(arg_ptr, k_cp, arg_item_len);
		k_cp += arg_item_len;
		u_cp += arg_item_len;
	    }
	    va_end(argv_ptr);

	    /*
	     * last, the trailing 0 argument and a null environment pointer.
	     */
	    *k_ap++ = (char *)0;
	    *k_ap   = (char *)0;

	    /*
	     * Now write all of this to user space.
	     */
	    (void) vm_write(user_task,
			    u_arg_page_start,
			    k_arg_page_start,
			    arg_page_size);

	    (void) vm_deallocate(mach_task_self(),
				 k_arg_page_start,
				 arg_page_size);
	}
}


void
msg_thread()
{
  while (1)
    mach_msg_server (request_server, 0, receive_set);
}


enum read_type
{
  DEV_READ,
  DEV_READI,
  IO_READ,
};
struct qr
{
  enum read_type type;
  mach_port_t reply_port;
  mach_msg_type_name_t reply_type;
  int amount;
  struct qr *next;
};
struct qr *qrhead, *qrtail;

/* Queue a read for later reply. */
void
queue_read (enum read_type type,
	    mach_port_t reply_port,
	    mach_msg_type_name_t reply_type,
	    int amount)
{
  struct qr *qr;

  spin_lock (&queuelock);
  
  qr = malloc (sizeof (struct qr));
  qr->type = type;
  qr->reply_port = reply_port;
  qr->reply_type = reply_type;
  qr->amount = amount;
  qr->next = 0;
  if (qrtail)
    qrtail->next = qr;
  else
    qrhead = qrtail = qr;

  spin_unlock (&queuelock);
}

/* Reply to a queued read. */
void
read_reply ()
{
  int avail;
  struct qr *qr;
  char * buf;
  int amtread;

  spin_lock (&queuelock);
  
  if (!qrhead)
    {
      spin_unlock (&queuelock);
      return;
    }
  
  qr = qrhead;
  qrhead = qr->next;
  if (qr == qrtail)
    qrtail = 0;

  spin_unlock (&queuelock);
  
  ioctl (0, FIONREAD, &avail);
  if (!avail)
    return;
  
  if (qr->type == DEV_READ)
    vm_allocate (mach_task_self (), (vm_address_t *)&buf, qr->amount, 1);
  else
    buf = alloca (qr->amount);
  amtread = read (0, buf, qr->amount);

  switch (qr->type)
    {
    case DEV_READ:
      if (amtread >= 0)
	ds_device_read_reply (qr->reply_port, qr->reply_type, 0,
			      (io_buf_ptr_t) buf, amtread);
      else
	ds_device_read_reply (qr->reply_port, qr->reply_type, errno, 0, 0);
      break;
      
    case DEV_READI:
      if (amtread >= 0)
	ds_device_read_reply_inband (qr->reply_port, qr->reply_type, 0,
				     buf, amtread);
      else
	ds_device_read_reply_inband (qr->reply_port, qr->reply_type, errno,
				     0, 0);
      break;
      
    case IO_READ:
      if (amtread >= 0)
	io_read_reply (qr->reply_port, qr->reply_type, 0,
		       buf, amtread);
      else
	io_read_reply (qr->reply_port, qr->reply_type, errno, 0, 0);
      break;
    }

  free (qr);
}

/*
 *	Handle bootstrap requests.
 */
/* These two functions from .../mk/bootstrap/default_pager.c. */

kern_return_t
do_bootstrap_privileged_ports(bootstrap, hostp, devicep)
	mach_port_t bootstrap;
	mach_port_t *hostp, *devicep;
{
	*hostp = privileged_host_port;
	*devicep = pseudo_master_device_port;
	return KERN_SUCCESS;
}

void
bootstrap_compat(in, out)
	mach_msg_header_t *in, *out;
{
	mig_reply_header_t *reply = (mig_reply_header_t *) out;
	mach_msg_return_t mr;

	struct imsg {
		mach_msg_header_t	hdr;
		mach_msg_type_t		port_desc_1;
		mach_port_t		port_1;
		mach_msg_type_t		port_desc_2;
		mach_port_t		port_2;
	} imsg;

	/*
	 * Send back the host and device ports.
	 */

	imsg.hdr.msgh_bits = MACH_MSGH_BITS_COMPLEX |
		MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(in->msgh_bits), 0);
	/* msgh_size doesn't need to be initialized */
	imsg.hdr.msgh_remote_port = in->msgh_remote_port;
	imsg.hdr.msgh_local_port = MACH_PORT_NULL;
	/* msgh_seqno doesn't need to be initialized */
	imsg.hdr.msgh_id = in->msgh_id + 100;	/* this is a reply msg */

	imsg.port_desc_1.msgt_name = MACH_MSG_TYPE_COPY_SEND;
	imsg.port_desc_1.msgt_size = (sizeof(mach_port_t) * 8);
	imsg.port_desc_1.msgt_number = 1;
	imsg.port_desc_1.msgt_inline = TRUE;
	imsg.port_desc_1.msgt_longform = FALSE;
	imsg.port_desc_1.msgt_deallocate = FALSE;
	imsg.port_desc_1.msgt_unused = 0;

	imsg.port_1 = privileged_host_port;

	imsg.port_desc_2 = imsg.port_desc_1;

	imsg.port_desc_2.msgt_name = MACH_MSG_TYPE_MAKE_SEND;
	imsg.port_2 = pseudo_master_device_port;

	/*
	 * Send the reply message.
	 * (mach_msg_server can not do this, because the reply
	 * is not in standard format.)
	 */

	mr = mach_msg(&imsg.hdr, MACH_SEND_MSG,
		      sizeof imsg, 0, MACH_PORT_NULL,
		      MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
	if (mr != MACH_MSG_SUCCESS)
		(void) mach_port_deallocate(mach_task_self (),
					    imsg.hdr.msgh_remote_port);

	/*
	 * Tell mach_msg_server to do nothing.
	 */

	reply->RetCode = MIG_NO_REPLY;
}


/* Implementation of exec interface */


kern_return_t
S_exec_exec (mach_port_t execserver,
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
	     boolean_t dtableSCopy,
	     portarray_t portarray,
	     mach_msg_type_number_t portarrayCnt,
	     boolean_t portarraySCopy,
	     intarray_t intarray,
	     mach_msg_type_number_t intarrayCnt,
	     boolean_t intarraySCopy,
	     mach_port_array_t deallocnames,
	     mach_msg_type_number_t deallocnamesCnt,
	     mach_port_array_t destroynames,
	     mach_msg_type_number_t destroynamesCnt)
{
  return EOPNOTSUPP;
}

kern_return_t
S_exec_init (
	mach_port_t execserver,
	auth_t auth_handle,
	process_t proc_server)
{
  /* Kludgy way to get a port to the auth server.  */
  authserver = auth_handle;
  if (proc_server != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), proc_server);
  return 0;
}

kern_return_t
S_exec_setexecdata (mach_port_t execserver,
		    portarray_t ports,
		    mach_msg_type_number_t portsCnt,
		    boolean_t portsSCopy,
		    intarray_t ints,
		    mach_msg_type_number_t intsCnt,
		    boolean_t intsSCopy)
{
  return EOPNOTSUPP;
}


kern_return_t
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
  mach_port_t *portarray;
  int *intarray, nc;
  char argv[100];

  if (!boot_like_hurd)
    return EOPNOTSUPP;

  /* The argv string has nulls in it; so we use %c for the nulls
     and fill with constant zero. */
  nc = sprintf (argv, "[BOOTSTRAP %s]%c%s%c%d%c%d%c%s", fsname, '\0', 
		bootstrap_args, '\0', php_child_name, '\0', 
		psmdp_child_name, '\0', bootdevice);

  if (nc > *argvlen)
    vm_allocate (mach_task_self (), (vm_address_t *)argvP, nc, 1);
  bcopy (argv, *argvP, nc);
  *argvlen = nc;
  
  *base_addr = fs_stack_base;
  *stack_size = fs_stack_size;

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



/* Imlementiation of tioctl interface */
/* This is bletcherously kludged to work with emacs in a fragile
   way. */
int term_modes[4];
char term_ccs[20];
int term_speeds[2];
struct sgttyb term_sgb;
int localbits;

#define ICANON (1 << 8)

void
init_termstate ()
{
  struct sgttyb sgb;
  int bits;
  ioctl (0, TIOCGETP, &term_sgb);
  ioctl (0, TIOCLGET, &localbits);
  /* Enter raw made.  Rather than try and interpret these bits,
     we just do what emacs does in .../emacs/src/sysdep.c for
     an old style terminal driver. */
  bits = localbits | LDECCTQ | LLITOUT | LPASS8 | LNOFLSH;
  ioctl (0, TIOCLSET, &bits);
  sgb = term_sgb;
  sgb.flags &= ~ECHO;
  sgb.flags |= RAW | ANYP;
  ioctl (0, TIOCSETN, &sgb);
}

void
restore_termstate ()
{
  ioctl (0, TIOCLSET, &localbits);
  ioctl (0, TIOCSETN, &term_sgb);
}

#ifdef notanymore
#include <termios.h>
#undef tcgetattr
#undef tcsetattr

kern_return_t 
S_tioctl_tiocgeta (mach_port_t port,
		   int modes[],
		   char ccs[],
		   int speeds[])
{
#if 1
  union { struct termios t;
	  struct { int modes[4]; char cc[20]; int speed[2]; } s; } u;
  if (tcgetattr (0, &u.t))
    return errno;
  bcopy (u.s.modes, modes, sizeof u.s.modes);
  bcopy (u.s.cc, ccs, sizeof u.s.cc);
  bcopy (u.s.speed, speeds, sizeof u.s.speed);
  return 0;
#else
  /* Emacs reads the terminal state in one of two cases:
     1) Checking whether or not a preceding tiocseta succeeded;
     2) Finding out what the state of the terminal was on startup. 
     In case (1) in only cares that we return exactly what it set;
     in case (2) it only uses it for a later seta on exit.  So we
     can just tell it what's lying around. */
  modes[0] = term_modes[0];
  modes[1] = term_modes[1];
  modes[2] = term_modes[2];
  modes[3] = term_modes[3];
  
  bcopy (term_ccs, ccs, 20);
  
  speeds[0] = term_speeds[0];
  speeds[1] = term_speeds[1];
  return 0;
#endif
}

kern_return_t
S_tioctl_tiocseta (mach_port_t port,
		   int modes[],
		   char ccs[],
		   int speeds[])
{
#if 1
  union { struct termios t;
	  struct { int modes[4]; char cc[20]; int speed[2]; } s; } u;
  memcpy (u.s.modes, modes, sizeof u.s.modes);
  memcpy (u.s.cc, ccs, sizeof u.s.cc);
  memcpy (u.s.speed, speeds, sizeof u.s.speed);
  return tcsetattr (0, 0, &u.t) ? errno : 0;
#else
  /* Emacs sets the termanal stet in one of two cases:
     1) Putting the terminal into raw mode for running;
     2) Restoring the terminal to its original state. 
     Because ICANON is set in the original state, and because
     emacs always clears ICANON when running, this tells us which
     is going on. */
  if ((modes[3] & ICANON) == 0)
    {
      struct sgttyb sgb;
      int bits;
      /* Enter raw made.  Rather than try and interpret these bits,
	 we just do what emacs does in .../emacs/src/sysdep.c for
	 an old style terminal driver. */
      bits = localbits | LDECCTQ | LLITOUT | LPASS8 | LNOFLSH;
      ioctl (0, TIOCLSET, &bits);
      sgb = term_sgb;
      sgb.flags &= ~ECHO;
      sgb.flags |= RAW | ANYP;
      ioctl (0, TIOCSETN, &sgb);
    }
  else
    {
      /* Leave raw mode */
      ioctl (0, TIOCLSET, &localbits);
      ioctl (0, TIOCSETN, &term_sgb);
    }

  term_modes[0] = modes[0];
  term_modes[1] = modes[1];
  term_modes[2] = modes[2];
  term_modes[3] = modes[3];
  bcopy (ccs, term_ccs, 20);
  term_speeds[0] = speeds[0];
  term_speeds[1] = speeds[1];
  return 0;
#endif
}

kern_return_t
S_tioctl_tiocsetaw (mach_port_t port,
		  int modes[4],
		  char ccs[20],
		  int speeds[2])
{
  return S_tioctl_tiocseta (port, modes, ccs, speeds);
}

kern_return_t
S_tioctl_tiocsetaf (mach_port_t port,
		  int modes[4],
		  char ccs[20],
		  int speeds[2])
{
  return S_tioctl_tiocseta (port, modes, ccs, speeds);
}
#endif /* notanymore */


/* Implementation of device interface */

kern_return_t
ds_device_open (mach_port_t master_port,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		dev_mode_t mode,
		dev_name_t name,
		mach_port_t *device,
		mach_msg_type_name_t *devicetype)
{
  if (master_port != pseudo_master_device_port)
    return D_INVALID_OPERATION;
  
  if (!strcmp (name, "console"))
    {
#if 0
      mach_port_insert_right (mach_task_self (), pseudo_console,
			      pseudo_console, MACH_MSG_TYPE_MAKE_SEND);
      console_send_rights++;
#endif
      console_mscount++;
      *device = pseudo_console;
      *devicetype = MACH_MSG_TYPE_MAKE_SEND;
      return 0;
    }
  
  *devicetype = MACH_MSG_TYPE_MOVE_SEND;
  return device_open (master_device_port, mode, name, device);
}

kern_return_t
ds_device_close (device_t device)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return 0;
}

kern_return_t
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

#if 0
  if (console_send_rights)
    {
      mach_port_mod_refs (mach_task_self (), pseudo_console, 
			  MACH_PORT_TYPE_SEND, -console_send_rights);
      console_send_rights = 0;
    }
#endif

  *bytes_written = write (1, data, datalen);
  
  return (*bytes_written == -1 ? D_IO_ERROR : D_SUCCESS);
}

kern_return_t
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

#if 0
  if (console_send_rights)
    {
      mach_port_mod_refs (mach_task_self (), pseudo_console, 
			  MACH_PORT_TYPE_SEND, -console_send_rights);
      console_send_rights = 0;
    }
#endif

  *bytes_written = write (1, data, datalen);
  
  return (*bytes_written == -1 ? D_IO_ERROR : D_SUCCESS);
}

kern_return_t
ds_device_read (device_t device,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		dev_mode_t mode,
		recnum_t recnum,
		int bytes_wanted,
		io_buf_ptr_t *data,
		unsigned int *datalen)
{
  int avail;
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;

#if 0  
  if (console_send_rights)
    {
      mach_port_mod_refs (mach_task_self (), pseudo_console, 
			  MACH_PORT_TYPE_SEND, -console_send_rights);
      console_send_rights = 0;
    }
#endif

  ioctl (0, FIONREAD, &avail);
  if (avail)
    {
      vm_allocate (mach_task_self (), (pointer_t *)data, bytes_wanted, 1);
      *datalen = read (0, *data, bytes_wanted);
      return (*datalen == -1 ? D_IO_ERROR : D_SUCCESS);
    }
  else
    {
      queue_read (DEV_READ, reply_port, reply_type, bytes_wanted);
      return MIG_NO_REPLY;
    }
}

kern_return_t
ds_device_read_inband (device_t device,
		       mach_port_t reply_port,
		       mach_msg_type_name_t reply_type,
		       dev_mode_t mode,
		       recnum_t recnum,
		       int bytes_wanted,
		       io_buf_ptr_inband_t data,
		       unsigned int *datalen)
{
  int avail;
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;

#if 0  
  if (console_send_rights)
    {
      mach_port_mod_refs (mach_task_self (), pseudo_console, 
			  MACH_PORT_TYPE_SEND, -console_send_rights);
      console_send_rights = 0;
    }
#endif

  ioctl (0, FIONREAD, &avail);
  if (avail)
    {
      *datalen = read (0, data, bytes_wanted);
      return (*datalen == -1 ? D_IO_ERROR : D_SUCCESS);
    }
  else
    {
      queue_read (DEV_READI, reply_port, reply_type, bytes_wanted);
      return MIG_NO_REPLY;
    }
}

kern_return_t
ds_xxx_device_set_status (device_t device,
			  dev_flavor_t flavor,
			  dev_status_t status,
			  u_int statu_cnt)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

kern_return_t
ds_xxx_device_get_status (device_t device,
			  dev_flavor_t flavor,
			  dev_status_t status,
			  u_int *statuscnt)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

kern_return_t
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

kern_return_t
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

kern_return_t
ds_device_set_status (device_t device,
		      dev_flavor_t flavor,
		      dev_status_t status,
		      unsigned int statuslen)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_get_status (device_t device,
		      dev_flavor_t flavor,
		      dev_status_t status,
		      unsigned int *statuslen)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

kern_return_t
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
kern_return_t
do_mach_notify_port_deleted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_msg_accepted (mach_port_t notify,
			     mach_port_t name)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_port_destroyed (mach_port_t notify,
			       mach_port_t port)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_no_senders (mach_port_t notify,
			   mach_port_mscount_t mscount)
{
  mach_port_t foo;
  if (notify == pseudo_console)
    {
      if (mscount == console_mscount)
	{
	  restore_termstate ();
	  uxexit (0);
	}
      else
	{
	  mach_port_request_notification (mach_task_self (), pseudo_console,
					  MACH_NOTIFY_NO_SENDERS, 
					  console_mscount, pseudo_console,
					  MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
	  if (foo != MACH_PORT_NULL)
	    mach_port_deallocate (mach_task_self (), foo);
	}
    }

  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_send_once (mach_port_t notify)
{
  return EOPNOTSUPP;
}

kern_return_t
do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t name)
{
#if 0
  if (name == child_task && notify == bootport)
    uxexit (0);
#endif
  return EOPNOTSUPP;
}


/* Implementation of the Hurd I/O interface, which
   we support for the console port only. */

kern_return_t
S_io_write (mach_port_t object,
	    mach_port_t reply_port,
	    mach_msg_type_name_t reply_type,
	    char *data,
	    mach_msg_type_number_t datalen,
	    off_t offset,
	    mach_msg_type_number_t *amtwritten)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;

#if 0
  if (console_send_rights)
    {
      mach_port_mod_refs (mach_task_self (), pseudo_console, 
			  MACH_PORT_TYPE_SEND, -console_send_rights);
      console_send_rights = 0;
    }
#endif

  *amtwritten = write (1, data, datalen);
  return *amtwritten == -1 ? errno : 0;
}

kern_return_t
S_io_read (mach_port_t object,
	   mach_port_t reply_port,
	   mach_msg_type_name_t reply_type,
	   char **data,
	   mach_msg_type_number_t *datalen,
	   off_t offset,
	   mach_msg_type_number_t amount)
{
  mach_msg_type_number_t avail;
  if (object != pseudo_console)
    return EOPNOTSUPP;
  
#if 0
  if (console_send_rights)
    {
      mach_port_mod_refs (mach_task_self (), pseudo_console, 
			  MACH_PORT_TYPE_SEND, -console_send_rights);
      console_send_rights = 0;
    }
#endif

  ioctl (0, FIONREAD, &avail);
  if (avail)
    {
      if (amount > *datalen)
	vm_allocate (mach_task_self (), (vm_address_t *) data, amount, 1);
      *datalen = read (0, *data, amount);
      return *datalen == -1 ? errno : 0;
    }
  else
    {
      queue_read (IO_READ, reply_port, reply_type, amount);
      return MIG_NO_REPLY;
    }
}

kern_return_t 
S_io_seek (mach_port_t object,
	   mach_port_t reply_port,
	   mach_msg_type_name_t reply_type,
	   off_t offset,
	   int whence,
	   off_t *newp)
{
  return object == pseudo_console ? ESPIPE : EOPNOTSUPP;
}

kern_return_t
S_io_readable (mach_port_t object,
	       mach_port_t reply_port,
	       mach_msg_type_name_t reply_type,
	       mach_msg_type_number_t *amt)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;
  ioctl (0, FIONREAD, amt);
  return 0;
}

kern_return_t 
S_io_set_all_openmodes (mach_port_t object,
			mach_port_t reply_port,
			mach_msg_type_name_t reply_type,
			int bits)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_get_openmodes (mach_port_t object,
		    mach_port_t reply_port,
		    mach_msg_type_name_t reply_type,
		    int *modes)
{
  *modes = O_READ | O_WRITE;
  return object == pseudo_console ? 0 : EOPNOTSUPP;
}

kern_return_t
S_io_set_some_openmodes (mach_port_t object,
			 mach_port_t reply_port,
			 mach_msg_type_name_t reply_type,
			 int bits)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_clear_some_openmodes (mach_port_t object,
			   mach_port_t reply_port,
			   mach_msg_type_name_t reply_type,
			   int bits)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_async (mach_port_t object,
	    mach_port_t reply_port,
	    mach_msg_type_name_t reply_type,
	    mach_port_t notify,
	    mach_port_t *id,
	    mach_msg_type_name_t *idtype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_mod_owner (mach_port_t object,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		pid_t owner)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_get_owner (mach_port_t object,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		pid_t *owner)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_get_icky_async_id (mach_port_t object,
			mach_port_t reply_port,
			mach_msg_type_name_t reply_type,
			mach_port_t *id,
			mach_msg_type_name_t *idtype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_select (mach_port_t object,
	     mach_port_t reply_port,
	     mach_msg_type_name_t reply_type,
	     int type,
	     mach_port_t ret,
	     mach_msg_type_name_t rettype,
	     int tag,
	     int *result)
{
  fd_set r, w, x;
  int n;

  if (object != pseudo_console)
    return EOPNOTSUPP;

  FD_ZERO (&r);
  FD_ZERO (&w);
  FD_ZERO (&x);
  FD_SET (0, &r);
  FD_SET (0, &w);
  FD_SET (0, &x);

  n = select (1,
	      (type & SELECT_READ) ? &r : 0,
	      (type & SELECT_WRITE) ? &w : 0,
	      (type & SELECT_URG) ? &x : 0,
	      0);
  if (n < 0)
    return errno;

  if (! FD_ISSET (0, &r))
    type &= ~SELECT_READ;
  if (! FD_ISSET (0, &w))
    type &= ~SELECT_WRITE;
  if (! FD_ISSET (0, &x))
    type &= ~SELECT_URG;

  *result = type;
  return 0;
}

kern_return_t
S_io_stat (mach_port_t object,
	   mach_port_t reply_port,
	   mach_msg_type_name_t reply_type,
	   struct stat *st)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;
  
  bzero (st, sizeof (struct stat));
  st->st_blksize = 1024;
  return 0;
}

kern_return_t
S_io_reauthenticate (mach_port_t object,
		     mach_port_t reply_port,
		     mach_msg_type_name_t reply_type,
		     mach_port_t rend)
{
  uid_t *gu, *au;
  gid_t *gg, *ag;
  unsigned int gulen = 0, aulen = 0, gglen = 0, aglen = 0;
    
  if (! auth_server_authenticate (authserver, 
				  object, MACH_MSG_TYPE_MAKE_SEND,
				  rend, MACH_MSG_TYPE_MOVE_SEND,
				  object, MACH_MSG_TYPE_MAKE_SEND,
				  &gu, &gulen,
				  &au, &aulen,
				  &gg, &gglen,
				  &ag, &aglen))
    {
      mig_deallocate (gu, gulen * sizeof *gu);
      mig_deallocate (au, aulen * sizeof *gu);
      mig_deallocate (gg, gglen * sizeof *gu);
      mig_deallocate (au, aulen * sizeof *gu);
    }

  return 0;
}

kern_return_t
S_io_restrict_auth (mach_port_t object,
		    mach_port_t reply_port,
		    mach_msg_type_name_t reply_type,
		    mach_port_t *newobject,
		    mach_msg_type_name_t *newobjtype,
		    uid_t *uids,
		    u_int nuids,
		    uid_t *gids,
		    u_int ngids)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;
  *newobject = pseudo_console;
  *newobjtype = MACH_MSG_TYPE_MAKE_SEND;
  console_mscount++;
  return 0;
}

kern_return_t
S_io_duplicate (mach_port_t object,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		mach_port_t *newobj,
		mach_msg_type_name_t *newobjtype)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;
  *newobj = pseudo_console;
  *newobjtype = MACH_MSG_TYPE_MAKE_SEND;
  console_mscount++;
  return 0;
}

kern_return_t
S_io_server_version (mach_port_t object,
		     mach_port_t reply_port,
		     mach_msg_type_name_t reply_type,
		     char *name,
		     int *maj,
		     int *min,
		     int *edit)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_map (mach_port_t obj,
	  mach_port_t reply_port,
	  mach_msg_type_name_t reply_type,
	  mach_port_t *rd,
	  mach_msg_type_name_t *rdtype,
	  mach_port_t *wr,
	  mach_msg_type_name_t *wrtype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_map_cntl (mach_port_t obj,
	       mach_port_t reply_port,
	       mach_msg_type_name_t reply_type,
	       mach_port_t *mem,
	       mach_msg_type_name_t *memtype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_get_conch (mach_port_t obj,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_release_conch (mach_port_t obj,
		    mach_port_t reply_port,
		    mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_eofnotify (mach_port_t obj,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type)

{
  return EOPNOTSUPP;
}

kern_return_t
S_io_prenotify (mach_port_t obj,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		vm_offset_t start,
		vm_offset_t end)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_postnotify (mach_port_t obj,
		 mach_port_t reply_port,
		 mach_msg_type_name_t reply_type,
		 vm_offset_t start,
		 vm_offset_t end)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_readsleep (mach_port_t obj,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_readnotify (mach_port_t obj,
		 mach_port_t reply_port,
		 mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}


kern_return_t
S_io_sigio (mach_port_t obj,
	    mach_port_t reply_port,
	    mach_msg_type_name_t reply_type)
{
  return EOPNOTSUPP;
}

    


/* Implementation of the Hurd terminal driver interface, which we only
   support on the console device.  */

kern_return_t
S_termctty_open_terminal (mach_port_t object,
			  int flags,
			  mach_port_t *result,
			  mach_msg_type_name_t *restype)
{
  return EOPNOTSUPP;
}

kern_return_t
S_term_getctty (mach_port_t object,
		mach_port_t *cttyid, mach_msg_type_name_t *cttyPoly)
{
  static mach_port_t id = MACH_PORT_NULL;

  if (object != pseudo_console)
    return EOPNOTSUPP;

  if (id == MACH_PORT_NULL)
    mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_DEAD_NAME, &id);

  *cttyid = id;
  *cttyPoly = MACH_MSG_TYPE_COPY_SEND;
  return 0;
}


kern_return_t S_term_open_ctty
(
	io_t terminal,
	pid_t pid,
	pid_t pgrp,
	mach_port_t *newtty,
	mach_msg_type_name_t *newttytype
)
{ return EOPNOTSUPP; }

kern_return_t S_term_set_nodename
(
	io_t terminal,
	string_t name
)
{ return EOPNOTSUPP; }

kern_return_t S_term_get_nodename
(
	io_t terminal,
	string_t name
)
{ return EOPNOTSUPP; }

kern_return_t S_term_set_filenode
(
	io_t terminal,
	file_t filenode
)
{ return EOPNOTSUPP; }

kern_return_t S_term_get_bottom_type
(
	io_t terminal,
	int *ttype
)
{ return EOPNOTSUPP; }

kern_return_t S_term_on_machdev
(
	io_t terminal,
	mach_port_t machdev
)
{ return EOPNOTSUPP; }

kern_return_t S_term_on_hurddev
(
	io_t terminal,
	io_t hurddev
)
{ return EOPNOTSUPP; }

kern_return_t S_term_on_pty
(
	io_t terminal,
	io_t *ptymaster
)
{ return EOPNOTSUPP; }

