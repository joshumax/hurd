/* 
   Copyright (C) 1996 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <hurd/netfs.h>
#include <sys/socket.h>
#include <stdio.h>
#include <device/device.h>
#include "nfs.h"
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <maptime.h>
#include <argp.h>

#define DEFAULT_SOFT_RETRIES  3	/* times */
#define DEFAULT_STAT_TIMEOUT  3	/* seconds */
#define DEFAULT_CACHE_TIMEOUT 3	/* seconds */
#define DEFAULT_READ_SIZE     8192 /* bytes */
#define DEFAULT_WRITE_SIZE    8192 /* bytes */

int stat_timeout = DEFAULT_STAT_TIMEOUT;
int cache_timeout = DEFAULT_CACHE_TIMEOUT;
int initial_transmit_timeout = 1;
int max_transmit_timeout = 30;
int soft_retries = DEFAULT_SOFT_RETRIES;
int mounted_soft = 1;
int read_size = DEFAULT_READ_SIZE;
int write_size = DEFAULT_WRITE_SIZE;

#define OPT_SOFT	's'
#define OPT_HARD	'h'
#define OPT_RSIZE	'r'
#define OPT_WSIZE	'w'
#define OPT_STAT_TO	-2
#define OPT_CACHE_TO	-3
#define OPT_INIT_TR_TO	-4
#define OPT_MAX_TR_TO	-5
#define OPT_MNT_PORT    -6
#define OPT_MNT_PORT_D  -7
#define OPT_NFS_PORT    -8
#define OPT_NFS_PORT_D  -9
#define OPT_HOLD	-10
#define OPT_MNT_PROG    -11
#define OPT_NFS_PROG    -12
#define OPT_PMAP_PORT	-13

/* Return a string corresponding to the printed rep of DEFAULT_what */
#define ___D(what) #what
#define __D(what) ___D(what)
#define _D(what) __D(DEFAULT_ ## what)

static struct argp_option options[] = {
  {0,0,0,0,0,1},
  {"soft",		    OPT_SOFT,	   "RETRIES", OPTION_ARG_OPTIONAL,
     "File system requests will eventually fail, after RETRIES tries if"
     " specified, otherwise " _D(SOFT_RETRIES)},
  {"hard",		    OPT_HARD, 0, 0,
     "Retry file systems requests until they succeed"},

  {0,0,0,0,0,2},
  {"read-size",		    OPT_RSIZE,	   "BYTES", 0,
     "Max packet size for reads (default " _D(READ_SIZE) ")"},
  {"rsize",0,0,OPTION_ALIAS},
  {"write-size",	    OPT_WSIZE,	   "BYTES", 0,
     "Max packet size for writes (default " _D(WRITE_SIZE)")"},
  {"wsize",0,0,OPTION_ALIAS},

  {0,0,0,0,"Timeouts:",3},
  {"stat-timeout",	    OPT_STAT_TO,   "SEC", 0,
     "Timeout for cached stat information (default " _D(STAT_TIMEOUT) ")"},
  {"cache-timeout",	    OPT_CACHE_TO,  "SEC", 0,
     "Timeout for cached file data (default " _D(CACHE_TIMEOUT) ")"},
  {"init-transmit-timeout", OPT_INIT_TR_TO,"SEC", 0}, 
  {"max-transmit-timeout",  OPT_MAX_TR_TO, "SEC", 0}, 

  {0,0,0,0,"Server specification:",4},
  {"mount-port",	    OPT_MNT_PORT,  "PORT", 0,
     "Port for mount server"},
  {"default-mount-port",    OPT_MNT_PORT_D,"PORT", 0,
     "Port for mount server, if none can be found automatically"},
  {"mount-program",	    OPT_MNT_PROG,  "ID[.VERS]"},

  {"nfs-port",	            OPT_NFS_PORT,  "PORT", 0,
     "Port for nfs operations"},
  {"default-nfs-port",      OPT_NFS_PORT_D,"PORT", 0,
     "Port for nfs operations, if none can be found automatically"},
  {"nfs-program",           OPT_NFS_PROG,  "ID[.VERS]"},

  {"pmap-port",             OPT_PMAP_PORT,  "SVC|PORT"},

  {"hold", OPT_HOLD, 0, OPTION_HIDDEN}, /*  */
  { 0 }
};
static char *args_doc = "REMOTE_FS [HOST]";
static char *doc = "If HOST is not specified, an attempt is made to extract"
" it from REMOTE_FS, using either the `HOST:FS' or `FS@HOST' notations.";

int
main (int argc, char **argv)
{
  mach_port_t bootstrap;
  static volatile int hold = 0;
  struct sockaddr_in addr;
  int ret;
  char *remote_fs = 0;
  char *host = 0;

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case OPT_SOFT:
	  mounted_soft = 1;
	  if (arg)
	    soft_retries = atoi (arg);
	  break;
	case OPT_HARD:
	  mounted_soft = 0;
	  break;

	case OPT_RSIZE: read_size = atoi (arg); break;
	case OPT_WSIZE: write_size = atoi (arg); break;

	case OPT_STAT_TO: stat_timeout = atoi (arg); break;
	case OPT_CACHE_TO: cache_timeout = atoi (arg); break;
	case OPT_INIT_TR_TO: initial_transmit_timeout = atoi (arg); break;
	case OPT_MAX_TR_TO: max_transmit_timeout = atoi (arg); break;

	case OPT_MNT_PORT:
	  mount_port_override = 1;
	  /* fall through */
	case OPT_MNT_PORT_D:
	  mount_port = atoi (arg);
	  break;

	case OPT_NFS_PORT:
	  nfs_port_override = 1;
	  /* fall through */
	case OPT_NFS_PORT_D:
	  nfs_port = atoi (arg);
	  break;

	case OPT_HOLD: hold = 1; break;

	case ARGP_KEY_ARG:
	  if (state->arg_num == 0)
	    remote_fs = arg;
	  else if (state->arg_num == 1)
	    host = arg;
	  else
	    return EINVAL;
	  break;

	case ARGP_KEY_END:
	  if (! host)
	    /* Try and extract HOST from REMOTE_FS.  */
	    {
	      char *sep = index (remote_fs, ':');
	      if (sep)
		{
		  *sep++ = '\0';
		  host = remote_fs;
		  remote_fs = sep;
		  break;
		}

	      sep = index (remote_fs, '@');
	      if (sep)
		{
		  *sep++ = '\0';
		  host = sep;
		  break;
		}

	      argp_error (state->argp, "No HOST specified");
	    }
	  break;

	case ARGP_KEY_NO_ARGS:
	  argp_error (state->argp, "No REMOTE_FS specified");

	default:
	  return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, args_doc, doc};

  argp_parse (&argp, argc, argv, 0, 0);

  while (hold);
    
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  netfs_init ();
  
  main_udp_socket = socket (PF_INET, SOCK_DGRAM, 0);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons (IPPORT_RESERVED);
  do
    {
      addr.sin_port = htons (ntohs (addr.sin_port) - 1);
      ret = bind (main_udp_socket, (struct sockaddr *)&addr, 
		  sizeof (struct sockaddr_in));
      if (ret == -1 && errno == EPERM)
	{
	  /* We aren't allowed privileged ports; no matter;
	     let the server deny us later if it wants. */
	  ret = 0;
	  break;
	}
    }
  while ((ret == -1) && (errno == EADDRINUSE));
  if (ret == -1)
    {
      perror ("binding main udp socket");
      exit (1);
    }

  errno = maptime_map (0, 0, &mapped_time);
  if (errno)
    perror ("mapping time");

  cthread_detach (cthread_fork ((cthread_fn_t) timeout_service_thread, 0));
  cthread_detach (cthread_fork ((cthread_fn_t) rpc_receive_thread, 0));
  
  hostname = malloc (1000);
  gethostname (hostname, 1000);
  netfs_root_node = mount_root (remote_fs, host);

  if (!netfs_root_node)
    exit (1);
  
  netfs_startup (bootstrap, 0);
  
  for (;;)
    netfs_server_loop ();
}

