/* Boot script parser for Mach.  */

/* Written by Shantanu Goel (goel@cs.columbia.edu).  */

#include <mach/mach_types.h>
#if !KERNEL || OSKIT_MACH
#include <string.h>
#endif
#include "boot_script.h"


/* This structure describes a symbol.  */
struct sym
{
  /* Symbol name.  */
  const char *name;

  /* Type of value returned by function.  */
  int type;

  /* Symbol value.  */
  integer_t val;

  /* For function symbols; type of value returned by function.  */
  int ret_type;

  /* For function symbols; if set, execute function at the time
     of command execution, not during parsing.  A function with
     this field set must also have `no_arg' set.  Also, the function's
     `val' argument will always be NULL.  */
  int run_on_exec;
};

/* Additional values symbols can take.
   These are only used internally.  */
#define VAL_SYM		10	/* symbol table entry */
#define VAL_FUNC	11	/* function pointer */

/* This structure describes an argument.  */
struct arg
{
  /* Argument text copied verbatim.  0 if none.  */
  char *text;

  /* Type of value assigned.  0 if none.  */
  int type;

  /* Argument value.  */
  integer_t val;
};

/* List of commands.  */
static struct cmd **cmds = 0;

/* Amount allocated for `cmds'.  */
static int cmds_alloc = 0;

/* Next available slot in `cmds'.  */
static int cmds_index = 0;

/* Symbol table.  */
static struct sym **symtab = 0;

/* Amount allocated for `symtab'.  */
static int symtab_alloc = 0;

/* Next available slot in `symtab'.  */
static int symtab_index = 0;

/* Create a task and suspend it.  */
static int
create_task (struct cmd *cmd, int *val)
{
  int err = boot_script_task_create (cmd);
  *val = (int) cmd->task;
  return err;
}

/* Resume a task.  */
static int
resume_task (struct cmd *cmd, int *val)
{
  return boot_script_task_resume (cmd);
}

/* Resume a task when the user hits return.  */
static int
prompt_resume_task (struct cmd *cmd, int *val)
{
  return boot_script_prompt_task_resume (cmd);
}

/* List of builtin symbols.  */
static struct sym builtin_symbols[] =
{
  { "task-create", VAL_FUNC, (integer_t) create_task, VAL_TASK, 0 },
  { "task-resume", VAL_FUNC, (integer_t) resume_task, VAL_NONE, 1 },
  { "prompt-task-resume",
    VAL_FUNC, (integer_t) prompt_resume_task, VAL_NONE, 1 },
};
#define NUM_BUILTIN (sizeof (builtin_symbols) / sizeof (builtin_symbols[0]))

/* Free CMD and all storage associated with it.
   If ABORTING is set, terminate the task associated with CMD,
   otherwise just deallocate the send right.  */
static void
free_cmd (struct cmd *cmd, int aborting)
{
  if (cmd->task)
    boot_script_free_task (cmd->task, aborting);
  if (cmd->args)
    {
      int i;
      for (i = 0; i < cmd->args_index; i++)
	boot_script_free (cmd->args[i], sizeof *cmd->args[i]);
      boot_script_free (cmd->args, sizeof cmd->args[0] * cmd->args_alloc);
    }
  if (cmd->exec_funcs)
    boot_script_free (cmd->exec_funcs,
		      sizeof cmd->exec_funcs[0] * cmd->exec_funcs_alloc);
  boot_script_free (cmd, sizeof *cmd);
}

/* Free all storage allocated by the parser.
   If ABORTING is set, terminate all tasks.  */
static void
cleanup (int aborting)
{
  int i;

  for (i = 0; i < cmds_index; i++)
    free_cmd (cmds[i], aborting);
  boot_script_free (cmds, sizeof cmds[0] * cmds_alloc);
  cmds = 0;
  cmds_index = cmds_alloc = 0;

  for (i = 0; i < symtab_index; i++)
    boot_script_free (symtab[i], sizeof *symtab[i]);
  boot_script_free (symtab, sizeof symtab[0] * symtab_alloc);
  symtab = 0;
  symtab_index = symtab_alloc = 0;
}

/* Add PTR to the list of pointers PTR_LIST, which
   currently has ALLOC amount of space allocated to it, and
   whose next available slot is INDEX.  If more space
   needs to to allocated, INCR is the amount by which
   to increase it.  Return 0 on success, non-zero otherwise.  */
static int
add_list (void *ptr, void ***ptr_list, int *alloc, int *index, int incr)
{
  if (*index == *alloc)
    {
      void **p;

      *alloc += incr;
      p = boot_script_malloc (*alloc * sizeof (void *));
      if (! p)
	{
	  *alloc -= incr;
	  return 1;
	}
      if (*ptr_list)
	{
	  memcpy (p, *ptr_list, *index * sizeof (void *));
	  boot_script_free (*ptr_list, (*alloc - incr) * sizeof (void *));
	}
      *ptr_list = p;
    }
  *(*ptr_list + *index) = ptr;
  *index += 1;
  return 0;
}

/* Create an argument with TEXT, value type TYPE, and value VAL.
   Add the argument to the argument list of CMD.  */
static struct arg *
add_arg (struct cmd *cmd, const char *text, int textlen, int type, int val)
{
  struct arg *arg;

  arg = boot_script_malloc (sizeof (struct arg) + textlen);
  if (arg)
    {
      arg->text = text == 0 ? 0 : memcpy (arg + 1, text, textlen);
      arg->type = type;
      arg->val = val;
      if (add_list (arg, (void ***) &cmd->args,
		    &cmd->args_alloc, &cmd->args_index, 5))
	{
	  boot_script_free (arg, sizeof *arg);
	  return 0;
	}
    }
  return arg;
}

/* Search for the symbol NAME in the symbol table.  */
static struct sym *
sym_lookup (const char *name)
{
  int i;

  for (i = 0; i < symtab_index; i++)
    if (! strcmp (name, symtab[i]->name))
      return symtab[i];
  return 0;
}

/* Create an entry for symbol NAME in the symbol table.  */
static struct sym *
sym_enter (const char *name)
{
  struct sym *sym;

  sym = boot_script_malloc (sizeof (struct sym));
  if (sym)
    {
      memset (sym, 0, sizeof (struct sym));
      sym->name = name;
      if (add_list (sym, (void ***) &symtab, &symtab_alloc, &symtab_index, 20))
	{
	  boot_script_free (sym, sizeof *sym);
	  return 0;
	}
    }
  return sym;
}

/* Parse the command line CMDLINE.  */
int
boot_script_parse_line (void *hook, char *cmdline)
{
  char *p, *q;
  int error;
  struct cmd *cmd;
  struct arg *arg;

  /* Extract command name.  Ignore line if it lacks a command.  */
  for (p = cmdline; *p == ' ' || *p == '\t'; p++)
    ;
  if (*p == '#')
    /* Ignore comment line.  */
    return 0;

#if 0
  if (*p && *p != ' ' && *p != '\t' && *p != '\n')
    {
      printf ("(bootstrap): %s\n", cmdline);
    }
#endif

  for (q = p; *q && *q != ' ' && *q != '\t' && *q != '\n'; q++)
    ;
  if (p == q)
      return 0;

  *q++ = '\0';

  /* Allocate a command structure.  */
  cmd = boot_script_malloc (sizeof (struct cmd) + (q - p));
  if (! cmd)
    return BOOT_SCRIPT_NOMEM;
  memset (cmd, 0, sizeof (struct cmd));
  cmd->hook = hook;
  cmd->path = memcpy (cmd + 1, p, q - p);
  p = q;

  for (arg = 0;;)
    {
      if (! arg)
	{
	  /* Skip whitespace.  */
	  while (*p == ' ' || *p == '\t')
	    p++;

	  /* End of command line.  */
	  if (! *p || *p == '\n')
	    {
	      /* Add command to list.  */
	      if (add_list (cmd, (void ***) &cmds,
			    &cmds_alloc, &cmds_index, 10))
		{
		  error = BOOT_SCRIPT_NOMEM;
		  goto bad;
		}
	      return 0;
	    }
	}

      /* Look for a symbol.  */
      if (arg || (*p == '$' && (*(p + 1) == '{' || *(p + 1) == '(')))
	{
	  char end_char = (*(p + 1) == '{') ? '}' : ')';
	  struct sym *sym = 0;

	  for (p += 2;;)
	    {
	      char c;
	      int i, type;
	      integer_t val;
	      struct sym *s;

	      /* Parse symbol name.  */
	      for (q = p; *q && *q != '\n' && *q != end_char && *q != '='; q++)
		;
	      if (p == q || ! *q || *q == '\n'
		  || (end_char == '}' && *q != '}'))
		{
		  error = BOOT_SCRIPT_SYNTAX_ERROR;
		  goto bad;
		}
	      c = *q;
	      *q = '\0';

	      /* See if this is a builtin symbol.  */
	      for (i = 0; i < NUM_BUILTIN; i++)
		if (! strcmp (p, builtin_symbols[i].name))
		  break;

	      if (i < NUM_BUILTIN)
		s = &builtin_symbols[i];
	      else
		{
		  /* Look up symbol in symbol table.
		     If no entry exists, create one.  */
		  s = sym_lookup (p);
		  if (! s)
		    {
		      s = sym_enter (p);
		      if (! s)
			{
			  error = BOOT_SCRIPT_NOMEM;
			  goto bad;
			}
		    }
		}

	      /* Only values are allowed in ${...} constructs.  */
	      if (end_char == '}' && s->type == VAL_FUNC)
		return BOOT_SCRIPT_INVALID_SYM;

	      /* Check that assignment is valid.  */
	      if (c == '=' && s->type == VAL_FUNC)
		{
		  error = BOOT_SCRIPT_INVALID_ASG;
		  goto bad;
		}

	      /* For function symbols, execute the function.  */
	      if (s->type == VAL_FUNC)
		{
		  if (! s->run_on_exec)
		    {
		      (error
		       = ((*((int (*) (struct cmd *, integer_t *)) s->val))
			  (cmd, &val)));
		      if (error)
			goto bad;
		      type = s->ret_type;
		    }
		  else
		    {
		      if (add_list (s, (void ***) &cmd->exec_funcs,
				    &cmd->exec_funcs_alloc,
				    &cmd->exec_funcs_index, 5))
			{
			  error = BOOT_SCRIPT_NOMEM;
			  goto bad;
			}
		      type = VAL_NONE;
		      goto out;
		    }
		}
	      else if (s->type == VAL_NONE)
		{
		  type = VAL_SYM;
		  val = (integer_t) s;
 		}
	      else
		{
		  type = s->type;
		  val = s->val;
		}

	      if (sym)
		{
		  sym->type = type;
		  sym->val = val;
		}
	      else if (arg)
		{
		  arg->type = type;
		  arg->val = val;
		}

	    out:
	      p = q + 1;
	      if (c == end_char)
		{
		  /* Create an argument if necessary.
		     We create an argument if the symbol appears
		     in the expression by itself.

		     NOTE: This is temporary till the boot filesystem
		     servers support arguments.  When that happens,
		     symbol values will only be printed if they're
		     associated with an argument.  */
		  if (! arg && end_char == '}')
		    {
		      if (! add_arg (cmd, 0, 0, type, val))
			{
			  error = BOOT_SCRIPT_NOMEM;
			  goto bad;
			}
		    }
		  arg = 0;
		  break;
		}
	      if (s->type != VAL_FUNC)
		sym = s;
	    }
	}
      else
	{
	  char c;

	  /* Command argument; just copy the text.  */
	  for (q = p;; q++)
	    {
	      if (! *q || *q == ' ' || *q == '\t' || *q == '\n')
		break;
	      if (*q == '$' && *(q + 1) == '{')
		break;
	    }
	  c = *q;
	  *q = '\0';

	  /* Add argument to list.  */
	  arg = add_arg (cmd, p, q + 1 - p, VAL_NONE, 0);
	  if (! arg)
	    {
	      error = BOOT_SCRIPT_NOMEM;
	      goto bad;
	    }
	  if (c == '$')
	    p = q;
	  else
	    {
	      if (c)
		p = q + 1;
	      else
		p = q;
	      arg = 0;
	    }
	}
    }


 bad:
  free_cmd (cmd, 1);
  cleanup (1);
  return error;
}

/* Ensure that the command line buffer can accommodate LEN bytes of space.  */
#define CHECK_CMDLINE_LEN(len) \
{ \
  if (cmdline_alloc - cmdline_index < len) \
    { \
      char *ptr; \
      int alloc, i; \
      alloc = cmdline_alloc + len - (cmdline_alloc - cmdline_index) + 100; \
      ptr = boot_script_malloc (alloc); \
      if (! ptr) \
	{ \
	  error = BOOT_SCRIPT_NOMEM; \
	  goto done; \
	} \
      memcpy (ptr, cmdline, cmdline_index); \
      for (i = 0; i < argc; ++i) \
	argv[i] = ptr + (argv[i] - cmdline); \
      boot_script_free (cmdline, cmdline_alloc); \
      cmdline = ptr; \
      cmdline_alloc = alloc; \
    } \
}

/* Execute commands previously parsed.  */
int
boot_script_exec ()
{
  int cmd_index;

  for (cmd_index = 0; cmd_index < cmds_index; cmd_index++)
    {
      char **argv, *cmdline;
      int i, argc, cmdline_alloc;
      int cmdline_index, error, arg_index;
      struct cmd *cmd = cmds[cmd_index];

      /* Skip command if it doesn't have an associated task.  */
      if (cmd->task == 0)
	continue;

      /* Allocate a command line and copy command name.  */
      cmdline_index = strlen (cmd->path) + 1;
      cmdline_alloc = cmdline_index + 100;
      cmdline = boot_script_malloc (cmdline_alloc);
      if (! cmdline)
	{
	  cleanup (1);
	  return BOOT_SCRIPT_NOMEM;
	}
      memcpy (cmdline, cmd->path, cmdline_index);

      /* Allocate argument vector.  */
      argv = boot_script_malloc (sizeof (char *) * (cmd->args_index + 2));
      if (! argv)
	{
	  boot_script_free (cmdline, cmdline_alloc);
	  cleanup (1);
	  return BOOT_SCRIPT_NOMEM;
	}
      argv[0] = cmdline;
      argc = 1;

      /* Build arguments.  */
      for (arg_index = 0; arg_index < cmd->args_index; arg_index++)
	{
	  struct arg *arg = cmd->args[arg_index];

	  /* Copy argument text.  */
	  if (arg->text)
	    {
	      int len = strlen (arg->text);

	      if (arg->type == VAL_NONE)
		len++;
	      CHECK_CMDLINE_LEN (len);
	      memcpy (cmdline + cmdline_index, arg->text, len);
	      argv[argc++] = &cmdline[cmdline_index];
	      cmdline_index += len;
	    }

	  /* Add value of any symbol associated with this argument.  */
	  if (arg->type != VAL_NONE)
	    {
	      char *p, buf[50];
	      int len;
	      mach_port_t name;

	      if (arg->type == VAL_SYM)
		{
		  struct sym *sym = (struct sym *) arg->val;

		  /* Resolve symbol value.  */
		  while (sym->type == VAL_SYM)
		    sym = (struct sym *) sym->val;
		  if (sym->type == VAL_NONE)
		    {
		      error = BOOT_SCRIPT_UNDEF_SYM;
		      goto done;
		    }
		  arg->type = sym->type;
		  arg->val = sym->val;
		}

	      /* Print argument value.  */
	      switch (arg->type)
		{
		case VAL_STR:
		  p = (char *) arg->val;
		  len = strlen (p);
		  break;

		case VAL_TASK:
		case VAL_PORT:
		  if (arg->type == VAL_TASK)
		    /* Insert send right to task port.  */
		    error = boot_script_insert_task_port
		      (cmd, (task_t) arg->val, &name);
		  else
		    /* Insert send right.  */
		    error = boot_script_insert_right (cmd,
						      (mach_port_t) arg->val,
						      &name);
		  if (error)
		    goto done;

		  i = name;
		  p = buf + sizeof (buf);
		  len = 0;
		  do
		    {
		      *--p = i % 10 + '0';
		      len++;
		    }
		  while (i /= 10);
		  break;

		default:
		  error = BOOT_SCRIPT_BAD_TYPE;
		  goto done;
		}
	      len++;
	      CHECK_CMDLINE_LEN (len);
	      memcpy (cmdline + cmdline_index, p, len - 1);
	      *(cmdline + cmdline_index + len - 1) = '\0';
	      if (! arg->text)
		  argv[argc++] = &cmdline[cmdline_index];
	      cmdline_index += len;
	    }
	}

      /* Terminate argument vector.  */
      argv[argc] = 0;

      /* Execute the command.  */
      if (boot_script_exec_cmd (cmd->hook, cmd->task, cmd->path,
				argc, argv, cmdline, cmdline_index))
	{
	  error = BOOT_SCRIPT_EXEC_ERROR;
	  goto done;
	}

      error = 0;

    done:
      boot_script_free (cmdline, cmdline_alloc);
      boot_script_free (argv, sizeof (char *) * (cmd->args_index + 2));
      if (error)
	{
	  cleanup (1);
	  return error;
	}
    }

  for (cmd_index = 0; cmd_index < cmds_index; cmd_index++)
    {
      int i;
      struct cmd *cmd = cmds[cmd_index];

      /* Execute functions that want to be run on exec.  */
      for (i = 0; i < cmd->exec_funcs_index; i++)
	{
	  struct sym *sym = cmd->exec_funcs[i];
	  int error = ((*((int (*) (struct cmd *, integer_t *)) sym->val))
		       (cmd, 0));
	  if (error)
	    {
	      cleanup (1);
	      return error;
	    }
	}
    }

  cleanup (0);
  return 0;
}

/* Create an entry for the variable NAME with TYPE and value VAL,
   in the symbol table.  */
int
boot_script_set_variable (const char *name, int type, integer_t val)
{
  struct sym *sym = sym_enter (name);

  if (sym)
    {
      sym->type = type;
      sym->val = val;
    }
  return sym ? 0 : 1;
}


/* Define the function NAME, which will return type RET_TYPE.  */
int
boot_script_define_function (const char *name, int ret_type,
			     int (*func) (const struct cmd *cmd,
					  integer_t *val))
{
  struct sym *sym = sym_enter (name);

  if (sym)
    {
      sym->type = VAL_FUNC;
      sym->val = (integer_t) func;
      sym->ret_type = ret_type;
      sym->run_on_exec = ret_type == VAL_NONE;
    }
  return sym ? 0 : 1;
}


/* Return a string describing ERR.  */
char *
boot_script_error_string (int err)
{
  switch (err)
    {
    case BOOT_SCRIPT_NOMEM:
      return "no memory";

    case BOOT_SCRIPT_SYNTAX_ERROR:
      return "syntax error";

    case BOOT_SCRIPT_INVALID_ASG:
      return "invalid variable in assignment";

    case BOOT_SCRIPT_MACH_ERROR:
      return "mach error";

    case BOOT_SCRIPT_UNDEF_SYM:
      return "undefined symbol";

    case BOOT_SCRIPT_EXEC_ERROR:
      return "exec error";

    case BOOT_SCRIPT_INVALID_SYM:
      return "invalid variable in expression";

    case BOOT_SCRIPT_BAD_TYPE:
      return "invalid value type";
    }
  return 0;
}

#ifdef BOOT_SCRIPT_TEST
#include <stdio.h>

int
boot_script_exec_cmd (void *hook,
		      mach_port_t task, char *path, int argc,
		      char **argv, char *strings, int stringlen)
{
  int i;

  printf ("port = %d: ", (int) task);
  for (i = 0; i < argc; i++)
    printf ("%s ", argv[i]);
  printf ("\n");
  return 0;
}

void
main (int argc, char **argv)
{
  char buf[500], *p;
  int len;
  FILE *fp;
  mach_port_t host_port, device_port;

  if (argc < 2)
    {
      fprintf (stderr, "Usage: %s <script>\n", argv[0]);
      exit (1);
    }
  fp = fopen (argv[1], "r");
  if (! fp)
    {
      fprintf (stderr, "Can't open %s\n", argv[1]);
      exit (1);
    }
  host_port = 1;
  device_port = 2;
  boot_script_set_variable ("host-port", VAL_PORT, (int) host_port);
  boot_script_set_variable ("device-port", VAL_PORT, (int) device_port);
  boot_script_set_variable ("root-device", VAL_STR, (int) "hd0a");
  boot_script_set_variable ("boot-args", VAL_STR, (int) "-ad");
  p = buf;
  len = sizeof (buf);
  while (fgets (p, len, fp))
    {
      int i, err;

      i = strlen (p) + 1;
      err = boot_script_parse_line (0, p);
      if (err)
	{
	  fprintf (stderr, "error %s\n", boot_script_error_string (err));
	  exit (1);
	}
      p += i;
      len -= i;
    }
  boot_script_exec ();
  exit (0);
}
#endif /* BOOT_SCRIPT_TEST */
