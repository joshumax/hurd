/* boot_script.c support functions for running in a Mach user task.
   Copyright (C) 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <a.out.h>
#include <elf.h>
#include <fcntl.h>
#include <mach.h>
#include <mach/machine/vm_param.h> /* For VM_XXX_ADDRESS */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <error.h>

#include "boot_script.h"
#include "private.h"

void *
boot_script_malloc (unsigned int size)
{
  return malloc (size);
}

void
boot_script_free (void *ptr, unsigned int size)
{
  free (ptr);
}


int
boot_script_task_create (struct cmd *cmd)
{
  error_t err;

  if (verbose)
    fprintf (stderr, "Creating task '%s'.\r\n", cmd->path);

  err = task_create (mach_task_self (), 0, &cmd->task);
  if (err)
    {
      error (0, err, "%s: task_create", cmd->path);
      return BOOT_SCRIPT_MACH_ERROR;
    }
  err = task_suspend (cmd->task);
  if (err)
    {
      error (0, err, "%s: task_resume", cmd->path);
      return BOOT_SCRIPT_MACH_ERROR;
    }
  return 0;
}

int
boot_script_task_resume (struct cmd *cmd)
{
  error_t err;

  if (verbose)
    fprintf (stderr, "Resuming task '%s'.\r\n", cmd->path);

  err = task_resume (cmd->task);
  if (err)
    {
      error (0, err, "%s: task_resume", cmd->path);
      return BOOT_SCRIPT_MACH_ERROR;
    }
  return 0;
}

int
boot_script_prompt_task_resume (struct cmd *cmd)
{
  char xx[5];

  printf ("Hit return to resume %s...", cmd->path);
  fgets (xx, sizeof xx, stdin);

  return boot_script_task_resume (cmd);
}

void
boot_script_free_task (task_t task, int aborting)
{
  if (aborting)
    task_terminate (task);
  else
    mach_port_deallocate (mach_task_self (), task);
}

int
boot_script_insert_right (struct cmd *cmd, mach_port_t port, mach_port_t *name)
{
  error_t err;

  *name = MACH_PORT_NULL;
  do
    {
      *name += 1;
      err = mach_port_insert_right (cmd->task,
                                    *name, port, MACH_MSG_TYPE_COPY_SEND);
    }
  while (err == KERN_NAME_EXISTS);

  if (err)
    {
      error (0, err, "%s: mach_port_insert_right", cmd->path);
      return BOOT_SCRIPT_MACH_ERROR;
    }

  return 0;
}

int
boot_script_insert_task_port (struct cmd *cmd, task_t task, mach_port_t *name)
{
  return boot_script_insert_right (cmd, task, name);
}

char *useropen_dir;

static int
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

static vm_address_t
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
      exit (1);
    }

  read (fd, &hdr, sizeof hdr);
  /* File must have magic ELF number.  */
  if (hdr.e.e_ident[0] == 0177 && hdr.e.e_ident[1] == 'E' &&
      hdr.e.e_ident[2] == 'L' && hdr.e.e_ident[3] == 'F')
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
      int quote = !! index (argv[i], ' ') || !! index (argv[i], '\t');
      write (2, " ", 1);
      if (quote)
        write (2, "\"", 1);
      write (2, argv[i], strlen (argv[i]));
      if (quote)
        write (2, "\"", 1);
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
  memset (args, 0, (vm_offset_t)arg_pos & (vm_page_size - 1));
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
