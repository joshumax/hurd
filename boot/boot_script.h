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
   boot_script_set_variable().  */
#define VAL_STR		1	/* string */
#define VAL_PORT	2	/* port */

/* The user must define this variable.  The task port of the program.  */
extern mach_port_t boot_script_task_port;

/* The user must define this function.  Allocate SIZE bytes of memory
   and return a pointer to it.  */
void *boot_script_malloc (int size);

/* The user must define this function.  Free SIZE bytes of memory
   named by PTR that was previously allocated by boot_script_malloc().  */
void boot_script_free (void *ptr, int size);

/* The user must define this function.  Create a new task and
   return a send right to it presumably using task_create().
   Return 0 on error.  */
mach_port_t boot_script_task_create (void);

/* The user must define this function.  Terminate the task whose
   send right is TASK presumably using task_terminate().  */
void boot_script_task_terminate (mach_port_t task);

/* The user must define this function.  Suspend TASK presumably
   using task_suspend().  */
void boot_script_task_suspend (mach_port_t task);

/* The user must define this function.  Resume TASK presumably
   using task_resume().  Return 0 on success, non-zero otherwise.  */
int boot_script_task_resume (mach_port_t task);

/* The user must define this function.  Deallocate a send right to PORT
   in the TASK presumably using mach_port_deallocate().  */
void boot_script_port_deallocate (mach_port_t task, mach_port_t port);

/* The user must define this function.  Insert the specified RIGHT to
   the PORT in the current task into TASK with NAME, presumably using
   mach_port_insert_right().  RIGHT can take any value allowed by
   mach_port_insert_right().  Return 0 for success, non-zero otherwise.  */
int boot_script_port_insert_right (mach_port_t task,
				   mach_port_t name, mach_port_t port,
				   mach_msg_type_name_t right);

/* The user must define this function.  Set the bootstrap port for TASK.  */
void boot_script_set_bootstrap_port (mach_port_t task, mach_port_t port);

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

/* Returns a string describing the error ERR.  */
char *boot_script_error_string (int err);
