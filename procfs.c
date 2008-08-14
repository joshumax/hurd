/* procfs -- a translator for providing GNU/Linux compatible 
             proc pseudo-filesystem
             
   procfs.c -- This file is the main file of the translator.
               This has important definitions and initializes
               the translator
               
   Copyright (C) 2008, FSF.
   Written as a Summer of Code Project
   
   procfs is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   procfs is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. 
*/

#include <stdio.h>
#include <argp.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <error.h>
#include <sys/stat.h>
#include <hurd/netfs.h>

#include "procfs.h"

/* Defines this Tanslator Name */
char *netfs_server_name = PROCFS_SERVER_NAME;
char *netfs_server_version = PROCFS_SERVER_VERSION;
int netfs_maxsymlinks = 12;

static const struct argp_child argp_children[] = 
  {
    {&netfs_std_startup_argp, 0, NULL, 0},
    {0}
  };


const char *argp_program_version = "/proc pseudo-filesystem (" PROCFS_SERVER_NAME
 ") " PROCFS_SERVER_VERSION "\n"
"Copyright (C) 2008 Free Software Foundation\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE."
"\n";

static char *args_doc = "PROCFSROOT";
static char *doc = "proc pseudo-filesystem for Hurd implemented as a translator. "
"This is still under very humble and initial stages of development.\n"
"Any Contribution or help is welcome. The code may not even compile";


/* The Filesystem */
struct procfs *procfs;

/* The FILESYSTEM component of PROCFS_FS.  */
char *procfs_root = "";

volatile struct mapped_time_value *procfs_maptime;

/* Startup options.  */
static const struct argp_option procfs_options[] = 
  {
    { 0 }
  };

  
/* argp parser function for parsing single procfs command line options  */  
static error_t
parse_procfs_opt (int key, char *arg, struct argp_state *state)
{
  switch (key) 
    {
    case ARGP_KEY_ARG:
      if (state->arg_num > 1) 
        argp_usage (state);
      break;
      
    case ARGP_KEY_NO_ARGS:
      argp_usage(state);
      break;
      
    default:
      return ARGP_ERR_UNKNOWN;
    }
}

/* Program entry point. */
int 
main (int argc, char **argv)
{
  error_t err;
  mach_port_t bootstrap, underlying_node;
  struct stat underlying_stat;
  
  struct argp argp = 
    {
      procfs_options, parse_procfs_opt,
      args_doc, doc, argp_children,
      NULL, NULL  
    }; 
    
   
  /* Parse the command line arguments */
//  argp_parse (&argp, argc, argv, 0, 0, 0);

  task_get_bootstrap_port (mach_task_self (), &bootstrap);

  netfs_init ();
        
  if (maptime_map (0, 0, &procfs_maptime)) 
    {
      perror (PROCFS_SERVER_NAME ": Cannot map time");
      return 1;
    }
  
  procfs_init ();

  err = procfs_create (procfs_root, getpid (), &procfs);
  if (err)
    error (4, err, "%s", procfs_root);
     
  /* Create our root node */
  netfs_root_node = procfs->root;

  /* Start netfs activities */  
  underlying_node = netfs_startup (bootstrap, 0);
  if (io_stat (underlying_node, &underlying_stat))
    error (1, err, "cannot stat underling node");

  /* Initialize stat information of the root node.  */
  netfs_root_node->nn_stat = underlying_stat;
  netfs_root_node->nn_stat.st_mode =
    S_IFDIR | (underlying_stat.st_mode & ~S_IFMT & ~S_ITRANS);
  
  for (;;)  
    netfs_server_loop ();
  return 1;
}
