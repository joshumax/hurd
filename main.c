#include <mach.h>
#include <error.h>
#include <argp.h>
#include <hurd/netfs.h>
#include "procfs.h"
#include "procfs_file.h"

static error_t get_entries (void *hook, void **contents, size_t *contents_len)
{
  static const char entries[] = "hello";
  *contents = (void *) entries;
  *contents_len = sizeof entries;
  return 0;
}

static error_t lookup (void *hook, const char *name, struct node **np)
{
  if (strcmp (name, "hello"))
    return ENOENT;

  *np = procfs_file_make_node ("Hello, World!\n", -1, NULL);
  if (! *np)
    return ENOMEM;

  return 0;
}

int main (int argc, char **argv)
{
  static const struct procfs_node_ops ops = {
    .get_contents = get_entries,
    .lookup = lookup,
  };
  mach_port_t bootstrap;

  argp_parse (&netfs_std_startup_argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  netfs_init ();
  netfs_root_node = procfs_make_node (&ops, NULL);

  netfs_startup (bootstrap, 0);
  for (;;)
    netfs_server_loop ();
}

