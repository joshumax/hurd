/* Print task vm information

   Copyright (C) 1996,97,98,2002 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include <stdio.h>
#include <stddef.h>
#include <argp.h>
#include <error.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <version.h>

#include <mach.h>
#include <mach/vm_statistics.h>
#include <mach/default_pager.h>
#include <hurd.h>

const char *argp_program_version = STANDARD_HURD_VERSION (vminfo);

static const struct argp_option options[] = {
  {"verbose",	'v', 0, 0, "Give more detailed information"},
  {"addresses", 'a', 0, 0, "Print region start addresses"},
  {"sizes",     's', 0, 0, "Print region sizes"},
  {"decimal",	'd', 0, 0, "Show number is decimal"},
  {"holes",	'h', 0, 0, "Show holes between regions explicitly"},
  {0}
};
static const char *args_doc = "PID [ADDR [SIZE]]]";
static const char *doc = "Show virtual memory regions for process PID"
"\vIf ADDR, and possibly SIZE, are given only regions enclosing the range"
" ADDR to ADDR+SIZE are shown (SIZE defaults to 0)."
"\nIf neither --addresses nor --sizes is specified, both are assumed.";

/* Possible things to show about regions.  */
#define W_ADDRS		0x1
#define W_SIZES		0x2
#define W_DETAILS	0x4

static char *
prot_rep (vm_prot_t prot)
{
  if (prot == 0)
    return "0";
  else
    {
      static char buf[20];
      char *p = buf;
      if (prot & VM_PROT_READ)
	*p++ = 'R';
      if (prot & VM_PROT_WRITE)
	*p++ = 'W';
      if (prot & VM_PROT_EXECUTE)
	*p++ = 'X';
      if (prot & ~VM_PROT_ALL)
	sprintf (p, "+%#x", (prot & ~VM_PROT_ALL));
      else
	*p = '\0';
      return buf;
    }
}

static char *
inh_rep (vm_inherit_t inh)
{
  static char buf[20];
  switch (inh)
    {
    case VM_INHERIT_SHARE: return "share";
    case VM_INHERIT_COPY: return "copy";
    case VM_INHERIT_NONE: return "none";
    default:
      sprintf (buf, "%d", inh);
      return buf;
    }
}

static unsigned
parse_num (char *arg, unsigned base, struct argp_state *state, char *what)
{
  char *arg_end;
  unsigned long num = strtoul (arg, &arg_end, base);
  if (*arg == '\0' || *arg_end != '\0')
    argp_error (state, "%s: Invalid %s", arg, what);
  return num;
}

int
main (int argc, char **argv)
{
  error_t err;
  int what = 0, hex = 1, holes = 0;
  vm_offset_t addr = 0, max_addr = ~addr;
  task_t task;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	  pid_t pid;
	  process_t proc;

	case 'a': what |= W_ADDRS; break;
	case 's': what |= W_SIZES; break;
	case 'v': what |= W_DETAILS; break;
	case 'd': hex = 0; break;
	case 'h': holes = 1; break;

	case ARGP_KEY_ARG:
	  switch (state->arg_num)
	    {
	    case 0:		/* PID */
	      pid = parse_num (arg, 10, state, "PID");
	      proc = getproc ();
	      err = proc_pid2task (proc, pid, &task);
	      if (err)
		argp_failure (state, 11, err, "%s", arg);
	      break;
	    case 1:		/* ADDR */
	      addr = max_addr = parse_num (arg, 0, state, "address"); break;
	    case 2:		/* SIZE */
	      max_addr = addr + parse_num (arg, 0, state, "size"); break;
	    default:
	      argp_usage (state);
	    }
	  break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  const struct argp argp = { options, parse_opt, args_doc, doc };

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  if ((what & ~W_DETAILS) == 0)
    what = W_ADDRS | W_SIZES | W_DETAILS;

  while (addr <= max_addr)
    {
      vm_size_t size;
      vm_prot_t prot, max_prot;
      mach_port_t obj;
      vm_offset_t offs;
      vm_inherit_t inh;
      int shared;
      vm_offset_t hole_addr = addr;

      err =
	vm_region (task, &addr, &size, &prot, &max_prot, &inh, &shared,
		   &obj, &offs);
      if (err)
	{
	  if (err != EKERN_NO_SPACE)
	    error (12, err, "vm_region");
	  break;
	}

      if (holes && hole_addr != addr)
	{
	  if ((what & (W_ADDRS|W_SIZES)) == (W_ADDRS|W_SIZES))
	    {
	      if (hex)
		printf ("          [%#lx] (hole)\n", addr - hole_addr);
	      else
		printf ("          [%lu] (hole)\n", addr - hole_addr);
	    }
	  else if ((what & (W_ADDRS|W_SIZES)) == W_SIZES)
	    {
	      if (hex)
		printf ("%#10lx (hole)\n", addr - hole_addr);
	      else
		printf ("%10lu (hole)\n", addr - hole_addr);
	    }
	}

      if ((what & (W_ADDRS|W_SIZES)) == (W_ADDRS|W_SIZES))
	if (hex)
	  printf ("%#10lx[%#zx]", addr, size);
	else
	  printf ("%10lu[%zd]", addr, size);
      else if ((what & (W_ADDRS|W_SIZES)) == W_ADDRS)
	if (hex)
	  printf ("%#10lx", addr);
	else
	  printf ("%10lu", addr);
      else if ((what & (W_ADDRS|W_SIZES)) == W_SIZES)
	{
	  if (hex)
	    printf ("%#10zx", size);
	  else
	    printf ("%10zu", size);
	}
      if (what & W_DETAILS)
	{
	  printf (" (prot=%s", prot_rep (prot));
	  if (max_prot != prot)
	    printf (", max_prot=%s", prot_rep (max_prot));
	  if (inh != VM_INHERIT_DEFAULT)
	    printf (", inherit=%s", inh_rep (inh));
	  if (shared)
	    printf (", shared");
	  if (obj != MACH_PORT_NULL)
	    printf (", mem_obj=%lu", obj);
	  if (offs != 0)
	    {
	      if (hex)
		printf (", offs=%#lx", offs);
	      else
		printf (", offs=%lu", offs);
	    }
	  putchar (')');
	}
      putchar ('\n');

      addr += size;
    }

  exit (0);
}
