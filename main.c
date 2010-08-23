#include <mach.h>
#include <hurd.h>
#include <unistd.h>
#include <error.h>
#include <argp.h>
#include <hurd/netfs.h>
#include "procfs.h"
#include "proclist.h"
#include "rootdir.h"
#include "dircat.h"
#include "main.h"

/* Command-line options */
int opt_clk_tck;
mode_t opt_stat_mode;
pid_t opt_fake_self;
pid_t opt_kernel_pid;

static error_t
argp_parser (int key, char *arg, struct argp_state *state)
{
  char *endp;

  switch (key)
  {
    case 'h':
      opt_clk_tck = strtol (arg, &endp, 0);
      if (*endp || ! *arg || opt_clk_tck <= 0)
	error (1, 0, "--clk-tck: HZ should be a positive integer");
      break;

    case 's':
      opt_stat_mode = strtol (arg, &endp, 8);
      if (*endp || ! *arg || opt_stat_mode & ~07777)
	error (1, 0, "--stat-mode: MODE should be an octal mode");
      break;

    case 'S':
      if (arg)
        {
	  opt_fake_self = strtol (arg, &endp, 0);
	  if (*endp || ! *arg)
	    error (1, 0, "--fake-self: PID must be an integer");
	}
      else
	opt_fake_self = 1;
      break;

    case 'k':
      opt_kernel_pid = strtol (arg, &endp, 0);
      if (*endp || ! *arg || (signed) opt_kernel_pid < 0)
	error (1, 0, "--kernel-process: PID must be a positive integer");
      break;
  }

  return 0;
}

struct argp argp = {
  .options = (struct argp_option []) {
    { "clk-tck", 'h', "HZ", 0,
	"Unit used for the values expressed in system clock ticks "
	"(default: sysconf(_SC_CLK_TCK))" },
    { "stat-mode", 's', "MODE", 0,
	"The [pid]/stat file publishes information which on Hurd is only "
	"available to the process owner.  "
	"You can use this option to override its mode to be more permissive "
	"for compatibility purposes.  "
	"(default: 0400)" },
    { "fake-self", 'S', "PID", OPTION_ARG_OPTIONAL,
	"Provide a fake \"self\" symlink to the given PID, for compatibility "
	"purposes.  If PID is omitted, \"self\" will point to init.  "
	"(default: no self link)" },
    { "kernel-process", 'k', "PID", 0,
	"Process identifier for the kernel, used to retreive its command line "
	"(default: 2)" },
    {}
  },
  .parser = argp_parser,
  .doc = "A virtual filesystem emulating the Linux procfs.",
  .children = (struct argp_child []) {
    { &netfs_std_startup_argp, },
    {}
  },
};

error_t
root_make_node (struct node **np)
{
  /* We never have two root nodes alive simultaneously, so it's ok to
     have this as static data.  */
  static struct node *root_dirs[3];
  error_t err;

  err = proclist_create_node (getproc (), &root_dirs[0]);
  if (err)
    return err;

  err = rootdir_create_node (&root_dirs[1]);
  if (err)
    {
      netfs_nrele (root_dirs[0]);
      return err;
    }

  root_dirs[2] = NULL;
  *np = dircat_make_node (root_dirs);
  if (! *np)
    return ENOMEM;

  /* Since this one is not created through proc_lookup(), we have to affect an
     inode number to it.  */
  (*np)->nn_stat.st_ino = * (uint32_t *) "PROC";

  return 0;
}

int main (int argc, char **argv)
{
  mach_port_t bootstrap;

  opt_clk_tck = sysconf(_SC_CLK_TCK);
  opt_stat_mode = 0400;
  opt_fake_self = -1;
  opt_kernel_pid = 2;
  argp_parse (&argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  netfs_init ();
  root_make_node (&netfs_root_node);

  netfs_startup (bootstrap, 0);
  for (;;)
    netfs_server_loop ();
}

