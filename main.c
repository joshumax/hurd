#include <mach.h>
#include <hurd.h>
#include <error.h>
#include <argp.h>
#include <hurd/netfs.h>
#include "procfs.h"
#include "procfs_file.h"
#include "procfs_dir.h"
#include "proclist.h"

static struct node *make_file (void *dir_hook, void *ent_hook)
{
  return procfs_file_make_node (ent_hook, -1, NULL);
}

static struct node *make_proclist (void *dir_hook, void *ent_hook)
{
  return proclist_make_node (getproc ());
}

int main (int argc, char **argv)
{
  static const struct procfs_dir_entry entries[] = {
    { "hello",		make_file,	"Hello, World!\n" },
    { "goodbye",	make_file,	"Goodbye, cruel World!\n" },
    { "proclist",	make_proclist,	},
    { }
  };
  mach_port_t bootstrap;

  argp_parse (&netfs_std_startup_argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  netfs_init ();
  netfs_root_node = procfs_dir_make_node (entries, NULL);

  netfs_startup (bootstrap, 0);
  for (;;)
    netfs_server_loop ();
}

