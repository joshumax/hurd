#include <mach.h>
#include <hurd.h>
#include <error.h>
#include <argp.h>
#include <hurd/netfs.h>
#include "procfs.h"
#include "procfs_file.h"
#include "procfs_dir.h"
#include "proclist.h"
#include "dircat.h"

static struct node *
make_file (void *dir_hook, void *ent_hook)
{
  return procfs_file_make_node (ent_hook, -1, NULL);
}

error_t
root_make_node (struct node **np)
{
  static const struct procfs_dir_entry static_entries[] = {
    { "hello",		make_file,	"Hello, World!\n" },
    { "goodbye",	make_file,	"Goodbye, cruel World!\n" },
  };
  /* We never have two root nodes alive simultaneously, so it's ok to
     have this as static data.  */
  static struct node *root_dirs[3];

  root_dirs[0] = procfs_dir_make_node (static_entries, NULL, NULL);
  if (! root_dirs[0])
    return ENOMEM;

  root_dirs[1] = proclist_make_node (getproc ());
  if (! root_dirs[1])
    {
      netfs_nrele (root_dirs[0]);
      return ENOMEM;
    }

  root_dirs[2] = NULL;
  *np = dircat_make_node (root_dirs);
  if (! *np)
    return ENOMEM;

  return 0;
}

int main (int argc, char **argv)
{
  mach_port_t bootstrap;

  argp_parse (&netfs_std_startup_argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  netfs_init ();
  root_make_node (&netfs_root_node);

  netfs_startup (bootstrap, 0);
  for (;;)
    netfs_server_loop ();
}

