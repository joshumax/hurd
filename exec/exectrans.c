
#include <stdio.h>
#include <getopt.h>
#include <error.h>
#include <sys/stat.h>

#include <hurd.h>
#include <hurd/trivfs.h>


/* Where to put the service ports. */
static struct port_bucket *port_bucket;

/* Trivfs hooks.  */
int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;
int trivfs_support_read = 1;
int trivfs_support_write = 1;
int trivfs_support_exec = 1;
int trivfs_allow_open = O_READ|O_WRITE|O_EXEC;

struct port_class *trivfs_protid_portclasses[1];
struct port_class *trivfs_cntl_portclasses[1];
int trivfs_protid_nportclasses = 1;
int trivfs_cntl_nportclasses = 1;


static int
exec_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  extern int exec_server (mach_msg_header_t *inp, mach_msg_header_t *outp);
  return exec_server (inp, outp) || trivfs_demuxer (inp, outp);
}

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_fstype = FSTYPE_MISC;
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  int count;

  /* Stop new requests.  */
  ports_inhibit_class_rpcs (trivfs_cntl_portclasses[0]);
  ports_inhibit_class_rpcs (trivfs_protid_portclasses[0]);

  /* Are there any extant user ports for the /servers/exec file?  */
  count = ports_count_class (trivfs_protid_portclasses[0]);
  if (count == 0 || (flags & FSYS_GOAWAY_FORCE))
    {
      /* No users.  Disconnect from the filesystem.  */
      mach_port_deallocate (mach_task_self (), fsys->underlying);

      /* Are there remaining exec_startup RPCs to answer?  */
      count = ports_count_class (execboot_portclass);
      if (count == 0)
	/* Nope.  We got no reason to live.  */
	exit (0);

      /* Continue servicing tasks starting up.  */
      ports_enable_class (execboot_portclass);

      /* No more communication with the parent filesystem.  */
      ports_destroy_right (fsys);
      going_down = 1;

      return 0;
    }
  else
    {
      /* We won't go away, so start things going again...  */
      ports_enable_class (trivfs_protid_portclasses[0]);
      ports_resume_class_rpcs (trivfs_cntl_portclasses[0]);
      ports_resume_class_rpcs (trivfs_protid_portclasses[0]);

      return EBUSY;
    }
}
