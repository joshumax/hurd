/* Load a task using the single server, and then run it
   as if we were the kernel.
   Copyright (C) 1993,94,95,96,97,98,99,2000,01,02,2006,14,16
     Free Software Foundation, Inc.

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
#include <device/device.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <mach/task_notify.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <mach/mig_support.h>
#include <mach/default_pager.h>
#include <argp.h>
#include <hurd/store.h>
#include <hurd/ihash.h>
#include <sys/reboot.h>
#include <sys/mman.h>
#include <version.h>

#include "notify_S.h"
#include "device_S.h"
#include "io_S.h"
#include "device_reply_U.h"
#include "io_reply_U.h"
#include "term_S.h"
#include "bootstrap_S.h"
/* #include "tioctl_S.h" */
#include "mach_S.h"
#include "mach_host_S.h"
#include "gnumach_S.h"
#include "task_notify_S.h"

#include "boot_script.h"

#include <hurd/auth.h>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <error.h>
#include <hurd.h>
#include <assert-backtrace.h>

#include "private.h"

/* We support two modes of operation.  Traditionally, Subhurds were
   privileged, i.e. they had the privileged kernel ports.  This has a
   few drawbacks.  Privileged subhurds can manipulate all tasks on the
   system and halt the system.  Nowadays we allow an unprivileged
   mode.  */
static int privileged;
static int want_privileged;

static struct termios orig_tty_state;
static int isig;
static char *kernel_command_line;

static void
init_termstate ()
{
  struct termios tty_state;

  if (tcgetattr (0, &tty_state) < 0)
    error (10, errno, "tcgetattr");

  orig_tty_state = tty_state;
  cfmakeraw (&tty_state);
  if (isig)
    tty_state.c_lflag |= ISIG;

  if (tcsetattr (0, 0, &tty_state) < 0)
    error (11, errno, "tcsetattr");
}

static void
restore_termstate ()
{
  tcsetattr (0, 0, &orig_tty_state);
}

#define host_fstat fstat
typedef struct stat host_stat_t;

void __attribute__ ((__noreturn__))
host_exit (int status)
{
  restore_termstate ();
  exit (status);
}

int verbose;

mach_port_t privileged_host_port, master_device_port;
mach_port_t pseudo_privileged_host_port;
mach_port_t pseudo_master_device_port;
mach_port_t receive_set;
mach_port_t pseudo_console, pseudo_root, pseudo_time;
mach_port_t pseudo_pset;
task_t pseudo_kernel;
mach_port_t task_notification_port;
mach_port_t dead_task_notification_port;
auth_t authserver;

/* The proc server registers for new task notifications which we will
   send to this port.  */
mach_port_t new_task_notification;

struct store *root_store;

pthread_spinlock_t queuelock = PTHREAD_SPINLOCK_INITIALIZER;
pthread_spinlock_t readlock = PTHREAD_SPINLOCK_INITIALIZER;

mach_port_t php_child_name, psmdp_child_name, taskname;

task_t child_task;
mach_port_t bootport;

int console_mscount;

vm_address_t fs_stack_base;
vm_size_t fs_stack_size;

void init_termstate ();
void restore_termstate ();

char *fsname;

char bootstrap_args[100] = "-";
char *bootdevice = 0;
char *bootscript = 0;


void safe_gets (char *buf, int buf_len)
{
  fgets (buf, buf_len, stdin);
}

extern char *useropen_dir;

/* XXX: glibc should provide mig_reply_setup but does not.  */
/* Fill in default response.  */
void
mig_reply_setup (
	const mach_msg_header_t	*in,
	mach_msg_header_t	*out)
{
      static const mach_msg_type_t RetCodeType = {
		/* msgt_name = */		MACH_MSG_TYPE_INTEGER_32,
		/* msgt_size = */		32,
		/* msgt_number = */		1,
		/* msgt_inline = */		TRUE,
		/* msgt_longform = */		FALSE,
		/* msgt_deallocate = */		FALSE,
		/* msgt_unused = */		0
	};

#define	InP	(in)
#define	OutP	((mig_reply_header_t *) out)
      OutP->Head.msgh_bits =
	MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(InP->msgh_bits), 0);
      OutP->Head.msgh_size = sizeof *OutP;
      OutP->Head.msgh_remote_port = InP->msgh_remote_port;
      OutP->Head.msgh_local_port = MACH_PORT_NULL;
      OutP->Head.msgh_seqno = 0;
      OutP->Head.msgh_id = InP->msgh_id + 100;
      OutP->RetCodeType = RetCodeType;
      OutP->RetCode = MIG_BAD_ID;
#undef InP
#undef OutP
}

error_t
mach_msg_forward (mach_msg_header_t *inp,
                  mach_port_t destination, mach_msg_type_name_t destination_type)
{
  /* Put the reply port back at the correct position, insert new
     destination.  */
  inp->msgh_local_port = inp->msgh_remote_port;
  inp->msgh_remote_port = destination;
  inp->msgh_bits =
    MACH_MSGH_BITS (destination_type, MACH_MSGH_BITS_REMOTE (inp->msgh_bits))
    | MACH_MSGH_BITS_OTHER (inp->msgh_bits);

  /* A word about resources carried in complex messages.

     "In a received message, msgt_deallocate is TRUE in type
     descriptors for out-of-line memory".  Therefore, "[the
     out-of-line memory] is implicitly deallocated from the sender
     [when we resend the message], as if by vm_deallocate".

     Similarly, rights in messages will be either
     MACH_MSG_TYPE_PORT_SEND, MACH_MSG_TYPE_PORT_SEND_ONCE, or
     MACH_MSG_TYPE_PORT_RECEIVE.  These types are aliases for,
     respectively, MACH_MSG_TYPE_MOVE_SEND,
     MACH_MSG_TYPE_MOVE_SEND_ONCE, and MACH_MSG_TYPE_MOVE_RECEIVE.
     Therefore, the rights are moved when we resend the message.  */

  return mach_msg (inp, MACH_SEND_MSG, inp->msgh_size,
                   0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
}

int
boot_demuxer (mach_msg_header_t *inp,
	      mach_msg_header_t *outp)
{
  error_t err;
  mig_routine_t routine;
  mig_reply_setup (inp, outp);

  if (inp->msgh_local_port == task_notification_port
      && MACH_PORT_VALID (new_task_notification)
      && 24000 <= inp->msgh_id && inp->msgh_id < 24100)
    {
      /* This is a message of the Process subsystem.  We relay this to
         allow the "outer" proc servers to communicate with the "inner"
         one.  */
      mig_reply_header_t *reply = (mig_reply_header_t *) outp;

      if (MACH_PORT_VALID (new_task_notification))
        err = mach_msg_forward (inp, new_task_notification, MACH_MSG_TYPE_COPY_SEND);
      else
        err = EOPNOTSUPP;

      if (err)
        reply->RetCode = err;
      else
        reply->RetCode = MIG_NO_REPLY;

      return TRUE;
    }

  if ((routine = io_server_routine (inp)) ||
      (routine = device_server_routine (inp)) ||
      (routine = notify_server_routine (inp)) ||
      (routine = term_server_routine (inp)) ||
      (routine = mach_server_routine (inp)) ||
      (routine = mach_host_server_routine (inp)) ||
      (routine = gnumach_server_routine (inp)) ||
      (routine = task_notify_server_routine (inp))
      /* (routine = tioctl_server_routine (inp)) */)
    {
      (*routine) (inp, outp);
      return TRUE;
    }
  else
    return FALSE;
}

void read_reply ();
void * msg_thread (void *);

const char *argp_program_version = STANDARD_HURD_VERSION (boot);

#define OPT_PRIVILEGED	-1
#define OPT_BOOT_SCRIPT	-2

static struct argp_option options[] =
{
  { NULL, 0, NULL, 0, "Boot options:" },
  { "boot-script", OPT_BOOT_SCRIPT, "BOOT-SCRIPT", 0,
    "boot script to execute" },
  { "boot-root",   'D', "DIR", 0,
    "Root of a directory tree in which to find files specified in BOOT-SCRIPT" },
  { "single-user", 's', 0, 0,
    "Boot in single user mode" },
  { "kernel-command-line", 'c', "COMMAND LINE", 0,
    "Simulated multiboot command line to supply" },
  { "verbose",     'v', 0, 0,
    "Be verbose" },
  { "pause" ,      'd', 0, 0,
    "Pause for user confirmation at various times during booting" },
  { "isig",      'I', 0, 0,
    "Do not disable terminal signals, so you can suspend and interrupt boot"},
  { "device",	   'f', "SUBHURD_NAME=DEVICE_FILE", 0,
    "Pass the given DEVICE_FILE to the Subhurd as device SUBHURD_NAME"},
  { "privileged", OPT_PRIVILEGED, NULL, 0,
    "Allow the subhurd to access privileged kernel ports"},
  { 0 }
};
static char doc[] = "Boot a second hurd";



/* Device pass through.  */

struct dev_map
{
  char *device_name;	/* The name of the device in the Subhurd.  */
  char *file_name;	/* The filename outside the Subhurd.  */
  struct dev_map *next;
};

static struct dev_map *dev_map_head;

static struct dev_map *
add_dev_map (const char *dev_name, const char *dev_file)
{
  file_t node;
  struct dev_map *map;

  /* See if we can open the file.  */
  node = file_name_lookup (dev_file, 0, 0);
  if (! MACH_PORT_VALID (node))
    error (1, errno, "%s", dev_file);
  mach_port_deallocate (mach_task_self (), node);

  map = malloc (sizeof *map);
  if (map == NULL)
    return NULL;

  map->device_name = strdup (dev_name);
  map->file_name = strdup (dev_file);
  map->next = dev_map_head;
  dev_map_head = map;
  return map;
}

static struct dev_map *lookup_dev (char *dev_name)
{
  struct dev_map *map;

  for (map = dev_map_head; map; map = map->next)
    {
      if (strcmp (map->device_name, dev_name) == 0)
	return map;
    }
  return NULL;
}

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  char *dev_file;

  switch (key)
    {
      size_t len;

    case 'c':  kernel_command_line = arg; break;

    case 'D':  useropen_dir = arg; break;

    case 'I':  isig = 1; break;

    case 'v':
      verbose += 1;
      break;

    case 's': case 'd':
      len = strlen (bootstrap_args);
      if (len >= sizeof bootstrap_args - 1)
	argp_error (state, "Too many bootstrap args");
      bootstrap_args[len++] = key;
      bootstrap_args[len] = '\0';
      break;

    case 'f':
      dev_file = strchr (arg, '=');
      if (dev_file == NULL)
	return ARGP_ERR_UNKNOWN;
      *dev_file = 0;
      add_dev_map (arg, dev_file+1);
      break;

    case OPT_PRIVILEGED:
      want_privileged = 1;
      break;

    case OPT_BOOT_SCRIPT:
      bootscript = arg;
      break;

    case ARGP_KEY_ARG:
      return ARGP_ERR_UNKNOWN;

    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input; break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static error_t
allocate_pseudo_ports (void)
{
  mach_port_t old;

  /* Allocate a port that we hand out as the privileged host port.  */
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &pseudo_privileged_host_port);
  mach_port_insert_right (mach_task_self (),
			  pseudo_privileged_host_port,
			  pseudo_privileged_host_port,
			  MACH_MSG_TYPE_MAKE_SEND);
  mach_port_move_member (mach_task_self (), pseudo_privileged_host_port,
			 receive_set);
  mach_port_request_notification (mach_task_self (),
                                  pseudo_privileged_host_port,
				  MACH_NOTIFY_NO_SENDERS, 1,
				  pseudo_privileged_host_port,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &old);
  assert_backtrace (old == MACH_PORT_NULL);

  /* Allocate a port that we hand out as the privileged processor set
     port.  */
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &pseudo_pset);
  mach_port_move_member (mach_task_self (), pseudo_pset,
			 receive_set);
  /* Make one send right that we copy when handing it out.  */
  mach_port_insert_right (mach_task_self (),
			  pseudo_pset,
			  pseudo_pset,
			  MACH_MSG_TYPE_MAKE_SEND);

  /* We will receive new task notifications on this port.  */
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &task_notification_port);
  mach_port_move_member (mach_task_self (), task_notification_port,
			 receive_set);

  /* And information about dying tasks here.  */
  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &dead_task_notification_port);
  mach_port_move_member (mach_task_self (), dead_task_notification_port,
			 receive_set);

  return 0;
}

void
read_boot_script (char **buffer, size_t *length)
{
  char *p, *buf;
  static const char filemsg[] = "Can't open boot script\n";
  static const char memmsg[] = "Not enough memory\n";
  int i, fd;
  size_t amt, len;

  fd = open (bootscript, O_RDONLY, 0);
  if (fd < 0)
    {
      write (2, filemsg, sizeof (filemsg));
      host_exit (1);
    }
  p = buf = malloc (500);
  if (!buf)
    {
      write (2, memmsg, sizeof (memmsg));
      host_exit (1);
    }
  len = 500;
  amt = 0;
  while (1)
    {
      i = read (fd, p, len - (p - buf));
      if (i <= 0)
        break;
      p += i;
      amt += i;
      if (p == buf + len)
        {
          char *newbuf;

          len += 500;
          newbuf = realloc (buf, len);
          if (!newbuf)
            {
              write (2, memmsg, sizeof (memmsg));
              host_exit (1);
            }
          p = newbuf + (p - buf);
          buf = newbuf;
        }
    }

  close (fd);
  *buffer = buf;
  *length = amt;
}


/* Boot script file for booting contemporary GNU Hurd systems.  Each
   line specifies a file to be loaded by the boot loader (the first
   word), and actions to be done with it.  */
const char *default_boot_script =
  /* First, the bootstrap filesystem.  It needs several ports as
     arguments, as well as the user flags from the boot loader.  */
  "/hurd/ext2fs.static"
  " --readonly"
  " --multiboot-command-line=${kernel-command-line}"
  " --host-priv-port=${host-port}"
  " --device-master-port=${device-port}"
  " --kernel-task=${kernel-task}"
  " --exec-server-task=${exec-task}"
  " -T device ${root-device} $(task-create) $(task-resume)"
  "\n"

  /* Now the exec server; to load the dynamically-linked exec server
     program, we have the boot loader in fact load and run ld.so,
     which in turn loads and runs /hurd/exec.  This task is created,
     and its task port saved in ${exec-task} to be passed to the fs
     above, but it is left suspended; the fs will resume the exec task
     once it is ready.  */
  "/lib/ld.so /hurd/exec $(exec-task=task-create)"
  "\n";


int
main (int argc, char **argv, char **envp)
{
  error_t err;
  mach_port_t foo;
  char *buf = 0;
  pthread_t pthread_id;
  char *root_store_name;
  const struct argp_child kids[] = { { &store_argp, 0, "Store options:", -2 },
                                     { 0 }};
  struct argp argp = { options, parse_opt, NULL, doc, kids };
  struct store_argp_params store_argp_params = { 0 };

  argp_parse (&argp, argc, argv, 0, 0, &store_argp_params);
  err = store_parsed_name (store_argp_params.result, &root_store_name);
  if (err)
    error (2, err, "store_parsed_name");

  err = store_parsed_open (store_argp_params.result, 0, &root_store);
  if (err)
    error (4, err, "%s", root_store_name);

  if (want_privileged)
    {
      get_privileged_ports (&privileged_host_port, &master_device_port);
      privileged = MACH_PORT_VALID (master_device_port);

      if (! privileged)
        error (1, 0, "Must be run as root for privileged subhurds");
    }

  if (privileged)
    strcat (bootstrap_args, "f");

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET,
		      &receive_set);

  if (root_store->class == &store_device_class && root_store->name
      && (root_store->flags & STORE_ENFORCED)
      && root_store->num_runs == 1
      && root_store->runs[0].start == 0
      && privileged)
    /* Let known device nodes pass through directly.  */
    bootdevice = root_store->name;
  else
    /* Pass a magic value that we can use to do I/O to ROOT_STORE.  */
    {
      bootdevice = "pseudo-root";
      mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			  &pseudo_root);
      mach_port_move_member (mach_task_self (), pseudo_root, receive_set);
    }

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &pseudo_master_device_port);
  mach_port_insert_right (mach_task_self (),
			  pseudo_master_device_port,
			  pseudo_master_device_port,
			  MACH_MSG_TYPE_MAKE_SEND);
  mach_port_move_member (mach_task_self (), pseudo_master_device_port,
			 receive_set);
  mach_port_request_notification (mach_task_self (), pseudo_master_device_port,
				  MACH_NOTIFY_NO_SENDERS, 1,
				  pseudo_master_device_port,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
  if (foo != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), foo);

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &pseudo_console);
  mach_port_move_member (mach_task_self (), pseudo_console, receive_set);
  mach_port_request_notification (mach_task_self (), pseudo_console,
				  MACH_NOTIFY_NO_SENDERS, 1, pseudo_console,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
  if (foo != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), foo);

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
		      &pseudo_time);
  mach_port_move_member (mach_task_self (), pseudo_time, receive_set);
  mach_port_request_notification (mach_task_self (), pseudo_time,
				  MACH_NOTIFY_NO_SENDERS, 1, pseudo_time,
				  MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
  if (foo != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), foo);

  if (! privileged)
    {
      err = allocate_pseudo_ports ();
      if (err)
        error (1, err, "Allocating pseudo ports");

      /* Create a new task namespace for us.  */
      err = proc_make_task_namespace (getproc (), task_notification_port,
                                      MACH_MSG_TYPE_MAKE_SEND);
      if (err)
        error (1, err, "proc_make_task_namespace");

      /* Create an empty task that the subhurds can freely frobnicate.  */
      err = task_create (mach_task_self (), 0, &pseudo_kernel);
      if (err)
        error (1, err, "task_create");
    }

  if (kernel_command_line == 0)
    asprintf (&kernel_command_line, "%s %s root=%s",
	      argv[0], bootstrap_args, bootdevice);

  /* Initialize boot script variables.  */
  if (boot_script_set_variable ("host-port", VAL_PORT,
                                privileged
                                ? (int) privileged_host_port
				: (int) pseudo_privileged_host_port)
      || boot_script_set_variable ("device-port", VAL_PORT,
				   (integer_t) pseudo_master_device_port)
      || boot_script_set_variable ("kernel-task", VAL_PORT,
				   (integer_t) pseudo_kernel)
      || boot_script_set_variable ("kernel-command-line", VAL_STR,
				   (integer_t) kernel_command_line)
      || boot_script_set_variable ("root-device",
				   VAL_STR, (integer_t) bootdevice)
      || boot_script_set_variable ("boot-args",
				   VAL_STR, (integer_t) bootstrap_args))
    {
      static const char msg[] = "error setting variable";

      write (2, msg, strlen (msg));
      host_exit (1);
    }

  /* Turn each `FOO=BAR' word in the command line into a boot script
     variable ${FOO} with value BAR.  */
  {
    int len = strlen (kernel_command_line) + 1;
    char *s = memcpy (alloca (len), kernel_command_line, len);
    char *word;

    while ((word = strsep (&s, " \t")) != 0)
      {
       char *eq = strchr (word, '=');
       if (eq == 0)
         continue;
       *eq++ = '\0';
       err = boot_script_set_variable (word, VAL_STR, (integer_t) eq);
       if (err)
         {
           char *msg;
           asprintf (&msg, "cannot set boot-script variable %s: %s\n",
                     word, boot_script_error_string (err));
           assert_backtrace (msg);
           write (2, msg, strlen (msg));
           free (msg);
           host_exit (1);
         }
      }
  }

  /* Parse the boot script.  */
  {
    char *p, *line;
    size_t amt;
    if (bootscript)
      read_boot_script (&buf, &amt);
    else
      buf = strdup (default_boot_script), amt = strlen (default_boot_script);

    line = p = buf;
    while (1)
      {
	while (p < buf + amt && *p != '\n')
	  p++;
	*p = '\0';
	err = boot_script_parse_line (0, line);
	if (err)
	  {
	    char *str;
	    int i;

	    str = boot_script_error_string (err);
	    i = strlen (str);
	    write (2, str, i);
	    write (2, " in `", 5);
	    write (2, line, strlen (line));
	    write (2, "'\n", 2);
	    host_exit (1);
	  }
	if (p == buf + amt)
	  break;
	line = ++p;
      }
  }

  if (index (bootstrap_args, 'd'))
    {
      static const char msg[] = "Pausing. . .";
      char c;
      write (2, msg, sizeof (msg) - 1);
      read (0, &c, 1);
    }

  init_termstate ();

  /* The boot script has now been parsed into internal data structures.
     Now execute its directives.  */
  {
    err = boot_script_exec ();
    if (err)
      {
	char *str = boot_script_error_string (err);
	int i = strlen (str);

	write (2, str, i);
	write (2, "\n",  1);
	host_exit (1);
      }
    free (buf);
  }

  mach_port_deallocate (mach_task_self (), pseudo_master_device_port);

  err = pthread_create (&pthread_id, NULL, msg_thread, NULL);
  if (!err)
    pthread_detach (pthread_id);
  else
    {
      errno = err;
      perror ("pthread_create");
    }

  for (;;)
    {
      fd_set rmask;
      FD_ZERO (&rmask);
      FD_SET (0, &rmask);
      if (select (1, &rmask, 0, 0, 0) == 1)
	read_reply ();
      else if (errno != EINTR)
        /* We hosed */
	error (5, errno, "select");
    }
}

void *
msg_thread (void *arg)
{
  while (1)
    mach_msg_server (boot_demuxer, 0, receive_set);
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
kern_return_t
queue_read (enum read_type type,
	    mach_port_t reply_port,
	    mach_msg_type_name_t reply_type,
	    int amount)
{
  struct qr *qr;

  qr = malloc (sizeof (struct qr));
  if (!qr)
    return D_NO_MEMORY;

  pthread_spin_lock (&queuelock);

  qr->type = type;
  qr->reply_port = reply_port;
  qr->reply_type = reply_type;
  qr->amount = amount;
  qr->next = 0;
  if (qrtail)
    qrtail->next = qr;
  else
    qrhead = qrtail = qr;

  pthread_spin_unlock (&queuelock);
  return D_SUCCESS;
}

/* TRUE if there's data available on stdin, which should be used to satisfy
   console read requests.  */
static int should_read = 0;

/* Reply to a queued read. */
void
read_reply ()
{
  int avail;
  struct qr *qr;
  char * buf;
  int amtread;

  /* By forcing SHOULD_READ to true before trying the lock, we ensure that
     either we get the lock ourselves or that whoever currently holds the
     lock will service this read when he unlocks it.  */
  should_read = 1;
  if (pthread_spin_trylock (&readlock))
    return;

  /* Since we're committed to servicing the read, no one else need do so.  */
  should_read = 0;

  ioctl (0, FIONREAD, &avail);
  if (!avail)
    {
      pthread_spin_unlock (&readlock);
      return;
    }

  pthread_spin_lock (&queuelock);

  if (!qrhead)
    {
      pthread_spin_unlock (&queuelock);
      pthread_spin_unlock (&readlock);
      return;
    }

  qr = qrhead;
  qrhead = qr->next;
  if (qr == qrtail)
    qrtail = 0;

  pthread_spin_unlock (&queuelock);

  if (qr->type == DEV_READ)
    buf = mmap (0, qr->amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  else
    buf = alloca (qr->amount);
  amtread = read (0, buf, qr->amount);

  pthread_spin_unlock (&readlock);

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

/* Unlock READLOCK, and also service any new read requests that it was
   blocking.  */
static void
unlock_readlock ()
{
  pthread_spin_unlock (&readlock);
  while (should_read)
    read_reply ();
}

/*
 *	Handle bootstrap requests.
 */
kern_return_t
do_bootstrap_privileged_ports(bootstrap, hostp, devicep)
	mach_port_t bootstrap;
	mach_port_t *hostp, *devicep;
{
	*hostp = privileged_host_port;
	*devicep = pseudo_master_device_port;
	return KERN_SUCCESS;
}

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
  struct dev_map *map;

  if (master_port != pseudo_master_device_port)
    return D_INVALID_OPERATION;

  if (verbose > 1)
    fprintf (stderr, "Device '%s' being opened.\r\n", name);

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
  else if (!strcmp (name, "time"))
    {
      *device = pseudo_time;
      *devicetype = MACH_MSG_TYPE_MAKE_SEND;
      return 0;
    }
  else if (strcmp (name, "pseudo-root") == 0)
    /* Magic root device.  */
    {
      *device = pseudo_root;
      *devicetype = MACH_MSG_TYPE_MAKE_SEND;
      return 0;
    }

  map = lookup_dev (name);
  if (map)
    {
      error_t err;
      file_t node;

      node = file_name_lookup (map->file_name, 0, 0);
      if (! MACH_PORT_VALID (node))
        return D_NO_SUCH_DEVICE;

      *devicetype = MACH_MSG_TYPE_MOVE_SEND;
      err = device_open (node, mode, "", device);
      mach_port_deallocate (mach_task_self (), node);
      return err;
    }

  if (! privileged)
    return D_NO_SUCH_DEVICE;

  *devicetype = MACH_MSG_TYPE_MOVE_SEND;
  return device_open (master_device_port, mode, name, device);
}

kern_return_t
ds_device_close (device_t device)
{
  if (device != pseudo_console && device != pseudo_root)
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
		 size_t datalen,
		 int *bytes_written)
{
  if (device == pseudo_console)
    {
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
  else if (device == pseudo_root)
    {
      size_t wrote;
      if (store_write (root_store, recnum, data, datalen, &wrote) != 0)
	return D_IO_ERROR;
      *bytes_written = wrote;
      return D_SUCCESS;
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_write_inband (device_t device,
			mach_port_t reply_port,
			mach_msg_type_name_t reply_type,
			dev_mode_t mode,
			recnum_t recnum,
			io_buf_ptr_inband_t data,
			size_t datalen,
			int *bytes_written)
{
  if (device == pseudo_console)
    {
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
  else if (device == pseudo_root)
    {
      size_t wrote;
      if (store_write (root_store, recnum, data, datalen, &wrote) != 0)
	return D_IO_ERROR;
      *bytes_written = wrote;
      return D_SUCCESS;
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_read (device_t device,
		mach_port_t reply_port,
		mach_msg_type_name_t reply_type,
		dev_mode_t mode,
		recnum_t recnum,
		int bytes_wanted,
		io_buf_ptr_t *data,
		size_t *datalen)
{
  if (device == pseudo_console)
    {
      int avail;

#if 0
      if (console_send_rights)
	{
	  mach_port_mod_refs (mach_task_self (), pseudo_console,
			      MACH_PORT_TYPE_SEND, -console_send_rights);
	  console_send_rights = 0;
	}
#endif

      pthread_spin_lock (&readlock);
      ioctl (0, FIONREAD, &avail);
      if (avail)
	{
	  *data = mmap (0, bytes_wanted, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  *datalen = read (0, *data, bytes_wanted);
	  unlock_readlock ();
	  return (*datalen == -1 ? D_IO_ERROR : D_SUCCESS);
	}
      else
	{
	  kern_return_t err;

	  unlock_readlock ();
	  err = queue_read (DEV_READ, reply_port, reply_type, bytes_wanted);
	  if (err)
	    return err;
	  return MIG_NO_REPLY;
	}
    }
  else if (device == pseudo_root)
    {
      *datalen = 0;
      return
	(store_read (root_store, recnum, bytes_wanted, (void **)data, datalen) == 0
	 ? D_SUCCESS
	 : D_IO_ERROR);
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_read_inband (device_t device,
		       mach_port_t reply_port,
		       mach_msg_type_name_t reply_type,
		       dev_mode_t mode,
		       recnum_t recnum,
		       int bytes_wanted,
		       io_buf_ptr_inband_t data,
		       size_t *datalen)
{
  if (device == pseudo_console)
    {
      int avail;

#if 0
      if (console_send_rights)
	{
	  mach_port_mod_refs (mach_task_self (), pseudo_console,
			      MACH_PORT_TYPE_SEND, -console_send_rights);
	  console_send_rights = 0;
	}
#endif

      pthread_spin_lock (&readlock);
      ioctl (0, FIONREAD, &avail);
      if (avail)
	{
	  *datalen = read (0, data, bytes_wanted);
	  unlock_readlock ();
	  return (*datalen == -1 ? D_IO_ERROR : D_SUCCESS);
	}
      else
	{
	  kern_return_t err;

	  unlock_readlock ();
	  err = queue_read (DEV_READI, reply_port, reply_type, bytes_wanted);
	  if (err)
	    return err;
	  return MIG_NO_REPLY;
	}
    }
  else if (device == pseudo_root)
    {
      error_t err;
      void *returned = data;

      *datalen = bytes_wanted;
      err =
	store_read (root_store, recnum, bytes_wanted, (void **)&returned, datalen);

      if (! err)
	{
	  if (returned != data)
	    {
	      memcpy ((void *)data, returned, *datalen);
	      munmap ((caddr_t) returned, *datalen);
	    }
	  return D_SUCCESS;
	}
      else
	return D_IO_ERROR;
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_map (device_t device,
	       vm_prot_t prot,
	       vm_offset_t offset,
	       vm_size_t size,
	       memory_object_t *pager,
	       int unmap)
{
  if (device == pseudo_console || device == pseudo_root)
    return D_INVALID_OPERATION;
  else if (device == pseudo_time)
    {
      error_t err;
      mach_port_t wr_memobj;
      file_t node = file_name_lookup ("/dev/time", O_RDONLY, 0);

      if (node == MACH_PORT_NULL)
	return D_IO_ERROR;

      err = io_map (node, pager, &wr_memobj);
      if (!err && MACH_PORT_VALID (wr_memobj))
	mach_port_deallocate (mach_task_self (), wr_memobj);

      mach_port_deallocate (mach_task_self (), node);
      return D_SUCCESS;
    }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_set_status (device_t device,
		      dev_flavor_t flavor,
		      dev_status_t status,
		      size_t statuslen)
{
  if (device != pseudo_console && device != pseudo_root)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

kern_return_t
ds_device_get_status (device_t device,
		      dev_flavor_t flavor,
		      dev_status_t status,
		      size_t *statuslen)
{
  if (device == pseudo_console)
    return D_INVALID_OPERATION;
  else if (device == pseudo_root)
    switch (flavor)
      {
      case DEV_GET_SIZE:
        if (*statuslen < DEV_GET_SIZE_COUNT)
          return D_INVALID_SIZE;
        status[DEV_GET_SIZE_DEVICE_SIZE] = root_store->size;
        status[DEV_GET_SIZE_RECORD_SIZE] = root_store->block_size;
        *statuslen = DEV_GET_SIZE_COUNT;
        return D_SUCCESS;

      case DEV_GET_RECORDS:
        if (*statuslen < DEV_GET_RECORDS_COUNT)
          return D_INVALID_SIZE;
        status[DEV_GET_RECORDS_DEVICE_RECORDS] = root_store->blocks;
        status[DEV_GET_RECORDS_RECORD_SIZE] = root_store->block_size;
        *statuslen = DEV_GET_RECORDS_COUNT;
        return D_SUCCESS;

      default:
        return D_INVALID_OPERATION;
      }
  else
    return D_NO_SUCH_DEVICE;
}

kern_return_t
ds_device_set_filter (device_t device,
		      mach_port_t receive_port,
		      int priority,
		      filter_array_t filter,
		      size_t filterlen)
{
  if (device != pseudo_console && device != pseudo_root)
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
  static int no_console;
  mach_port_t foo;
  if (notify == pseudo_master_device_port)
    {
      if (no_console)
	goto bye;
      pseudo_master_device_port = MACH_PORT_NULL;
      return 0;
    }
  if (notify == pseudo_console)
    {
      if (mscount == console_mscount &&
	  pseudo_master_device_port == MACH_PORT_NULL)
	{
	bye:
	  restore_termstate ();
	  write (2, "bye\n", 4);
	  host_exit (0);
	}
      else
	{
	  no_console = (mscount == console_mscount);

	  mach_port_request_notification (mach_task_self (), pseudo_console,
					  MACH_NOTIFY_NO_SENDERS,
					  console_mscount == mscount
					  ? mscount + 1
					  : console_mscount,
					  pseudo_console,
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

static void task_died (mach_port_t name);

kern_return_t
do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t name)
{
#if 0
  if (name == child_task && notify == bootport)
    host_exit (0);
#endif
  if (notify != dead_task_notification_port)
    return EOPNOTSUPP;
  task_died (name);
  mach_port_deallocate (mach_task_self (), name);
  return 0;
}


/* Implementation of the Hurd I/O interface, which
   we support for the console port only. */

kern_return_t
S_io_write (mach_port_t object,
	    mach_port_t reply_port,
	    mach_msg_type_name_t reply_type,
	    data_t data,
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
	   data_t *data,
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

  pthread_spin_lock (&readlock);
  ioctl (0, FIONREAD, &avail);
  if (avail)
    {
      if (amount > *datalen)
	*data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      *datalen = read (0, *data, amount);
      unlock_readlock ();
      return *datalen == -1 ? errno : 0;
    }
  else
    {
      kern_return_t err;
      unlock_readlock ();
      err = queue_read (IO_READ, reply_port, reply_type, amount);
      if (err)
	return err;
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

static kern_return_t
io_select_common (mach_port_t object,
		  mach_port_t reply_port,
		  mach_msg_type_name_t reply_type,
		  struct timespec *tsp, int *type)
{
  struct timeval tv, *tvp;
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

  if (tsp == NULL)
    tvp = NULL;
  else
    {
      tv.tv_sec = tsp->tv_sec;
      tv.tv_usec = tsp->tv_nsec / 1000;
      tvp = &tv;
    }

  n = select (1,
	      (*type & SELECT_READ) ? &r : 0,
	      (*type & SELECT_WRITE) ? &w : 0,
	      (*type & SELECT_URG) ? &x : 0,
	      tvp);
  if (n < 0)
    return errno;

  if (! FD_ISSET (0, &r))
    *type &= ~SELECT_READ;
  if (! FD_ISSET (0, &w))
    *type &= ~SELECT_WRITE;
  if (! FD_ISSET (0, &x))
    *type &= ~SELECT_URG;

  return 0;
}

kern_return_t
S_io_select (mach_port_t object,
	     mach_port_t reply_port,
	     mach_msg_type_name_t reply_type,
	     int *type)
{
  return io_select_common (object, reply_port, reply_type, NULL, type);
}

kern_return_t
S_io_select_timeout (mach_port_t object,
		     mach_port_t reply_port,
		     mach_msg_type_name_t reply_type,
		     struct timespec ts,
		     int *type)
{
  return io_select_common (object, reply_port, reply_type, &ts, type);
}

kern_return_t
S_io_stat (mach_port_t object,
	   mach_port_t reply_port,
	   mach_msg_type_name_t reply_type,
	   struct stat *st)
{
  if (object != pseudo_console)
    return EOPNOTSUPP;

  memset (st, 0, sizeof(struct stat));
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
  size_t gulen = 0, aulen = 0, gglen = 0, aglen = 0;
  error_t err;

  /* XXX: This cannot possibly work, authserver is 0.  */

  err = mach_port_insert_right (mach_task_self (), object, object,
				MACH_MSG_TYPE_MAKE_SEND);
  assert_perror_backtrace (err);

  do
    err = auth_server_authenticate (authserver,
				  rend, MACH_MSG_TYPE_COPY_SEND,
				  object, MACH_MSG_TYPE_COPY_SEND,
				  &gu, &gulen,
				  &au, &aulen,
				  &gg, &gglen,
				  &ag, &aglen);
  while (err == EINTR);

  if (!err)
    {
      mig_deallocate ((vm_address_t) gu, gulen * sizeof *gu);
      mig_deallocate ((vm_address_t) au, aulen * sizeof *gu);
      mig_deallocate ((vm_address_t) gg, gglen * sizeof *gu);
      mig_deallocate ((vm_address_t) au, aulen * sizeof *gu);
    }
  mach_port_deallocate (mach_task_self (), rend);
  mach_port_deallocate (mach_task_self (), object);

  return 0;
}

kern_return_t
S_io_restrict_auth (mach_port_t object,
		    mach_port_t reply_port,
		    mach_msg_type_name_t reply_type,
		    mach_port_t *newobject,
		    mach_msg_type_name_t *newobjtype,
		    uid_t *uids,
		    size_t nuids,
		    uid_t *gids,
		    size_t ngids)
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


kern_return_t
S_io_pathconf (mach_port_t obj,
	       mach_port_t reply_port,
	       mach_msg_type_name_t reply_type,
	       int name, int *value)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_identity (mach_port_t obj,
	       mach_port_t reply,
	       mach_msg_type_name_t replytype,
	       mach_port_t *id,
	       mach_msg_type_name_t *idtype,
	       mach_port_t *fsid,
	       mach_msg_type_name_t *fsidtype,
	       ino_t *fileno)
{
  return EOPNOTSUPP;
}

kern_return_t
S_io_revoke (mach_port_t obj,
	     mach_port_t reply, mach_msg_type_name_t replyPoly)
{
  return EOPNOTSUPP;
}



/* Implementation of the Hurd terminal driver interface, which we only
   support on the console device.  */

kern_return_t
S_termctty_open_terminal (ctty_t object,
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

kern_return_t S_term_get_peername
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

/* Mach host emulation.  */

kern_return_t
S_vm_set_default_memory_manager (mach_port_t host_priv,
                                 mach_port_t *default_manager)
{
  if (host_priv != pseudo_privileged_host_port)
    return KERN_INVALID_HOST;

  if (*default_manager != MACH_PORT_NULL)
    return KERN_INVALID_ARGUMENT;

  *default_manager = MACH_PORT_NULL;
  return KERN_SUCCESS;
}

kern_return_t
S_host_reboot (mach_port_t host_priv,
               int flags)
{
  fprintf (stderr, "Would %s the system.  Bye.\r\n",
           flags & RB_HALT? "halt": "reboot");
  host_exit (0);
}


kern_return_t
S_host_processor_set_priv (mach_port_t host_priv,
			   mach_port_t set_name,
			   mach_port_t *set)
{
  if (host_priv != pseudo_privileged_host_port)
    return KERN_INVALID_HOST;

  *set = pseudo_pset;
  return KERN_SUCCESS;
}

kern_return_t
S_register_new_task_notification (mach_port_t host_priv,
				  mach_port_t notification)
{
  if (host_priv != pseudo_privileged_host_port)
    return KERN_INVALID_HOST;

  if (! MACH_PORT_VALID (notification))
    return KERN_INVALID_ARGUMENT;

  if (MACH_PORT_VALID (new_task_notification))
    return KERN_NO_ACCESS;

  new_task_notification = notification;
  return KERN_SUCCESS;
}


/* Managing tasks.  */

static void
task_ihash_cleanup (hurd_ihash_value_t value, void *cookie)
{
  (void) cookie;
  mach_port_deallocate (mach_task_self (), (mach_port_t) value);
}

static struct hurd_ihash task_ihash =
  HURD_IHASH_INITIALIZER_GKI (HURD_IHASH_NO_LOCP, task_ihash_cleanup, NULL,
                              NULL, NULL);

static void
task_died (mach_port_t name)
{
  if (verbose > 1)
    fprintf (stderr, "Task '%lu' died.\r\n", name);

  hurd_ihash_remove (&task_ihash, (hurd_ihash_key_t) name);
}

/* Handle new task notifications from proc.  */
error_t
S_mach_notify_new_task (mach_port_t notify,
			mach_port_t task,
			mach_port_t parent)
{
  error_t err;
  mach_port_t previous;

  if (notify != task_notification_port)
    return EOPNOTSUPP;

  if (verbose > 1)
    fprintf (stderr, "Task '%lu' created by task '%lu'.\r\n", task, parent);

  err = mach_port_request_notification (mach_task_self (), task,
                                        MACH_NOTIFY_DEAD_NAME, 0,
                                        dead_task_notification_port,
                                        MACH_MSG_TYPE_MAKE_SEND_ONCE,
                                        &previous);
  if (err)
    goto fail;
  assert_backtrace (! MACH_PORT_VALID (previous));

  mach_port_mod_refs (mach_task_self (), task, MACH_PORT_RIGHT_SEND, +1);
  err = hurd_ihash_add (&task_ihash,
                        (hurd_ihash_key_t) task, (hurd_ihash_value_t) task);
  if (err)
    {
      mach_port_deallocate (mach_task_self (), task);
      goto fail;
    }

  if (MACH_PORT_VALID (new_task_notification))
    /* Relay the notification.  This consumes task and parent.  */
    return mach_notify_new_task (new_task_notification, task, parent);

  mach_port_deallocate (mach_task_self (), task);
  mach_port_deallocate (mach_task_self (), parent);
  return 0;

 fail:
  task_terminate (task);
  return err;
}

kern_return_t
S_processor_set_tasks(mach_port_t processor_set,
		      task_array_t *task_list,
		      mach_msg_type_number_t *task_listCnt)
{
  error_t err;
  size_t i;

  if (!task_ihash.nr_items)
    {
      *task_listCnt = 0;
      return 0;
    }

  err = vm_allocate (mach_task_self (), (vm_address_t *) task_list,
		     task_ihash.nr_items * sizeof **task_list, 1);
  if (err)
    return err;

  /* The first task has to be the kernel.  */
  (*task_list)[0] = pseudo_kernel;

  i = 1;
  HURD_IHASH_ITERATE (&task_ihash, value)
    {
      task_t task = (task_t) value;
      if (task == pseudo_kernel)
        continue;

      (*task_list)[i] = task;
      i += 1;
    }

  *task_listCnt = task_ihash.nr_items;
  return 0;
}
