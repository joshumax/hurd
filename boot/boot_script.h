/* Definitions for boot script parser for Mach.  */

/* Written by Shantanu Goel (goel@cs.columbia.edu).  */

/* Error codes returned by boot_script_parse_line()
   and boot_script_exec_cmd().  */
#define BOOT_SCRIPT_NOMEM		1
#define BOOT_SCRIPT_SYNTAX_ERROR	2
#define BOOT_SCRIPT_INVALID_ASG		3
#define BOOT_SCRIPT_MACH_ERROR		4
#define BOOT_SCRIPT_UNDEF_SYM		5
#define BOOT_SCRIPT_EXEC_ERROR		6
#define BOOT_SCRIPT_INVALID_SYM		7
#define BOOT_SCRIPT_BAD_TYPE		8

/* Legal values for argument `type' to function
   boot_script_set_variable and boot_script_define_function.  */
#define VAL_NONE	0	/* none -- function runs at exec time */
#define VAL_STR		1	/* string */
#define VAL_PORT	2	/* port */

/* This structure describes a command.  */
struct cmd
{
  /* Path of executable.  */
  char *path;

  /* Task port.  */
  mach_port_t task;

  /* Argument list.  */
  struct arg **args;

  /* Amount allocated for `args'.  */
  int args_alloc;

  /* Next available slot in `args'.  */
  int args_index;

  /* List of functions that want to be run on command execution.  */
  struct sym **exec_funcs;

  /* Amount allocated for `exec_funcs'.  */
  int exec_funcs_alloc;

  /* Next available slot in `exec_funcs'.  */
  int exec_funcs_index;
};


/* The user must define this function.  Load the image of the
   executable specified by PATH in TASK.  Create a thread
   in TASK and point it at the executable's entry point.  Initialize
   TASK's stack with argument vector ARGV of length ARGC whose
   strings are STRINGS.  STRINGS has length STRINGLEN.
   Return 0 for success, non-zero otherwise.  */
int boot_script_exec_cmd (mach_port_t task, char *path, int argc,
			  char **argv, char *strings, int stringlen);

/* The user must define this function.  Load the contents of FILE
   into a fresh anonymous memory object and return the memory object port.  */
mach_port_t boot_script_read_file (const char *file);

/* Parse the command line LINE.  This causes the command line to be
   converted into an internal format.  Returns 0 for success, non-zero
   otherwise.

   NOTE: The parser writes into the line so it must not be a string constant.
   It is also the responsibility of the caller not to deallocate the line
   across calls to the parser.  */
int boot_script_parse_line (char *cmdline);

/* Execute the command lines prevously parsed.
   Returns 0 for success, non-zero otherwise.  */
int boot_script_exec (void);

/* Create an entry in the symbol table for variable NAME,
   whose type is TYPE and value is VAL.  Returns 0 on success,
   non-zero otherwise.  */
int boot_script_set_variable (const char *name, int type, int val);

/* Define the function NAME, which will return type RET_TYPE.  */
int boot_script_define_function (const char *name, int ret_type,
				 int (*func) (const struct cmd *cmd, int *val));

/* Returns a string describing the error ERR.  */
char *boot_script_error_string (int err);


void safe_gets (char *, size_t);
