#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach.h>
#include <hurd/process.h>
#include "procfs.h"
#include "procfs_file.h"
#include "procfs_dir.h"

#define PID_STR_SIZE (3 * sizeof (pid_t) + 1)

struct proclist_node
{
  process_t process;
};

static error_t
proclist_get_contents (void *hook, void **contents, size_t *contents_len)
{
  struct proclist_node *pl = hook;
  pidarray_t pids;
  mach_msg_type_number_t num_pids;
  error_t err;
  int i;

  num_pids = 0;
  err = proc_getallpids (pl->process, &pids, &num_pids);
  if (err)
    return EIO;

  *contents = malloc (num_pids * PID_STR_SIZE);
  if (*contents)
    {
      *contents_len = 0;
      for (i=0; i < num_pids; i++)
	{
	  int n = sprintf (*contents + *contents_len, "%d", pids[i]);
	  assert (n >= 0);
	  *contents_len += (n + 1);
	}
    }
  else
    err = ENOMEM;

  vm_deallocate (mach_task_self (), (vm_address_t) pids, num_pids * sizeof pids[0]);
  return err;
}

static error_t
proclist_lookup (void *hook, const char *name, struct node **np)
{
  *np = procfs_file_make_node ("Ceci n'est pas un processus\n", -1, NULL);
  return *np ? 0 : ENOMEM;
}

struct node *
proclist_make_node (process_t process)
{
  static const struct procfs_node_ops ops = {
    .get_contents = proclist_get_contents,
    .lookup = proclist_lookup,
    .cleanup_contents = free,
    .cleanup = free,
  };
  struct proclist_node *pl;
  struct node *np;

  pl = malloc (sizeof *pl);
  if (! pl)
    return NULL;

  memset (pl, 0, sizeof *pl);
  pl->process = process;

  np = procfs_make_node (&ops, pl);
  if (! np)
    free (pl);

  return np;
}

