/* Load a task using the single server, and then run it
   as if we were the kernel.
   Copyright (C) 1993,94,95,96,97,98,99,2000,01,02,2006
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
#include <a.out.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <elf.h>
#include <mach/mig_support.h>
#include <mach/default_pager.h>
#include <mach/machine/vm_param.h> /* For VM_XXX_ADDRESS */
#include <argp.h>
#include <hurd/store.h>
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

#include "boot_script.h"

#include <hurd/auth.h>

#ifdef UX
#undef STORE			/* We can't use libstore when under UX.  */
#else
#define STORE
#endif

#ifdef UX

#include "ux.h"

#else  /* !UX */

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <error.h>
#include <hurd.h>
#include <assert.h>

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
#define host_exit exit

#endif /* UX */

mach_port_t privileged_host_port, master_device_port, defpager;
mach_port_t pseudo_master_device_port;
mach_port_t receive_set;
mach_port_t pseudo_console, pseudo_root;
auth_t authserver;

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

char *useropen_dir;

int
useropen (const char *name, int flags, int mode)
{
  if (useropen_dir)
    {
      static int dlen;
      if (!dlen) dlen = strlen (useropen_dir);
      {
	int len = strlen (name);
	char try[dlen + 1 + len + 1];
	int fd;
	memcpy (try, useropen_dir, dlen);
	try[dlen] = '/';
	memcpy (&try[dlen + 1], name, len + 1);
	fd = open (try, flags, mode);
	if (fd >= 0)
	  return fd;
      }
    }
  return open (name, flags, mode);
}

int
request_server (mach_msg_header_t *inp,
		mach_msg_header_t *outp)
{
  extern int io_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int device_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int notify_server (mach_msg_header_t *, mach_msg_header_t *);
  extern int term_server (mach_msg_header_t *, mach_msg_header_t *);
/*  extern int tioctl_server (mach_msg_header_t *, mach_msg_header_t *); */
  extern int bootstrap_server (mach_msg_header_t *, mach_msg_header_t *);
  extern void bootstrap_compat ();

#if 0
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
#endif
   return (io_server (inp, outp)
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
  union
    {
      struct exec a;
      Elf32_Ehdr e;
    } hdr;
  char msg[] = ": cannot open bootstrap file\n";

  fd = useropen (file, O_RDONLY, 0);

  if (fd == -1)
    {
      write (2, file, strlen (file));
      write (2, msg, sizeof msg - 1);
      task_terminate (t);
      host_exit (1);
    }

  read (fd, &hdr, sizeof hdr);
  if (*(Elf32_Word *) hdr.e.e_ident == *(Elf32_Word *) "\177ELF")
    {
      Elf32_Phdr phdrs[hdr.e.e_phnum], *ph;
      lseek (fd, hdr.e.e_phoff, SEEK_SET);
      read (fd, phdrs, sizeof phdrs);
      for (ph = phdrs; ph < &phdrs[sizeof phdrs/sizeof phdrs[0]]; ++ph)
	if (ph->p_type == PT_LOAD)
	  {
	    vm_address_t buf;
	    vm_size_t offs = ph->p_offset & (ph->p_align - 1);
	    vm_size_t bufsz = round_page (ph->p_filesz + offs);

	    buf = (vm_address_t) mmap (0, bufsz,
				       PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);

	    lseek (fd, ph->p_offset, SEEK_SET);
	    read (fd, (void *)(buf + offs), ph->p_filesz);

	    ph->p_memsz = ((ph->p_vaddr + ph->p_memsz + ph->p_align - 1)
			   & ~(ph->p_align - 1));
	    ph->p_vaddr &= ~(ph->p_align - 1);
	    ph->p_memsz -= ph->p_vaddr;

	    vm_allocate (t, (vm_address_t*)&ph->p_vaddr, ph->p_memsz, 0);
	    vm_write (t, ph->p_vaddr, buf, bufsz);
	    munmap ((caddr_t) buf, bufsz);
	    vm_protect (t, ph->p_vaddr, ph->p_memsz, 0,
			((ph->p_flags & PF_R) ? VM_PROT_READ : 0) |
			((ph->p_flags & PF_W) ? VM_PROT_WRITE : 0) |
			((ph->p_flags & PF_X) ? VM_PROT_EXECUTE : 0));
	  }
      return hdr.e.e_entry;
    }
  else
    {
      /* a.out */
      int magic = N_MAGIC (hdr.a);
      int headercruft;
      vm_address_t base = 0x10000;
      int rndamount, amount;
      vm_address_t bsspagestart, bssstart;
      char *buf;

      headercruft = sizeof (struct exec) * (magic == ZMAGIC);

      amount = headercruft + hdr.a.a_text + hdr.a.a_data;
      rndamount = round_page (amount);
      buf = mmap (0, rndamount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      lseek (fd, sizeof hdr.a - headercruft, SEEK_SET);
      read (fd, buf, amount);
      vm_allocate (t, &base, rndamount, 0);
      vm_write (t, base, (vm_address_t) buf, rndamount);
      if (magic != OMAGIC)
	vm_protect (t, base, trunc_page (headercruft + hdr.a.a_text),
		    0, VM_PROT_READ | VM_PROT_EXECUTE);
      munmap ((caddr_t) buf, rndamount);

      bssstart = base + hdr.a.a_text + hdr.a.a_data + headercruft;
      bsspagestart = round_page (bssstart);
      vm_allocate (t, &bsspagestart,
		   hdr.a.a_bss - (bsspagestart - bssstart), 0);

      return hdr.a.a_entry;
    }
}


void read_reply ();
void * msg_thread (void *);

/* Callbacks for boot_script.c; see boot_script.h.  */

mach_port_t
boot_script_read_file (const char *filename)
{
  static const char msg[] = ": cannot open\n";
  int fd = useropen (filename, O_RDONLY, 0);
  host_stat_t st;
  error_t err;
  mach_port_t memobj;
  vm_address_t region;

  write (2, filename, strlen (filename));
  if (fd < 0)
    {
      write (2, msg, sizeof msg - 1);
      host_exit (1);
    }
  else
    write (2, msg + sizeof msg - 2, 1);

  host_fstat (fd, &st);

  err = default_pager_object_create (defpager, &memobj,
				     round_page (st.st_size));
  if (err)
    {
      static const char msg[] = "cannot create default-pager object\n";
      write (2, msg, sizeof msg - 1);
      host_exit (1);
    }

  region = 0;
  vm_map (mach_task_self (), &region, round_page (st.st_size),
	  0, 1, memobj, 0, 0, VM_PROT_ALL, VM_PROT_ALL, VM_INHERIT_NONE);
  read (fd, (char *) region, st.st_size);
  munmap ((caddr_t) region, round_page (st.st_size));

  close (fd);
  return memobj;
}

int
boot_script_exec_cmd (void *hook,
		      mach_port_t task, char *path, int argc,
		      char **argv, char *strings, int stringlen)
{
  char *args, *p;
  int arg_len, i;
  size_t reg_size;
  void *arg_pos;
  vm_offset_t stack_start, stack_end;
  vm_address_t startpc, str_start;
  thread_t thread;

  write (2, path, strlen (path));
  for (i = 1; i < argc; ++i)
    {
      write (2, " ", 1);
      write (2, argv[i], strlen (argv[i]));
    }
  write (2, "\r\n", 2);

  startpc = load_image (task, path);
  arg_len = stringlen + (argc + 2) * sizeof (char *) + sizeof (integer_t);
  arg_len += 5 * sizeof (int);
  stack_end = VM_MAX_ADDRESS;
  stack_start = VM_MAX_ADDRESS - 16 * 1024 * 1024;
  vm_allocate (task, &stack_start, stack_end - stack_start, FALSE);
  arg_pos = (void *) ((stack_end - arg_len) & ~(sizeof (natural_t) - 1));
  args = mmap (0, stack_end - trunc_page ((vm_offset_t) arg_pos),
	       PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  str_start = ((vm_address_t) arg_pos
	       + (argc + 2) * sizeof (char *) + sizeof (integer_t));
  p = args + ((vm_address_t) arg_pos & (vm_page_size - 1));
  *(int *) p = argc;
  p = (void *) p + sizeof (int);
  for (i = 0; i < argc; i++)
    {
      *(char **) p = argv[i] - strings + (char *) str_start;
      p = (void *) p + sizeof (char *);
    }
  *(char **) p = 0;
  p = (void *) p + sizeof (char *);
  *(char **) p = 0;
  p = (void *) p + sizeof (char *);
  memcpy (p, strings, stringlen);
  bzero (args, (vm_offset_t) arg_pos & (vm_page_size - 1));
  vm_write (task, trunc_page ((vm_offset_t) arg_pos), (vm_address_t) args,
	    stack_end - trunc_page ((vm_offset_t) arg_pos));
  munmap ((caddr_t) args,
	  stack_end - trunc_page ((vm_offset_t) arg_pos));

  thread_create (task, &thread);
#ifdef i386_THREAD_STATE_COUNT
  {
    struct i386_thread_state regs;
    reg_size = i386_THREAD_STATE_COUNT;
    thread_get_state (thread, i386_THREAD_STATE,
		      (thread_state_t) &regs, &reg_size);
    regs.eip = (int) startpc;
    regs.uesp = (int) arg_pos;
    thread_set_state (thread, i386_THREAD_STATE,
		      (thread_state_t) &regs, reg_size);
  }
#elif defined(ALPHA_THREAD_STATE_COUNT)
  {
    struct alpha_thread_state regs;
    reg_size = ALPHA_THREAD_STATE_COUNT;
    thread_get_state (thread, ALPHA_THREAD_STATE,
		      (thread_state_t) &regs, &reg_size);
    regs.r30 = (natural_t) arg_pos;
    regs.pc = (natural_t) startpc;
    thread_set_state (thread, ALPHA_THREAD_STATE,
		      (thread_state_t) &regs, reg_size);
  }
#else
# error needs to be ported
#endif

  thread_resume (thread);
  mach_port_deallocate (mach_task_self (), thread);
  return 0;
}

const char *argp_program_version = STANDARD_HURD_VERSION (boot);

static struct argp_option options[] =
{
  { "boot-root",   'D', "DIR", 0,
    "Root of a directory tree in which to find files specified in BOOT-SCRIPT" },
  { "single-user", 's', 0, 0,
    "Boot in single user mode" },
  { "kernel-command-line", 'c', "COMMAND LINE", 0,
    "Simulated multiboot command line to supply" },
  { "pause" ,      'd', 0, 0,
    "Pause for user confirmation at various times during booting" },
  { "isig",      'I', 0, 0,
    "Do not disable terminal signals, so you can suspend and interrupt boot."},
  { "device",	   'f', "device_name=device_file", 0,
    "Specify a device file used by subhurd and its virtual name."},
  { 0 }
};
static char args_doc[] = "BOOT-SCRIPT";
static char doc[] = "Boot a second hurd";

struct dev_map 
{
  char *name;
  mach_port_t port;
  struct dev_map *next;
};

static struct dev_map *dev_map_head;

static struct dev_map *add_dev_map (char *dev_name, char *dev_file)
{
  struct dev_map *map = malloc (sizeof (*map));

  assert (map);
  map->name = dev_name;
  map->port = file_name_lookup (dev_file, 0, 0);
  if (map->port == MACH_PORT_NULL)
    error (1, errno, "file_name_lookup: %s", dev_file);
  map->next = dev_map_head;
  dev_map_head = map;
  return map;
}

static struct dev_map *lookup_dev (char *dev_name)
{
  struct dev_map *map;

  for (map = dev_map_head; map; map = map->next)
    {
      if (strcmp (map->name, dev_name) == 0)
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

    case ARGP_KEY_ARG:
      if (state->arg_num == 0)
	bootscript = arg;
      else
	return ARGP_ERR_UNKNOWN;
      break;

    case ARGP_KEY_INIT:
      state->child_inputs[0] = state->input; break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

int
main (int argc, char **argv, char **envp)
{
  error_t err;
  mach_port_t foo;
  char *buf = 0;
  int i, len;
  pthread_t pthread_id;
  char *root_store_name;
  const struct argp_child kids[] = { { &store_argp }, { 0 }};
  struct argp argp = { options, parse_opt, args_doc, doc, kids };
  struct store_argp_params store_argp_params = { 0 };

  argp_parse (&argp, argc, argv, 0, 0, &store_argp_params);
  err = store_parsed_name (store_argp_params.result, &root_store_name);
  if (err)
    error (2, err, "store_parsed_name");

  err = store_parsed_open (store_argp_params.result, 0, &root_store);
  if (err)
    error (4, err, "%s", root_store_name);

  get_privileged_ports (&privileged_host_port, &master_device_port);

  defpager = MACH_PORT_NULL;
  vm_set_default_memory_manager (privileged_host_port, &defpager);

  strcat (bootstrap_args, "f");

  mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_PORT_SET,
		      &receive_set);

  if (root_store->class == &store_device_class && root_store->name
      && (root_store->flags & STORE_ENFORCED)
      && root_store->num_runs == 1 && root_store->runs[0].start == 0)
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

  if (kernel_command_line == 0)
    asprintf (&kernel_command_line, "%s %s root=%s",
	      argv[0], bootstrap_args, bootdevice);

  /* Initialize boot script variables.  */
  if (boot_script_set_variable ("host-port", VAL_PORT,
				(int) privileged_host_port)
      || boot_script_set_variable ("device-port", VAL_PORT,
				   (integer_t) pseudo_master_device_port)
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
           assert (msg);
           write (2, msg, strlen (msg));
           free (msg);
           host_exit (1);
         }
      }
  }

  /* Parse the boot script.  */
  {
    char *p, *line;
    static const char filemsg[] = "Can't open boot script\n";
    static const char memmsg[] = "Not enough memory\n";
    int amt, fd, err;

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
    int err;

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
      else /* We hosed */
	error (5, errno, "select");
    }

/*  mach_msg_server (request_server, __vm_page_size * 2, receive_set); */
}

void *
msg_thread (void *arg)
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
      *devicetype = MACH_MSG_TYPE_MOVE_SEND;
      return device_open (map->port, mode, "", device);
    }

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
	      bcopy (returned, (void *)data, *datalen);
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
ds_xxx_device_set_status (device_t device,
			  dev_flavor_t flavor,
			  dev_status_t status,
			  size_t statu_cnt)
{
  if (device != pseudo_console)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

kern_return_t
ds_xxx_device_get_status (device_t device,
			  dev_flavor_t flavor,
			  dev_status_t status,
			  size_t *statuscnt)
{
  if (device != pseudo_console && device != pseudo_root)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
}

kern_return_t
ds_xxx_device_set_filter (device_t device,
			  mach_port_t rec,
			  int pri,
			  filter_array_t filt,
			  size_t len)
{
  if (device != pseudo_console && device != pseudo_root)
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
  if (device != pseudo_console && device != pseudo_root)
    return D_NO_SUCH_DEVICE;
  return D_INVALID_OPERATION;
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
    if (flavor == DEV_GET_SIZE)
      if (*statuslen < DEV_GET_SIZE_COUNT)
	return D_INVALID_SIZE;
      else
	{
	  status[DEV_GET_SIZE_DEVICE_SIZE] = root_store->size;
	  status[DEV_GET_SIZE_RECORD_SIZE] = root_store->block_size;
	  *statuslen = DEV_GET_SIZE_COUNT;
	  return D_SUCCESS;
	}
    else
      return D_INVALID_OPERATION;
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

kern_return_t
do_mach_notify_dead_name (mach_port_t notify,
			  mach_port_t name)
{
#if 0
  if (name == child_task && notify == bootport)
    host_exit (0);
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
  size_t gulen = 0, aulen = 0, gglen = 0, aglen = 0;
  error_t err;

  err = mach_port_insert_right (mach_task_self (), object, object,
				MACH_MSG_TYPE_MAKE_SEND);
  assert_perror (err);

  if (! auth_server_authenticate (authserver,
				  rend, MACH_MSG_TYPE_COPY_SEND,
				  object, MACH_MSG_TYPE_COPY_SEND,
				  &gu, &gulen,
				  &au, &aulen,
				  &gg, &gglen,
				  &ag, &aglen))
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
