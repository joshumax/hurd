/* Roughly Unix/Linux-compatible `mount' frontend for Hurd translators.

   Copyright (C) 1999, 2004, 2013 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "../sutils/fstab.h"
#include <argp.h>
#include <argz.h>
#include <error.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <hurd/fsys.h>
#include <hurd/fshelp.h>
#include <hurd/paths.h>
#ifdef HAVE_BLKID
#include <blkid/blkid.h>
#endif

#include "match-options.h"

#define SEARCH_FMTS	_HURD "%sfs\0" _HURD "%s"
#define DEFAULT_FSTYPE	"auto"

static char *fstype = DEFAULT_FSTYPE;
static char *device, *mountpoint;
static int verbose;
static int fake;
static char *options;
static size_t options_len;
static mach_msg_timeout_t timeout;

static enum { mount, query } mode;
static enum { qf_standard, qf_fstab, qf_translator } query_format;
static struct fstab_argp_params fstab_params;


static const struct argp_option argp_opts[] =
{
  {"timeout",	'T',	"MILLISECONDS",	0, "Timeout for translator startup"},
  {"format",	'p',	"mount|fstab|translator", OPTION_ARG_OPTIONAL,
   "Output format for query (no filesystem arguments)"},
  {"options", 'o', "OPTIONS", 0, "A `,' separated list of options"},
  {"readonly", 'r', 0, 0, "Never write to disk or allow opens for writing"},
  {"writable", 'w', 0, 0, "Use normal read/write behavior"},
  {"update", 'u', 0, 0, "Flush any meta-data cached in core"},
  {"remount", 0, 0, OPTION_ALIAS},
  {"verbose", 'v', 0, 0, "Give more detailed information"},
  {"no-mtab", 'n', 0, 0, "Do not update /etc/mtab"},
  {"test-opts", 'O', "OPTIONS", 0,
   "Only mount fstab entries matching the given set of options"},
  {"bind", 'B', 0, 0, "Bind mount, firmlink"},
  {"firmlink", 0, 0, OPTION_ALIAS},
  {"fake", 'f', 0, 0, "Do not actually mount, just pretend"},
  {0, 0}
};

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  struct fstab_argp_params *params = state->input;
  error_t err;
  switch (key)
    {
    case ARGP_KEY_INIT:
      state->child_inputs[0] = params; /* pass down to fstab_argp parser */
      break;

#define ARGZ(call)							      \
      err = argz_##call; 						      \
      if (err)								      \
	argp_failure (state, 100, ENOMEM, "%s", arg);			      \
      break
    case 'r': ARGZ (add (&options, &options_len, "ro"));
    case 'w': ARGZ (add (&options, &options_len, "rw"));
    case 'u': ARGZ (add (&options, &options_len, "update"));
    case 'B': ARGZ (add (&options, &options_len, "bind"));
    case 'o': ARGZ (add_sep (&options, &options_len, arg, ','));
    case 'v': ++verbose; break;
#undef ARGZ

    case 'T':
      {
	char *end;
	unsigned long int ms = strtoul (arg, &end, 10);
	if (end && *end == '\0')
	  timeout = ms;
	else
	  {
	    argp_error (state,
			"--timeout needs a numeric argument (milliseconds)");
	    return EINVAL;
	  }
      }
      break;

    case 'p':
      if (arg == 0 || !strcasecmp (arg, "fstab"))
	query_format = qf_fstab;
      else if (!strcasecmp (arg, "mount"))
	query_format = qf_standard;
      else if (!strcasecmp (arg, "translator")
	       || !strcasecmp (arg, "showtrans"))
	query_format = qf_translator;
      else
	{
	  argp_error (state, "invalid argument to --format");
	  return EINVAL;
	}
      break;

    case 'n':
      /* do nothing */
      break;

    case 'f':
      fake = 1;
      break;

    case 'O':
      err = argz_create_sep (arg, ',', &test_opts, &test_opts_len);
      if (err)
	argp_failure (state, 100, ENOMEM, "%s", arg);
      break;

    case ARGP_KEY_ARG:
      if (mountpoint == 0)	/* One arg: mountpoint */
	mountpoint = arg;
      else if (device == 0)	/* Two args: device, mountpoint */
	{
	  device = mountpoint;
	  mountpoint = arg;
	}
      else			/* More than two args.  */
	{
	  argp_error (state, "too many arguments");
	  return EINVAL;
	}
      break;

    case ARGP_KEY_NO_ARGS:
      if (! params->do_all)
	{
	  params->do_all = 1;
	  mode = query;
	}
      break;

    case ARGP_KEY_END:
      if (params->do_all && mountpoint)
	{
	  argp_error (state, "filesystem argument not allowed with --all");
	  return EINVAL;
	}
      if (mode == query && options_len != 0)
	{
	  argp_error (state,
		      "mount options not allowed without filesystem argument");
	  return EINVAL;
	}
      if (mountpoint)
	switch (argz_count (params->types, params->types_len))
	  {
	  default:
	    argp_error (state,
			"multiple types not allowed with filesystem argument");
	    return EINVAL;
	  case 1:
	    fstype = params->types;
	    params->types = 0;
	    params->types_len = 0;
	    break;
	  case 0:
	    break;
	  }
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

static const char doc[] = "Start active filesystem translators";
static const char args_doc[] = "\
DEVICE\t(in " _PATH_MNTTAB ")\n\
DIRECTORY\t(in " _PATH_MNTTAB ")\n\
[-t TYPE] DEVICE DIRECTORY\n\
-a";
static const struct argp_child argp_kids[] =
{ { &fstab_argp, 0,
    "Filesystem selection (if no explicit filesystem arguments given):", 2 },
  { 0 } };
static struct argp argp = { argp_opts, parse_opt, args_doc, doc, argp_kids };

/* Mount one filesystem.  */
static error_t
do_mount (struct fs *fs, int remount)
{
  error_t err;
  char *fsopts, *o;
  size_t fsopts_len;
  char *mntopts;
  size_t mntopts_len;
  fsys_t mounted;

  inline void explain (const char *command)
    {
      if (verbose)
	{
	  const char *o;
	  printf ("%s %s", command, fs->mntent.mnt_dir);
	  for (o = fsopts; o; o = argz_next (fsopts, fsopts_len, o))
	    printf (" %s", o);
	  putchar ('\n');
	}
    }

  err = fs_fsys (fs, &mounted);
  if (err)
    {
      error (0, err, "cannot determine if %s is already mounted",
	     fs->mntent.mnt_fsname);
      return err;
    }


  /* Produce an argz of translator option arguments from the
     given FS's options and the command-line options.  */

#define ARGZ(call)							      \
  err = argz_##call;							      \
  if (err)								      \
    error (3, ENOMEM, "collecting mount options");			      \

  if (fs->mntent.mnt_opts)
    {
      /* Append the fstab options to any specified on the command line.  */
      ARGZ (create_sep (fs->mntent.mnt_opts, ',', &mntopts, &mntopts_len));

      /* Remove the `noauto' and `bind' options, since they're for us not the
         filesystem.  */
      for (o = mntopts; o; o = argz_next (mntopts, mntopts_len, o))
        {
          if (strcmp (o, MNTOPT_NOAUTO) == 0)
            argz_delete (&mntopts, &mntopts_len, o);
          if (strcmp (o, "bind") == 0)
            {
              fs->mntent.mnt_type = strdup ("firmlink");
              if (!fs->mntent.mnt_type)
                error (3, ENOMEM, "failed to allocate memory");
              argz_delete (&mntopts, &mntopts_len, o);
            }
        }

      ARGZ (append (&mntopts, &mntopts_len, options, options_len));
    }
  else
    {
      mntopts = options;
      mntopts_len = options_len;
    }

  /* Convert the list of options into a list of switch arguments.  */
  fsopts = 0;
  fsopts_len = 0;
  for (o = mntopts; o; o = argz_next (mntopts, mntopts_len, o))
    if (*o == '-')		/* Allow letter opts `-o -r,-E', BSD style.  */
      {
	ARGZ (add (&fsopts, &fsopts_len, o));
      }
    else if ((strcmp (o, "defaults") != 0) && (strlen (o) != 0))
      {
	/* Prepend `--' to the option to make a long option switch,
	   e.g. `--ro' or `--rsize=1024'.  */
	char arg[2 + strlen (o) + 1];
	arg[0] = arg[1] = '-';
	memcpy (&arg[2], o, sizeof arg - 2);
	ARGZ (add (&fsopts, &fsopts_len, arg));
      }

  if (mntopts != options)
    free (mntopts);
#undef ARGZ

  if (remount)
    {
      if (mounted == MACH_PORT_NULL)
	{
	  error (0, 0, "%s not already mounted", fs->mntent.mnt_fsname);
	  return EBUSY;
	}

      /* Send an RPC to request the new options, including --update.  */
      explain ("fsysopts");
      err = fsys_set_options (mounted, fsopts, fsopts_len, 0);
      if (err)
	error (0, err, "cannot remount %s", fs->mntent.mnt_fsname);
      return err;
    }
  else
    {
      /* Error during file lookup; we use this to avoid duplicating error
	 messages.  */
      error_t open_err = 0;
      /* The control port for any active translator we start up.  */
      fsys_t active_control;
      file_t node;		/* Port to the underlying node.  */
      struct fstype *type;

      /* The callback to start_translator opens NODE as a side effect.  */
      error_t open_node (int flags,
			 mach_port_t *underlying,
			 mach_msg_type_name_t *underlying_type,
			 task_t task, void *cookie)
	{
	  node = file_name_lookup (fs->mntent.mnt_dir,
				   flags | O_NOTRANS, 0666);
	  if (node == MACH_PORT_NULL)
	    {
	      open_err = errno;
	      return open_err;
	    }

	  *underlying = node;
	  *underlying_type = MACH_MSG_TYPE_COPY_SEND;

	  return 0;
	}

      /* Do not fail if there is an active translator if --fake is
         given. This mimics Linux mount utility more closely which
         just looks into the mtab file. */
      if (mounted != MACH_PORT_NULL && !fake)
	{
	  error (0, 0, "%s already mounted", fs->mntent.mnt_fsname);
	  return EBUSY;
	}

      if (strcmp (fs->mntent.mnt_type, "auto") == 0)
        {
#if HAVE_BLKID
          char *type =
            blkid_get_tag_value (NULL, "TYPE", fs->mntent.mnt_fsname);
          if (! type)
            {
              error (0, 0, "failed to detect file system type");
              return EFTYPE;
            }
          else
            {
	      if (strcmp (type, "vfat") == 0)
		fs->mntent.mnt_type = strdup ("fat");
	      else
		fs->mntent.mnt_type = strdup (type);
              if (! fs->mntent.mnt_type)
                error (3, ENOMEM, "failed to allocate memory");
            }
#else
	  fs->mntent.mnt_type = strdup ("ext2");
	  if (! fs->mntent.mnt_type)
	    error (3, ENOMEM, "failed to allocate memory");
#endif
        }

      err = fs_type (fs, &type);
      if (err)
	{
	  error (0, err, "%s: cannot determine filesystem type",
		 fs->mntent.mnt_fsname);
	  return err;
	}
      if (type->program == 0)
	{
	  error (0, 0, "%s: filesystem type `%s' unknown",
		 fs->mntent.mnt_fsname, type->name);
	  return EFTYPE;
	}

      /* Stick the translator program name in front of the option switches.  */
      err = argz_insert (&fsopts, &fsopts_len, fsopts, type->program);
      /* Now stick the device name on the end as the last argument.  */
      if (!err)
	err = argz_add (&fsopts, &fsopts_len, fs->mntent.mnt_fsname);
      if (err)
	error (3, ENOMEM, "collecting mount options");

      /* Now we have a translator command line argz in FSOPTS.  */

      if (fake) {
        /* Fake the translator startup. */
        mach_port_t underlying;
        mach_msg_type_name_t underlying_type;
        err = open_node (O_READ, &underlying, &underlying_type, 0, NULL);
        if (err)
          error (1, errno, "cannot mount on %s", fs->mntent.mnt_dir);

        mach_port_deallocate (mach_task_self (), underlying);

        /* See if the translator is at least executable. */
        if (access(type->program, X_OK) == -1)
          error (1, errno, "can not execute %s", type->program);

        return 0;
      }

      explain ("settrans -a");
      {
	mach_port_t ports[INIT_PORT_MAX];
	mach_port_t fds[STDERR_FILENO + 1];
	int ints[INIT_INT_MAX];
	int i;

	for (i = 0; i < INIT_PORT_MAX; i++)
	  ports[i] = MACH_PORT_NULL;
	for (i = 0; i < STDERR_FILENO + 1; i++)
	  fds[i] = MACH_PORT_NULL;
	memset (ints, 0, INIT_INT_MAX * sizeof(int));

	ports[INIT_PORT_CWDIR] = getcwdir ();
	ports[INIT_PORT_CRDIR] = getcrdir ();
	ports[INIT_PORT_AUTH] = getauth ();

	err = fshelp_start_translator_long (open_node, NULL,
					    fsopts, fsopts, fsopts_len,
					    fds, MACH_MSG_TYPE_COPY_SEND,
					    STDERR_FILENO + 1,
					    ports, MACH_MSG_TYPE_COPY_SEND,
					    INIT_PORT_MAX,
					    ints, INIT_INT_MAX,
					    geteuid (),
					    timeout, &active_control);
	for (i = 0; i < INIT_PORT_MAX; i++)
	  mach_port_deallocate (mach_task_self (), ports[i]);
	for (i = 0; i <= STDERR_FILENO; i++)
	  mach_port_deallocate (mach_task_self (), fds[i]);
      }
      /* If ERR is due to a problem opening the translated node, we print
	 that name, otherwise, the name of the translator.  */
      if (open_err)
	error (0, open_err, "cannot mount on %s", fs->mntent.mnt_dir);
      else if (err)
	error (0, err, "cannot start translator %s", fsopts);
      else
	{
	  err = file_set_translator (node, 0, FS_TRANS_SET|FS_TRANS_EXCL, 0,
				     0, 0,
				     active_control, MACH_MSG_TYPE_COPY_SEND);
	  if (err == EBUSY)
	    error (0, 0, "%s already mounted on", fs->mntent.mnt_dir);
	  else if (err)
	    error (0, err, "cannot set translator on %s", fs->mntent.mnt_dir);
	  if (err)
	    fsys_goaway (active_control, FSYS_GOAWAY_FORCE);
	  mach_port_deallocate (mach_task_self (), active_control);
	}

      return err;
    }
}

/* Report the state of one filesystem to stdout.  */
static error_t
do_query (struct fs *fs)
{
  error_t err;
  fsys_t fsys;
  char _opts[200], *opts = _opts;
  size_t opts_len = sizeof opts;
  size_t nopts;

  err = fs_fsys (fs, &fsys);
  if (err)
    return err;

  if (fsys == MACH_PORT_NULL)
    /* This filesystem is not mounted.  Nothing to report.  */
    return 0;

  err = fsys_get_options (fsys, &opts, &opts_len);
  if (err)
    {
      error (0, err, "%s: cannot get filesystem options",
	     fs->mntent.mnt_fsname);
      return err;
    }

  nopts = argz_count (opts, opts_len);
  if (nopts == 0)
    {
      error (0, 0, "%s: fsys_get_options returned empty string",
	     fs->mntent.mnt_dir);
      return EGRATUITOUS;
    }

  if (query_format == qf_translator)
    {
      argz_stringify (opts, opts_len, ' ');
      printf ("%s: %s\n", fs->mntent.mnt_dir, opts);
    }
  else
    {
      char *argv[nopts];
      const char *prog, *device;

      inline const char *fsopt_string (const char *opt) /* canonicalize */
	{
	  if (opt[0] == '-' && opt[1] == '-')
	    {
	      opt += 2;
	      if (!strcmp (opt, "readonly"))
		return "ro";
	      if (!strcmp (opt, "writable"))
		return "rw";
	    }
	  else
	    {
	      if (!strcmp (opt, "-r"))
		return "ro";
	      if (!strcmp (opt, "-w"))
		return "rw";
	    }
	  return opt;
	}
      inline void print_prog (const char *sfx)
	{
	  if (!strncmp (_HURD, prog, sizeof _HURD - 1))
	    {
	      const char *type = &prog[sizeof _HURD - 1];
	      size_t len = strlen (type);
	      if (!strcmp (&type[len - 2], "fs"))
		printf ("%.*s%s", (int) (len - 2), type, sfx);
	      else
		printf ("%s%s", type, sfx);
	    }
	  else
	    printf ("%s%s", prog, sfx);
	}
      inline int print_opts (char sep)
	{
	  char *opt = argz_next (opts, opts_len, prog);
	  if (opt == 0)
	    return 0;
	  fputs (fsopt_string (opt), stdout);
	  while ((opt = argz_next (opts, opts_len, opt)) != 0)
	    printf ("%c%s", sep, fsopt_string (opt));
	  return 1;
	}

      argz_extract (opts, opts_len, argv);
      prog = argv[0];

      if (nopts < 2)
	device = fs->mntent.mnt_fsname;
      else
	{
	  static const char type_opt[] = "--store-type=";
	  device = argv[--nopts];
	  opts_len -= strlen (device) + 1;
	  if (!strncmp (type_opt, argv[nopts - 1], sizeof type_opt - 1))
	    {
	      asprintf ((char **) &device, "%s:%s",
			&argv[nopts - 1][sizeof type_opt - 1], device);
	      opts_len -= strlen (argv[nopts - 1]) + 1;
	    }
	}

      switch (query_format)
	{
	case qf_standard:
	  printf ("%s on %s type ", device, fs->mntent.mnt_dir);
	  print_prog (" (");
	  if (print_opts (','))
	    puts (")");
	  else
	    puts ("defaults)");
	  break;

	case qf_fstab:
	  printf ("%s\t%s\t", device, fs->mntent.mnt_dir);
	  print_prog ("\t");
	  printf ("%s\t%d %d\n",
		  print_opts (',') ? "" : "defaults",
		  fs->mntent.mnt_freq, fs->mntent.mnt_passno);
	  break;

	case qf_translator:	/* impossible */
	  break;
	}
    }

  return 0;
}

int
main (int argc, char **argv)
{
  unsigned int remount, firmlink;
  struct fstab *fstab;
  struct fs *fs;
  error_t err;

  argp_parse (&argp, argc, argv, 0, 0, &fstab_params);

  if (!mountpoint && (fstab_params.types == 0

		      || !strncasecmp (fstab_params.types, "no", 2)))
    {
      /* Always add the type "swap" to the list of types to exclude.  */
      err = argz_add (&fstab_params.types, &fstab_params.types_len,
		      "no"MNTTYPE_SWAP);
      if (err)
	error (3, ENOMEM, "parsing arguments");
    }

  fstab = fstab_argp_create (&fstab_params, SEARCH_FMTS, sizeof SEARCH_FMTS);

  /* This is a convenient way of checking for any `remount' options.  */
  remount = 0;
  err = argz_replace (&options, &options_len, "remount", "update", &remount);
  if (err)
    error (3, ENOMEM, "collecting mount options");

  /* Do not pass `bind' option to firmlink translator */
  char *opt = NULL;
  firmlink = 0;
  while ((opt = argz_next (options, options_len, opt)))
    if (strcmp (opt, "bind") == 0)
      {
        firmlink = 1;
        argz_delete(&options, &options_len, opt);
      }

  if (device)			/* two-argument form */
    {
      struct mntent m =
      {
	mnt_fsname: device,
	mnt_dir: mountpoint,
	mnt_type: fstype,
	mnt_opts: 0,
	mnt_freq: 0, mnt_passno: 0
      };
      if (firmlink)
        m.mnt_type = strdup ("firmlink");

      err = fstab_add_mntent (fstab, &m, &fs);
      if (err)
	error (2, err, "%s", mountpoint);
    }
  else if (mountpoint && remount)	/* one-argument remount */
    {
      struct mntent m =
      {
	mnt_fsname: mountpoint, /* since we cannot know the device,
				   using mountpoint here leads to more
				   helpful error messages */
	mnt_dir: mountpoint,
	mnt_type: fstype,
	mnt_opts: 0,
	mnt_freq: 0, mnt_passno: 0
      };
      if (firmlink)
        m.mnt_type = strdup ("firmlink");

      err = fstab_add_mntent (fstab, &m, &fs);
      if (err)
	error (2, err, "%s", mountpoint);
    }
  else if (mountpoint)		/* one-argument form */
    {
      fs = fstab_find (fstab, mountpoint);
      if (!fs)
	error (2, 0, "%s: Unknown device or filesystem", mountpoint);
    }
  else
    fs = 0;

  if (fs != 0)
    err = do_mount (fs, remount);
  else
    switch (mode)
      {
      case mount:
	for (fs = fstab->entries; fs; fs = fs->next)
	  {
	    if (fstab_params.do_all) {
              if (hasmntopt (&fs->mntent, MNTOPT_NOAUTO))
                continue;

              if (! match_options (&fs->mntent))
                continue;

              fsys_t mounted;
              err = fs_fsys (fs, &mounted);
              if (err)
                  error (0, err, "cannot determine if %s is already mounted",
                         fs->mntent.mnt_fsname);

              if (mounted != MACH_PORT_NULL)
                continue;
            }
	    err |= do_mount (fs, remount);
	  }
	break;

      case query:
	for (fs = fstab->entries; fs; fs = fs->next)
	  err |= do_query (fs);
	break;
      }

  return err ? 1 : 0;
}
