/* Login record (utmp) server.

   Copyright (C) 1995, 1999 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* This program maintains an translator node that represents a particular
   login session and answers queries about that session's status.  The PID of
   the session's root process is given, and the session will be considered
   defunct if this process goes away.  */

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd/paths.h>
#include <sys/socket.h>
#include <hurd/socket.h>
#include <hurd/fsys.h>
#include <mach/notify.h>
#include <stdio.h>
#include <assert-backtrace.h>
#include <fcntl.h>
#include <getopt.h>

/* ---------------------------------------------------------------- */
/* Global state.  */

/* A string describing the user's location.  */
static char *location = NULL;

/* The login collection we're for.  */
static int login_collection = 0;

/* A string containing a list of devices the user is using for this login
   session, separated by '\0'.  */
static char *devices = "";
/* The total length of DEVICES.  */
static int devices_len = 0;
/* The amount of space allocated for DEVICES.  */
static int devices_alloced = 0;

/* ---------------------------------------------------------------- */

/* If NAME is in the global DEVICES vector, return a pointer to it in
   DEVICES, otherwise return NULL.  */
static char *
_find_device(char *name)
{
  char *dev = devices;
  int left = devices_len;

  while (left > 0)
    {
      if (strcmp(name, dev) == 0)
	return dev;
      else
	{
	  int skip = strlen (dev) + 1;
	  dev += skip;
	  left -= skip;
	}
    }

  return 0;
}

/* Add NAME to the global DEVICES vector, unless it's already there.  If a
   memory allocation error occurs, ENOMEM is returned, otherwise 0.  */
static error_t
add_device(char *name)
{
  int len = strlen(name) + 1;

  if (!_find_device(name))
    {
      if (len + devices_len > devices_alloced)
	{
	  int alloc = len + devices_len;
	  char *new_devs = realloc(devices, alloc);
	  if (new_devs == NULL)
	    return ENOMEM;

	  devices = new_devs;
	  devices_alloced = alloc;
	}

      strcpy(devices + devices_len, name);
      devices_len += len;
    }

  return 0;
}

/* Remove NAME from the global DEVICES vector, if it's there.  */
static void
remove_device(char *name)
{
  char *dev = _find_device(name);

  if (dev)
    {
      int len = strlen(name) + 1;
      int tail_len = devices_len - ((dev - devices) + len);
      if (tail_len > 0)
	bcopy(dev + len, dev, tail_len);
    }
}

/* Given that DEV points to a device name in the global DEVICES vector,
   return a pointer to the next one, or NULL if DEV was the last.  */
static char *
next_device(char *dev)
{
  int next_pos = (dev - devices) + strlen(dev) + 1;
  if (next_pos == devices_len)
    return NULL;
  else
    return devices + next_pos;
}

/* ---------------------------------------------------------------- */

/* Try and start the translator for CTL_PORT on NODE.  If successful, this
   call will not return until the translator is stopped; otherwise it returns
   an error code describing the reason why it couldn't start.  */
static error_t
start_translator(file_t node, fsys_t ctl_port)
{
  mach_port_t prev;
  error_t err =
    file_set_translator(node, 0, FS_TRANS_EXCL, 0, NULL, 0, ctl_port);

  if (err)
    return err;

  /* Set up a watch on the process we're interested in.  */
  err =
    mach_port_request_notification(mach_task_self(),
				   watched_process, MACH_NOTIFY_DEAD_NAME, 1,
				   node,
				   MACH_MSG_TYPE_MAKE_SEND_ONCE, &prev);
  if (prev)
    mach_port_deallocate(mach_task_self(), prev);

  /* Boogie. */
  ports_manage_port_operations_onethread();

  return 0;
}

/* Find an unoccupied (one with no active translator) filename starting with
   NAME_PFX, and start the translator for CTL_PORT on it.  If successful, this
   call will not return until the translator is stopped; otherwise it returns
   an error code describing the reason why it couldn't start.  When
   successful, this function sets UTMP_NODE_NAME to the name of the file we
   started the translator on.  */
static error_t
find_node_and_start_translator(char *name_pfx, fsys_t ctl_port)
{
  error_t err;
  int num = 0;

  do
    {
      file_t node;

      sprintf(utmp_node_name, "%s:%d", name_pfx, num++);

      node = file_name_lookup(utmp_node_name, O_CREAT | O_NOTRANS, 0666);
      if (node == MACH_PORT_NULL)
	err = errno;
      else
	err = start_translator(node, ctl_port);
    }
  while (err == EBUSY);

  return err;
}

/* ---------------------------------------------------------------- */

#define SHORT_OPTIONS "p"

static struct option options[] =
{
  {"pid", required_argument, 0, 'p'},
  { 0 }
}

int
main (int argc, char **argv)
{
  int opt;
  error_t err;
  fsys_t ctl_port;
  int pid = 0;
  process_t proc_server = getproc();

  while ((opt = getopt_long(argc, argv, SHORT_OPTIONS, options, 0)) != EOF)
    switch (opt)
      {
      case 'p':			/* Watch pid N */
	pid = atoi(optarg);
	if (pid == 0)
	  error(1, 0, "%s: Bad process id", optarg);
	break;
      default:
	usage(-1);
      }
	
  if (pid == 0)
    /* By default watch the top of the current login collection.  */
    {
      err = proc_getloginid(proc_server, getpid(), &pid);
      if (err)
	error(2, err, "Couldn't get current login collection");
    }

  /* Setup a watch on the process we're interested in.  */
  err = proc_pid2proc(proc_server, pid, &watched_process);
  if (err)
    error(3, err, "Couldn't get process port for watched process");

  _libports_initialize ();

  ctl_port = trivfs_handle_port(MACH_PORT_NULL, PT_CTL, PT_NODE);
  
}

/* Looks for the dead-name notification message that indicates the process
   we're watching for has died, and kills off ourselves if it sees it.  */
static int
notice_process_died_msg(mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  if (inp->msgh_id == MACH_NOTIFY_DEAD_NAME)
    {
      mach_dead_name_notification_t *notify_msg =
	(mach_dead_name_notification_t *)inp;

      if (notify_msg->not_port == watched_process)
	/* This is what we've been waiting for!  Now we can die.  */
	die(0);
    }

  return 0;
}

/* ---------------------------------------------------------------- */

/* Copy the char array DATA, of length DATA_LEN, into the MIG return buffer
   pointed to by BUF, whose length is pointed to by BUF_LEN, reallocating BUF
   with vm_allocate if it isn't long enough.  BUF and BUF_LEN are updated to
   point to the final storage & real length.  If a vm allocation failure
   occurs, EKERN_NO_SPACE is returned, otherwise 0.  */
static error_t
return_data(char *data, int data_len, char **buf, int *buf_len)
{
  if (data_len > *buf_len)
    {
      *buf = mmap (0, data_len, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      if (*buf == (char *) -1)
	return errno;
    }

  *buf_len = data_len;
  bcopy(data, buf, data_len);

  return 0;
}

/* ---------------------------------------------------------------- */
/* login RPC handlers.  */

/* Send an immediate message to the user.  */
error_t
S_login_message_user(file_t utmp, char *msg, int msg_len)
{
  error_t err;
#if 0
  struct trivfs_protid *cred = ports_check_port_type (sockfile, PT_NODE);
  int perms;
  
  if (!cred)
    return EOPNOTSUPP;
  
  err = file_check_access (cred->realnode, &perms);
  if (!err && !(perms & O_READ))
    err = EACCES;
#endif

  return 0;
}

/* Return a human-readable string describing the user's physical location.  */
error_t
S_login_get_location(file_t utmp, char **buf, unsigned *len)
{
  return return_data(location, strlen(location) + 1, buf, len);
}

/* Return how much time has passed since the user last used an input device. */
error_t
S_login_get_idle_time(file_t utmp, time_value_t *tv)
{
  tv->seconds = 0;
  tv->microseconds = 0;

  if (devices_len > 0)
    {
      char *dev;
      for (dev = devices; dev != NULL; dev = next_device(dev))
	{
	  struct stat stat;
	  if (stat(dev, &state) == 0
	      && (stat.st_atim.tv_sec < tv->seconds
		  || (stat.st_atim.tv_sec == tv->seconds
		      && stat.st_atim.tv_nsec / 1000 < tv->microseconds)))
	    {
	      tv->seconds = stat.st_atim.tv_sc;
	      tv->microseconds = stat.st_atim.tv_nsec / 1000;
	    }
	}
    }

  return 0;
}

/* Return a list of file names for input devices being used, separated by null
   characters.  This call is optional; clients should not depend on it.  */
error_t
S_login_get_input_devices(file_t utmp, char **buf, unsigned *len)
{
  return return_data(devices, devices_len, buf, len);
}

/* Return the process collection ID for the user's login collection. */
error_t
S_login_get_login_collection(file_t utmp, int *pid)
{
  *pid = login_collection;
  return 0;
}

/* ---------------------------------------------------------------- */
/* Trivfs hooks  */

/* Port types */
#define PT_CTL 0
#define PT_NODE 1
#define PT_PROC 2		/* Our process handle. */

int trivfs_fstype = FSTYPE_MISC;
int trivfs_fsid = 0;

int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;

int trivfs_allow_open = 0;

int trivfs_protid_porttypes[] = {PT_NODE};
int trivfs_cntl_porttypes[] = {PT_CTL};
int trivfs_protid_nporttypes = 1;
int trivfs_cntl_nporttypes = 1;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  die(0);
}

/* ---------------------------------------------------------------- */
/* Ports hooks  */

void (*ports_cleanroutines[])(void *) =
{
  [PT_CTL] = trivfs_clean_cntl,
  [PT_NODE] = trivfs_clean_protid,
};

int
ports_demuxer (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  return notice_process_died_msg(inp, outp)
    || login_server(inp, outp) || trivfs_demuxer(inp, outp);
}

void
ports_notice_idle (int nhard, int nsoft)
{
  if (!nhard && !nsoft)
    die(0);
}

void
ports_no_live_ports ()
{
  die(0);
}

void
ports_no_hard_ports ()
{
  die(0);
}
