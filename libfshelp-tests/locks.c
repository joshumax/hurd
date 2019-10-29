/* Test record locking.

   Copyright (C) 2001, 2018-2019 Free Software Foundation, Inc.

   Written by Neal H Walfield <neal@cs.uml.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#include "../libfshelp/fshelp.h"
#include "../libfshelp/rlock.h"
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <string.h>

#include "fs_U.h"

#ifndef PEROPENS
#define PEROPENS 10
#endif

struct rlock_box box;
struct rlock_peropen peropens[PEROPENS];
loff_t pointers[PEROPENS];
loff_t file_size;

struct command
{
  char *name;
  int (*func)(char *cmds);
  char *doc;
};

error_t cmd_help (char *);
error_t cmd_comment (char *);
error_t cmd_echo (char *);
error_t cmd_lock (char *);
error_t cmd_list (char *);
error_t cmd_seek (char *);
error_t cmd_exec (char *);

struct command commands [] =
  {
    { "help", cmd_help, "Print this screen" },
    { "#", cmd_comment, "Comment (Must _start_ the line)." },
    { "echo", cmd_echo, "Echo the line." },
    { "lock", cmd_lock,
      "po start length type\n"
      "\ttype = { F_UNLCK=0, F_RDLCK,=1, F_WRLCK=2 }" },
    { "list", cmd_list, "list all locks' status" },
    { "seek", cmd_seek, "PO1 ... Print the position of the given po.\n"
      "\tPO1=N ... Seek a given po." },
    { "exec", cmd_exec, "Execute a built in echoing the command."}
  };

error_t
cmd_help (char *args)
{
  int i;
  printf ("Commands:\n");
  for (i = 0; i < sizeof (commands) / sizeof (struct command); i ++)
    printf ("%s\t%s\n", commands[i].name, commands[i].doc);
  return 0;
}

error_t
cmd_comment (char *args)
{
  return 0;
}

error_t
cmd_echo (char *args)
{
  printf ("%s", args);
  return 0;
}

error_t
cmd_lock (char *args)
{
  int po, type;
  loff_t start, len;
  struct flock64 lock;
  mach_port_t rendezvous = MACH_PORT_NULL;
  error_t err;

  if (4 != sscanf (args, "%d %ld %ld %d", &po, (long*)&start, (long*)&len, &type))
    {
      printf ("Syntax error.\n");
      return 0;
    }

  lock.l_type = type;
  lock.l_whence = SEEK_CUR;
  lock.l_start = (long)start;
  lock.l_len = (long)len;

  if (po < 0 || po >= PEROPENS)
    {
      printf ("Unknown peropen: %d.\n", po);
      return 0;
    }

  switch (type)
    {
    case 0: lock.l_type = F_UNLCK; break;
    case 1: lock.l_type = F_RDLCK; break;
    case 2: lock.l_type = F_WRLCK; break;
    default: printf ("Unknown type.\n"); return 0;
    }

  err= fshelp_rlock_tweak (&box, NULL, &peropens[po], O_RDWR,
			   file_size, pointers[po], F_SETLK64,
			   &lock, rendezvous);
  if (! err)
    {
      char buf[10];
      sprintf (buf, "%d\n", po);
      cmd_list (buf);
    }
  return err;
}

error_t
cmd_list (char *args)
{
  char *end;

  void dump (int i)
    {
      struct rlock_list *l;

      printf ("%3d:", i);
      for (l = *peropens[i].locks; l; l = l->po.next)
        {
	  printf ("\tStart = %4ld; Length = %4ld; Type = ", (long)l->start, (long)l->len);
	  switch (l->type)
	    {
	    case F_RDLCK: printf ("F_RDLCK"); break;
	    case F_WRLCK: printf ("F_WRLCK"); break;
	    case F_UNLCK: printf ("F_UNLCK"); break;
	    default: printf ("UNKNOWN"); break;
	    }
	  printf ("\n");
	}

      if (*peropens[i].locks == NULL)
	printf ("\n");
    }

  while (*args == ' ')
    args ++;

  if (*args == '\n' || *args == '\0')
    {
      int i;

      for (i = 0; i < PEROPENS; i ++)
	dump (i);
      return 0;
    }

  while (1)
    {
      long int p = strtoll (args, &end, 0);
      if (end == args)
        {
	  printf ("Syntax error.\n");
	  return 0;
	}

      if (p < 0 || p > PEROPENS)
	printf ("%3ld:\tOut of range.", p);
      else
	dump (p);

      while (*end == ' ')
	end ++;

      if (*end == '\n' || *end == '\0')
	return 0;
      args = end;
    }
}

error_t
cmd_seek (char *args)
{
  char *end;
  int p;

  while (*args == ' ')
    args ++;

  if (*args == '\n' || *args == '\0')
    {
      int i;
      for (i = 0; i < PEROPENS; i ++)
        printf ("%3d: %ld\n", i, (long)pointers[i]);
      return 0;
    }

  while (1)
    {
      int set = 0;
      long seek_to = 0;

      p = strtol (args, &end, 0);
      if (end == args)
        {
	  printf ("Syntax error.\n");
	  return 0;
	}

      if (*end == '=')
        {
	  set = 1;
	  args = end + 1;
	  seek_to = strtol (args, &end, 0);
	  if (end == args)
	    {
	      printf ("Syntax error.\n");
	      return 0;
	    }
	}

      if (p < 0 || p > PEROPENS)
	printf ("%3d: unknown peropen\n", p);
      else
        {
          printf ("%3d: %ld", p, (long)pointers[p]);
	  if (set)
	    printf (" => %ld\n", (long)(pointers[p] = seek_to));
	  else
	    printf ("\n");
	}

      while (*end == ' ')
	end ++;
      if (*end == '\0' || *end == '\n')
        return 0;
      args = end;
    }
}

error_t
interpret (char *buffer)
{
  int i;

  while (*buffer == ' ')
    buffer ++;

  if (*buffer == '\n')
    return 0;

  for (i = 0; i < sizeof (commands) / sizeof (struct command); i ++)
  if (strncmp (commands[i].name, buffer, strlen (commands[i].name)) == 0)
    {
      error_t err;
      err = commands[i].func (buffer + strlen (commands[i].name) + 1);
      if (err)
	printf ("%s\n", strerror (err));
      return err;
    }

  printf ("Unknown command.\n");
  return 0;
}

error_t
cmd_exec (char *arg)
{
  printf ("%s", arg);
  interpret (arg);
  return 0;
}

int main (int argc, char *argv[])
{
  int i;

  if (argc != 1)
    {
      printf ("Usage: %s\n"
	      "\tType `help' at the prompt.\n"
	      "\tUsed to test the record locking functions in libfshelp\n",
	      argv[0]);
      return 1;
    }

  fshelp_rlock_init (&box);
  for (i = 0; i < PEROPENS; i ++)
    fshelp_rlock_po_init (&peropens[i]);

  while (! feof (stdin))
    {
      char b[1024];

      printf ("> ");
      fflush (stdout);

      if (! fgets (b, sizeof (b), stdin))
	{
	  if (feof (stdin))
	    break;
	  else
	    continue;
	}

      interpret (b);
    }

  printf ("\n");
  return 0;
}
