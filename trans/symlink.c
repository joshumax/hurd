/* Translator for S_IFLNK nodes
   Copyright (C) 1994 Free Software Foundation

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

mach_port_t realnode;

/* We return this for O_NOLINK lookups */
mach_port_t realnodenoauth;

/* We return this for non O_NOLINK lookups */
char *linktarget;

main (int argc, char **argv)
{
  mach_port_t bootstrap;
  mach_port_t control;
  
  _libports_initialize ();
  
  task_get_bootstrap_port (mach_task_self (), &bootstrap);
  if (bootstrap == MACH_PORT_NULL)
    {
      fprintf (stderr, "%s must be started as a translator\n", argv[0]);
      exit (1);
    }
  
  if (argc != 2)
    {
      fprintf (stderr, "Usage: %s link-target\n", argv[0]);
      exit (1);
    }

  linktarget = argv[1];

  /* Reply to our parent */
  control = ports_allocate_port (PT_CTL, sizeof (struct port_info));
  error = fsys_startup (bootstrap, ports_get_right (control),
			MACH_MSG_TYPE_MAKE_SEND, &realnode);

  io_restrict_auth (realnode, &realnodenoauth, 0, 0, 0, 0);

  /* Launch */
  ports_manage_port_operations_onethread ();
  return 0;
}

S_fsys_getroot (mach_port_t fsys_t,
		mach_port_t dotdotnode,
		uid_t *uids,
		u_int nuids,
		uid_t *gids,
		u_int ngids,
		int flags,
		retry_type *do_retry,
		char *retry_name,
		mach_port_t *ret,
		mach_port_name_t *rettype)
{
  if (flags & O_NOLINK)
    {
      /* Return our underlying node. */
      *ret = realnodenoauth;
      *rettype = MACH_MSG_TYPE_COPY_SEND;
      *do_retry = FS_RETRY_REAUTH;
      retry_name[0] = '\0';
      return 0;
    }
  else
    {
      /* Return telling the user to follow the link */
      strcpy (retry_name, linktarget);
      if (linktarget[0] == '/')
	{
	  *do_retry = FS_RETRY_MAGICAL;
	  *ret = MACH_PORT_NULL;
	  *rettype = MACH_MSG_TYPE_COPY_SEND;
	}
      else
	{
	  *do_retry = FS_RETRY_REAUTH;
	  *ret = dotdotnode;
	  *rettype = MACH_MSG_TYPE_MOVE_SEND;
	}
    }
  return 0;
}
