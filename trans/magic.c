/* A translator for returning FS_RETRY_MAGIC strings.

   Copyright (C) 1999,2001,02 Free Software Foundation, Inc.

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
#include <assert.h>

const char *argp_program_version = STANDARD_HURD_VERSION (magic);
static char args_doc[] = "MAGIC";
static char doc[] = "A translator that returns the magic retry result MAGIC.";
static const struct argp_option options[] =
{
  {"directory",	'd', 0,		0, "Provide virtual (empty) directory node"},
  {0}
};

/* The magic string we return for lookups.  */
static char *magic;

static int directory;		/* --directory flag */

/* Pre-fab contents of dummy directory for dir_readdir.
   Set up only under --directory.  */
static void *dirbuf;
static size_t dirbufsize;

/* Trivfs hooks  */

int trivfs_fstype = FSTYPE_DEV;
int trivfs_fsid = 0;

int trivfs_support_read = 0;
int trivfs_support_write = 0;
int trivfs_support_exec = 0;

int trivfs_allow_open = O_READ;

void
trivfs_modify_stat (struct trivfs_protid *cred, struct stat *st)
{
  st->st_size = dirbufsize;
  st->st_blocks = getpagesize () / S_BLKSIZE;

  st->st_mode = ((st->st_mode & ~S_IFMT & ~ALLPERMS)
		 | S_IFDIR | S_IXUSR|S_IXGRP|S_IXOTH
		 | (st->st_mode & (S_IRUSR|S_IRGRP|S_IROTH)));
}

error_t
trivfs_goaway (struct trivfs_control *fsys, int flags)
{
  exit (0);
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
  strcpy (retry_name, magic);
  *do_retry = FS_RETRY_MAGICAL;
  *node = MACH_PORT_NULL;
  *node_type = MACH_MSG_TYPE_COPY_SEND;
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
      (*cred)->po->hook = (void *) dotdot;
      err = mach_port_mod_refs (mach_task_self (), dotdot,
				MACH_PORT_RIGHT_SEND, +1);
      assert_perror (err);
      err = mach_port_deallocate (mach_task_self (), dotdot);
      assert_perror (err);
    }
  return err;
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
      if (!directory)
	return ENOTDIR;
      else
	{
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
	      char *p = stpcpy (retry_name, magic);
	      *p++ = '/';
	      strcpy (p, name);
	      *retry_type = FS_RETRY_MAGICAL;
	      *retrypt = MACH_PORT_NULL;
	      *retrypt_type = MACH_MSG_TYPE_COPY_SEND;
	      return 0;
	    }
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
  assert_perror (err);
  err = mach_port_mod_refs (mach_task_self (), cred->realnode,
			    MACH_PORT_RIGHT_SEND, +1);
  assert_perror (err);

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

  if (entry > 0)
    {
      void *p;
      int i;
      i = 0;
      for (p = dirbuf; p < dirbuf + dirbufsize;
	   p += ((struct dirent *) p)->d_reclen)
	if (i++ == entry)
	  break;
      *data = p;
      *datalen = dirbuf + dirbufsize - p;
      *amount = 2 - entry;
    }
  else
    {
      *data = dirbuf;
      *datalen = dirbufsize;
      *amount = 2;
    }

  *data_dealloc = 0;
  return 0;
}



static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    case 'd':
      directory = 1;
      return 0;

    case ARGP_KEY_NO_ARGS:
      argp_usage (state);
      return EINVAL;

    case ARGP_KEY_ARGS:
      if (state->next != state->argc - 1)
	{
	  argp_usage (state);
	  return EINVAL;
	}
      else
	{
	  magic = state->argv[state->next];
	  return 0;
	}
      break;
    }

  return ARGP_ERR_UNKNOWN;
}

error_t
trivfs_append_args (struct trivfs_control *fsys,
		    char **argz, size_t *argz_len)
{
  return ((directory ? argz_add (argz, argz_len, "--directory") : 0)
	  ?: argz_add (argz, argz_len, magic));
}

int
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap;
  struct trivfs_control *fsys;
  struct argp argp = { options, parse_opt, args_doc, doc };

  argp_parse (&argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    error (1, 0, "Must be started as a translator");

  /* Reply to our parent */
  err = trivfs_startup (bootstrap, 0, 0, 0, 0, 0, &fsys);
  mach_port_deallocate (mach_task_self (), bootstrap);
  if (err)
    error (3, err, "Contacting parent");

  if (directory)
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
      dirbuf = mmap (0, getpagesize (), PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
      d = add (dirbuf, ".");
      d = add (d, "..");
      dirbufsize = (char *) d - (char *) dirbuf;

      trivfs_open_hook = &magic_open;
    }
  else
    trivfs_getroot_hook = &magic_getroot;

  /* Launch. */
  ports_manage_port_operations_one_thread (fsys->pi.bucket, trivfs_demuxer,
					   2 * 60 * 1000);

  return 0;
}
