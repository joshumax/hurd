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
  }

  return 0;
}

struct argp argp = {
  .options = (struct argp_option []) {
    { "clk-tck", 'h', "HZ", 0,
	"Unit used for the values expressed in system clock ticks "
	"(default: sysconf(_SC_CLK_TCK))" },
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

