/*
   Copyright (C) 2017 Free Software Foundation, Inc.

   Written by Miles Bader <address@hidden>

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
   along with the GNU Hurd.  If not, see <<a rel="nofollow" href="http://www.gnu.org/licenses/">http://www.gnu.org/licenses/</a>>.
*/

/* Fsysopts and command line option parsing */

#include <options.h>

#include <stdlib.h>
#include <argp.h>
#include <argz.h>
#include <error.h>

#include <acpifs.h>

/* Option parser */
static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  error_t err = 0;
  struct parse_hook *h = state->hook;

  /* Return _ERR from this routine */
#define RETURN(_err)                          \
  do { return _err; } while (0)

  /* Print a parsing error message and (if exiting is turned off) return the
     error code ERR.  */
#define PERR(err, fmt, args...)               \
  do { argp_error (state, fmt , ##args); RETURN (err); } while (0)

  /* Like PERR but for non-parsing errors.  */
#define FAIL(rerr, status, perr, fmt, args...)  \
  do{ argp_failure (state, status, perr, fmt , ##args); RETURN (rerr); } while(0)

  if (!arg && state->next < state->argc && (*state->argv[state->next] != '-'))
    {
      arg = state->argv[state->next];
      state->next++;
    }

  switch (opt)
    {
    case 'U':
      h->perm.uid = atoi (arg);
      break;
    case 'G':
      h->perm.gid = atoi (arg);
      break;

    case ARGP_KEY_INIT:
      /* Initialize our parsing state.  */
      h = malloc (sizeof (struct parse_hook));
      if (!h)
        FAIL (ENOMEM, 1, ENOMEM, "option parsing");

      h->ncache_len = NODE_CACHE_MAX;
      h->perm.uid = 0;
      h->perm.gid = 0;
      state->hook = h;
      break;

    case ARGP_KEY_SUCCESS:
      /* Set permissions to FS */
      fs->perm = h->perm;

      /* Set cache len */
      fs->node_cache_max = h->ncache_len;

      if (fs->root)
       {
         /*
          * FS is already initialized, that means we've been called by fsysopts.
          * Update permissions.
          */

         /* Don't accept new RPCs during this process */
         err = ports_inhibit_all_rpcs ();
         if (err)
           return err;

         err = fs_set_permissions (fs);

         /* Accept RPCs again */
         ports_resume_all_rpcs ();
       }

      /* Free the hook */
      free (h);

      break;

    case ARGP_KEY_ERROR:
      /* Parsing error occurred, free the permissions. */
      free (h);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  return err;
}

/*
 * Print current permissions. Called by fsysopts.
 */
error_t
netfs_append_args (char **argz, size_t * argz_len)
{
  error_t err = 0;
  struct acpifs_perm *p;

#define ADD_OPT(fmt, args...)           \
  do { char buf[100];                   \
       if (! err) {                     \
         snprintf (buf, sizeof buf, fmt , ##args);      \
         err = argz_add (argz, argz_len, buf); } } while (0)

  p = &fs->perm;
  if (p->uid >= 0)
    ADD_OPT ("--uid=%u", p->uid);
  if (p->gid >= 0)
    ADD_OPT ("--gid=%u", p->gid);

#undef ADD_OPT
  return err;
}

struct argp acpi_argp = { options, parse_opt, 0, doc };

struct argp *netfs_runtime_argp = &acpi_argp;
