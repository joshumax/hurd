/* A translator for returning FS_RETRY_MAGIC strings.

   Copyright (C) 1999,2001,02, 03 Free Software Foundation, Inc.

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

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/trivfs.h>
#include <hurd/fshelp.h>
#include <hurd/fsys.h>
#include <version.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <error.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <argp.h>
#include <argz.h>
#include <assert-backtrace.h>

#include "fsys_S.h"

const char *argp_program_version = STANDARD_HURD_VERSION (magic);

/* This structure has all the state about one filesystem.
   It hangs off trivfs_control->hook.  */
struct magic
{
  /* We chain all filesystems together so we can tell easily when they are
     all unused.  */
  struct trivfs_control *next;

  /* The magic string we return for lookups.  */
  char *magic;

  int directory;		/* --directory flag */

  /* Pre-fab contents of dummy directory for dir_readdir.
     Set up only under --directory.  */
  void *dirbuf;
  size_t dirbufsize;

  unsigned int nusers;		/* Count of users, only with --directory.  */
};

static inline void
free_magic (struct magic *m)
{
  free (m->magic);
  if (m->dirbuf)
    munmap (m->dirbuf, m->dirbufsize);
  free (m);
}

static struct trivfs_control *all_fsys;

/* Trivfs hooks  */

/* Our port class.  */
struct port_class *trivfs_protid_class;

int trivfs_fstype = FSTYPE_DEV;
int trivfs_fsid = 0;

int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  struct magic *const m = cred->po->cntl->hook;

  st->st_size = m->dirbufsize;
  st->st_blocks = getpagesize () / S_BLKSIZE;

  st->st_mode = ((st->st_mode & ~S_IFMT & ~ALLPERMS)
		 | S_IFDIR | S_IXUSR|S_IXGRP|S_IXOTH
		 | (st->st_mode & (S_IRUSR|S_IRGRP|S_IROTH)));
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  struct magic *const m = fsys->hook;

  /* We are single-threaded, so no fancy stuff is needed here.  */

  if (m->nusers > 0 && !(flags & FSYS_GOAWAY_FORCE))
    return EBUSY;

  /* No more communication with the parent filesystem.
     This running RPC should now be the only ref keeping FSYS alive.  */
  ports_destroy_right (fsys);
  return 0;
}


/* Clean pointers in a struct trivfs_control when its last reference
   vanishes before it's freed.  This overrides the libtrivfs version
   so we can clean up our hook data.  */
void
trivfs_clean_cntl (void *arg)
{
  struct trivfs_control *cntl = arg;
  struct magic *const m = cntl->hook;

  /* Remove us from the list of all filesystems.  */
  struct trivfs_control **lastp = &all_fsys;
  while (*lastp != cntl)
    lastp = &((struct magic *) (*lastp)->hook)->next;
  *lastp = m->next;

  if (all_fsys == 0)
    /* Nothing more to do in this life.  */
    exit (0);

  mach_port_destroy (mach_task_self (), cntl->filesys_id);
  mach_port_destroy (mach_task_self (), cntl->file_id);
  mach_port_deallocate (mach_task_self (), cntl->underlying);

  free_magic (m);
}

/* This hook is used when running without --directory;
   it circumvents basically all the trivfs machinery.  */

static error_t
magic_getroot (struct trivfs_control *cntl,
	       mach_port_t reply_port,
	       mach_msg_type_name_t reply_port_type,
	       mach_port_t dotdot,
	       uid_t *uids, u_int nuids, uid_t *gids, u_int ngids,
	       int flags,
	       retry_type *do_retry, char *retry_name,
	       mach_port_t *node, mach_msg_type_name_t *node_type)
{
  error_t err;
  struct magic *const m = cntl->hook;

  if (m->directory)
    return EAGAIN;		/* Do normal trivfs getroot processing.  */

  strcpy (retry_name, m->magic);
  *do_retry = FS_RETRY_MAGICAL;
  *node = MACH_PORT_NULL;
  *node_type = MACH_MSG_TYPE_COPY_SEND;

  err = mach_port_deallocate (mach_task_self (), dotdot);
  assert_perror_backtrace (err);

  return 0;
}

/* This hook is used when running with --directory, when
   we do use all the normal trivfs machinery.  We just use
   the normal trivfs open, but then stash the DOTDOT port
   in the trivfs_peropen.  */

static error_t
magic_open  (struct trivfs_control *cntl,
	     struct iouser *user,
	     mach_port_t dotdot,
	     int flags,
	     mach_port_t realnode,
	     struct trivfs_protid **cred)
{
  error_t err = trivfs_open (cntl, user, flags, realnode, cred);
  if (!err)
    {
      /* We consume the reference for DOTDOT.  */
      (*cred)->po->hook = (void *) dotdot;
      struct magic *const m = cntl->hook;
      m->nusers++;
    }
  return err;
}

static void
magic_peropen_destroy (struct trivfs_peropen *po)
{
  mach_port_deallocate (mach_task_self (), (mach_port_t) po->hook);
}


/* We have this hook only for simple tracking of the live user ports.  */
static void
magic_protid_destroy (struct trivfs_protid *cred)
{
  struct magic *const m = cred->po->cntl->hook;
  m->nusers--;
}


/* Do a directory lookup.  */

error_t
trivfs_S_dir_lookup (struct trivfs_protid *cred,
		     mach_port_t reply, mach_msg_type_name_t reply_type,
		     char *name,
		     int flags,
		     mode_t mode,
		     retry_type *retry_type,
		     char *retry_name,
		     mach_port_t *retrypt,
		     mach_msg_type_name_t *retrypt_type)
{
  int perms;
  error_t err;
  struct trivfs_protid *newcred;
  mach_port_t dotdot;
  struct iouser *user;

  if (!cred)
    return EOPNOTSUPP;

  if (name[0] != '\0')
    {
      struct magic *const m = cred->po->cntl->hook;

      if (!m->directory)
	return ENOTDIR;

      /* We have a real lookup in the dummy directory.
	 Handle `.' and `..' specially, and anything else
	 gets redirected to the magical retry.  */

      while (*name == '/')
	++name;
      while (!strncmp (name, "./", 2))
	{
	  name += 2;
	  while (*name == '/')
	    ++name;
	}

      if (!strcmp (name, "..") || !strncmp (name, "../", 3))
	{
	  name += 2;
	  while (*name == '/')
	    ++name;
	  strcpy (retry_name, name);
	  *retry_type = FS_RETRY_REAUTH;
	  *retrypt = (mach_port_t) cred->po->hook;
	  *retrypt_type = MACH_MSG_TYPE_COPY_SEND;
	  return 0;
	}
      else if (name[0] != '\0' && strcmp (name, "."))
	{
	  if (m->magic == 0)
	    strcpy (retry_name, name);
	  else
	    {
	      char *p = stpcpy (retry_name, m->magic);
	      *p++ = '/';
	      strcpy (p, name);
	    }
	  *retry_type = FS_RETRY_MAGICAL;
	  *retrypt = MACH_PORT_NULL;
	  *retrypt_type = MACH_MSG_TYPE_COPY_SEND;
	  return 0;
	}
    }

  /* This is a null-pathname "reopen" call; do the right thing. */

  /* Burn off flags we don't actually implement */
  flags &= O_HURD;
  flags &= ~(O_CREAT|O_EXCL|O_NOLINK|O_NOTRANS);

  /* Validate permissions */
  if (! trivfs_check_access_hook)
    file_check_access (cred->realnode, &perms);
  else
    (*trivfs_check_access_hook) (cred->po->cntl, cred->user,
				 cred->realnode, &perms);
  if ((flags & (O_READ|O_WRITE|O_EXEC) & perms)
      != (flags & (O_READ|O_WRITE|O_EXEC)))
    return EACCES;

  /* Execute the open */

  dotdot = (mach_port_t) cred->po->hook;
  err = iohelp_dup_iouser (&user, cred->user);
  if (err)
    return err;
  err = magic_open (cred->po->cntl, user, dotdot, flags,
		    cred->realnode, &newcred);
  if (err)
    {
      iohelp_free_iouser (user);
      return err;
    }
  err = mach_port_mod_refs (mach_task_self (), dotdot,
			    MACH_PORT_RIGHT_SEND, +1);
  assert_perror_backtrace (err);
  err = mach_port_mod_refs (mach_task_self (), cred->realnode,
			    MACH_PORT_RIGHT_SEND, +1);
  assert_perror_backtrace (err);

  *retry_type = FS_RETRY_NORMAL;
  *retry_name = '\0';
  *retrypt = ports_get_right (newcred);
  *retrypt_type = MACH_MSG_TYPE_MAKE_SEND;
  ports_port_deref (newcred);
  return 0;
}

error_t
trivfs_S_dir_readdir (struct trivfs_protid *cred,
		      mach_port_t reply, mach_msg_type_name_t reply_type,
		      char **data,
		      size_t *datalen,
		      boolean_t *data_dealloc,
		      int entry,
		      int nentries,
		      vm_size_t bufsiz,
		      int *amount)
{
  if (!cred)
    return EOPNOTSUPP;

  struct magic *const m = cred->po->cntl->hook;

  if (entry > 0)
    {
      void *p;
      int i;
      i = 0;
      for (p = m->dirbuf; p < m->dirbuf + m->dirbufsize;
	   p += ((struct dirent *) p)->d_reclen)
	if (i++ == entry)
	  break;
      *data = p;
      *datalen = m->dirbuf + m->dirbufsize - p;
      *amount = 2 - entry;
    }
  else
    {
      *data = m->dirbuf;
      *datalen = m->dirbufsize;
      *amount = 2;
    }

  *data_dealloc = 0;
  return 0;
}


#include <hurd/paths.h>
#define _SERVERS_MAGIC _SERVERS "magic"

/* To whom should we try to delegate on startup?  */
static const char *delegate = _SERVERS_MAGIC;

static const struct argp_option options[] =
{
  {"directory",	'd', 0,		0, "Provide virtual (empty) directory node"},
  {"use-server", 'U', "NAME",	0,
   "Delegate to server NAME instead of " _SERVERS_MAGIC},
  {0}
};

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  struct magic *const m = state->input;

  switch (opt)
    {
    case 'U':
      /* This is only valid for the startup options, not delegates.  */
      if (all_fsys != 0)
	return EINVAL;
      delegate = arg;
      return 0;

    case 'd':
    case ARGP_KEY_NO_ARGS:
      m->directory = 1;
      return 0;

    case ARGP_KEY_ARG:
      if (m->magic != 0)
	{
	  argp_usage (state);
	  return EINVAL;
	}
      m->magic = strdup (arg);
      return m->magic == 0 ? ENOMEM : 0;

    case ARGP_KEY_SUCCESS:
      if (m->directory)
	{
	  inline struct dirent *add (struct dirent *d, const char *name)
	    {
	      d->d_fileno = 2;	/* random */
	      d->d_type = DT_DIR;
	      d->d_namlen = strlen (name);
	      strcpy (d->d_name, name);
	      d->d_name[d->d_namlen] = '\0';
	      d->d_reclen = &d->d_name[d->d_namlen + 1] - (char *) d;
	      d->d_reclen = ((d->d_reclen + __alignof (struct dirent) - 1)
			     & ~(__alignof (struct dirent) - 1));
	      return (struct dirent *) ((char *) d + d->d_reclen);
	    }
	  struct dirent *d;
	  m->dirbuf = mmap (0, getpagesize (), PROT_READ|PROT_WRITE,
			    MAP_ANON, 0, 0);
	  d = add (m->dirbuf, ".");
	  d = add (d, "..");
	  m->dirbufsize = (char *) d - (char *) m->dirbuf;
	}
      return 0;
    }

  return ARGP_ERR_UNKNOWN;
}

error_t
trivfs_append_args (struct trivfs_control *fsys,
		    char **argz, size_t *argz_len)
{
  struct magic *const m = fsys->hook;
  return ((m->directory ? argz_add (argz, argz_len, "--directory") : 0)
	  ?: (m->magic ? argz_add (argz, argz_len, m->magic) : 0));
}

static struct argp argp =
  {
    options, parse_opt, "MAGIC",
    "A translator that returns the magic retry result MAGIC."
  };

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;
  struct magic *m = calloc (1, sizeof *m);

  argp_parse (&argp, argc, argv, 0, 0, m);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  if (delegate != 0)
    {
      /* First, try to have the canonical server sitting on /servers/magic
	 take over for us.  */
      err = fshelp_delegate_translation (delegate, bootstrap, argv);
      if (err == 0)
	return 0;
    }

  /* Nope, we are doing it ourselves.  */

  trivfs_getroot_hook = &magic_getroot;
  trivfs_open_hook = &magic_open;
  trivfs_protid_destroy_hook = &magic_protid_destroy;
  if (m->directory)
    trivfs_peropen_destroy_hook = &magic_peropen_destroy;

  err = trivfs_add_protid_port_class (&trivfs_protid_class);
  if (err)
    error (1, 0, "error creating protid port class");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, 0, 0, trivfs_protid_class, 0, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "Contacting parent");
  fsys->hook = m;
  all_fsys = fsys;

  /* Launch. */
  while (1)
    {
      ports_manage_port_operations_one_thread (fsys->pi.bucket, trivfs_demuxer,
					       10 * 60 * 1000);

      /* That returns when 10 minutes pass without an RPC.  Try shutting down
	 as if sent fsys_goaway; if we have any users who need us to stay
	 around, this returns EBUSY and we loop to service more RPCs.  */

      struct trivfs_control *fs = all_fsys;
      do
	{
	  struct magic *const m = fs->hook;
	  struct trivfs_control *const next = m->next;
	  trivfs_goaway (fs, 0);
	  fs = next;
	} while (fs != 0);
    }

  return 0;
}


/* Handle delegated filesystems.  */
error_t
trivfs_S_fsys_forward (mach_port_t server,
		       mach_port_t reply,
		       mach_msg_type_name_t replytype,
		       mach_port_t requestor,
		       char *argz, size_t argz_len)
{
  struct trivfs_protid *cred
    = ports_lookup_port (all_fsys->pi.bucket, server, trivfs_protid_class);
  if (!cred)
    return EOPNOTSUPP;
  ports_port_deref (cred);

  /* Allocate a new structure for parameters, and parse the arguments
     to fill it in.  */
  struct magic *m = calloc (1, sizeof *m);
  if (!m)
    return ENOMEM;

  int argc = argz_count (argz, argz_len);
  char *argv[argc + 1];
  argz_extract (argz, argz_len, argv);
  error_t err = argp_parse (&argp, argc, argv,
			    ARGP_NO_ERRS | ARGP_NO_HELP, 0, m);
  if (err)
    {
      free_magic (m);
      return err;
    }

  /* Now we are ready to start up the filesystem.  Contact the parent.  */
  struct trivfs_control *fsys;
  err = trivfs_startup (requestor, 0,
			NULL, all_fsys->pi.bucket,
			NULL, all_fsys->pi.bucket,
			&fsys);
  if (err)
    {
      free_magic (m);
      return err;
    }
  mach_port_deallocate (mach_task_self (), requestor);

  /* The new filesystem is all hooked up.
     Put it on the list of all filesystems we are serving.  */
  m->next = all_fsys;
  fsys->hook = m;
  all_fsys = fsys;

  return 0;
}
