#include <stdlib.h>
#include <hurd/process.h>
#include "procfs.h"
#include "procfs_dir.h"
#include "process.h"

struct process_node {
  process_t procserv;
  pid_t pid;
};


/* The proc_getprocargs() and proc_getprocenv() calls have the same
   prototype and we use them in the same way; namely, publish the data
   they return as-is.  We take advantage of this to have common code and
   use a function pointer as the procfs_dir "entry hook" to choose the
   call to use on a file by file basis.  */

struct process_argz_node
{
  struct process_node pn;
  error_t (*getargz) (process_t, pid_t, void **, mach_msg_type_number_t *);
};

static error_t
process_argz_get_contents (void *hook, void **contents, size_t *contents_len)
{
  struct process_argz_node *pz = hook;
  error_t err;

  *contents_len = 0;
  err = pz->getargz (pz->pn.procserv, pz->pn.pid, contents, contents_len);
  if (err)
    return EIO;

  return 0;
}

static struct node *
process_argz_make_node (void *dir_hook, void *entry_hook)
{
  static const struct procfs_node_ops ops = {
    .get_contents = process_argz_get_contents,
    .cleanup_contents = procfs_cleanup_contents_with_vm_deallocate,
    .cleanup = free,
  };
  struct process_argz_node *zn;

  zn = malloc (sizeof *zn);
  if (! zn)
    return NULL;

  memcpy (&zn->pn, dir_hook, sizeof zn->pn);
  zn->getargz = entry_hook;

  return procfs_make_node (&ops, zn);
}


struct node *
process_make_node (process_t procserv, pid_t pid)
{
  static const struct procfs_dir_entry entries[] = {
    { "cmdline",	process_argz_make_node,		proc_getprocargs,	},
    { "environ",	process_argz_make_node,		proc_getprocenv,	},
    { NULL, }
  };
  struct process_node *pn;

  pn = malloc (sizeof *pn);
  if (! pn)
    return NULL;

  pn->procserv = procserv;
  pn->pid = pid;

  return procfs_dir_make_node (entries, pn, free);
}

