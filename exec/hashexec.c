/* GNU Hurd standard exec server, #! script execution support.
   Copyright (C) 1995, 1996, 1997, 1998, 1999, 2000, 2002, 2010
   Free Software Foundation, Inc.
   Written by Roland McGrath.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include "priv.h"
#include <hurd/sigpreempt.h>
#include <unistd.h>
#include <envz.h>
#include <sys/param.h>

/* This is called to check E for a #! interpreter specification.  E has
   already been prepared (successfully) and checked (unsuccessfully).  If
   we return success, our caller just returns success for the RPC; we must
   handle all the RPC argument details ourselves.  If we return ENOEXEC, we
   should leave everything as it was.  If we return failure other than
   ENOEXEC, our caller will just fail the RPC.  */
void
check_hashbang (struct execdata *e,
		file_t file,
		task_t oldtask,
		int flags,
		char *file_name_exec,
		char *argv, u_int argvlen, boolean_t argv_copy,
		char *envp, u_int envplen, boolean_t envp_copy,
		mach_port_t *dtable, u_int dtablesize, boolean_t dtable_copy,
		mach_port_t *portarray, u_int nports, boolean_t portarray_copy,
		int *intarray, u_int nints, boolean_t intarray_copy,
		mach_port_t *deallocnames, u_int ndeallocnames,
		mach_port_t *destroynames, u_int ndestroynames)
{
  char *p;
  char *interp, *arg;		/* Interpreter file name, and first argument */
  size_t interp_len, arg_len;
  file_t interp_file;		/* Port open on the interpreter file.  */
  char *new_argv;
  size_t new_argvlen;
  mach_port_t *new_dtable = NULL;
  u_int new_dtablesize;

  file_t user_fd (int fd)
    {
      if (fd >= 0 && fd < dtablesize)
	{
	  const file_t dport = dtable[fd];
	  if (dport != MACH_PORT_NULL)
	    {
	      mach_port_mod_refs (mach_task_self (), dport,
				  MACH_PORT_RIGHT_SEND, +1);
	      return dport;
	    }
	}
      errno = EBADF;
      return MACH_PORT_NULL;
    }
  file_t user_crdir, user_cwdir;
  error_t user_port (int which, error_t (*operate) (mach_port_t))
    {
      error_t reauthenticate (file_t unauth, file_t *result)
	{
	  error_t err;
	  mach_port_t ref;

	  /* MAKE_SEND is safe here because we destroy REF ourselves. */

	  error_t uauth (auth_t auth)
	    {
	      return auth_user_authenticate (auth,
					     ref, MACH_MSG_TYPE_MAKE_SEND,
					     result);
	    }
	  if (*result != MACH_PORT_NULL)
	    return 0;
	  ref = mach_reply_port ();
	  err = io_reauthenticate (unauth, ref, MACH_MSG_TYPE_MAKE_SEND);
	  if (!err)
	    err = user_port (INIT_PORT_AUTH, &uauth);
	  mach_port_destroy (mach_task_self (), ref);
	  return err;
	}

      /* Find the specified port, using defaults if so specified.  */
      mach_port_t port = ((which < nports &&
			   portarray[which] != MACH_PORT_NULL)
			  ? portarray[which] :
			  (flags & EXEC_DEFAULTS && which < std_nports)
				? std_ports[which]
				: MACH_PORT_NULL);

      /* Reauthenticate dir ports if they are the defaults.  */
      switch (which)
	{
	case INIT_PORT_CRDIR:
	  /* If secure, always use the default root.  */
	  if ((which < std_nports && flags & EXEC_SECURE) ||
	      (which < std_nports && port == std_ports[which]))
	    return (reauthenticate (std_ports[which], &user_crdir) ?:
		    (*operate) (user_crdir));
	  break;
	case INIT_PORT_CWDIR:
	  /* If secure, reauthenticate cwd whether default or given.  */
	  if ((flags & EXEC_SECURE) ||
	      (which < std_nports && port == std_ports[which]))
	    return (reauthenticate (port, &user_cwdir) ?:
		    (*operate) (user_cwdir));
	  break;
	}

      return (*operate) (port);
    }
  /* Look up NAME on behalf of the client.  */
  inline error_t lookup (const char *name, int flags, mach_port_t *result)
    {
      return hurd_file_name_lookup (&user_port, &user_fd, 0,
				    name, flags, 0, result);
    }

  const char *page;
  char interp_buf[vm_page_size - 2 + 1];

  e->error = 0;
  page = map (e, 0, 2);

  if (!page)
    {
      if (!e->error)
	e->error = ENOEXEC;
      return;
    }

  /* Check for our ``magic number''--"#!".  */
  if (page[0] != '#' || page[1] != '!')
    {
      /* These are not the droids we're looking for.  */
      e->error = ENOEXEC;
      return;
    }

  /* Read the rest of the first line of the file.
     We in fact impose an arbitrary limit of about a page on this.  */

  p = memccpy (interp_buf, page + 2, '\n',
	       MIN (map_fsize (e) - 2, sizeof interp_buf));
  if (p == NULL)
    {
      /* The first line went on for more than sizeof INTERP_BUF!  */
      interp_len = sizeof interp_buf;
      interp_buf[interp_len - 1] = '\0';
    }
  else
    {
      interp_len = p - interp_buf; /* Includes null terminator.  */
      *--p = '\0';		/* Kill the newline.  */
    }

  /* We are now done reading the script file.  */
  finish (e, 0);


  /* Find the name of the interpreter.  */
  interp = interp_buf + strspn (interp_buf, " \t");
  p = strpbrk (interp, " \t");

  if (p)
    {
      /* Terminate the interpreter name.  */
      *p++ = '\0';

      /* Skip remaining blanks, and the rest of the line is the argument.  */

      arg = p + strspn (p, " \t");
      arg_len = interp_len - 1 - (arg - interp_buf); /* without null here */
      interp_len = p - interp; /* This one includes the null.  */

      if (arg_len == 0)
	arg = NULL;
      else
	{
	  /* Trim trailing blanks after the argument.  */
	  size_t i = arg_len - 1;
	  while (arg[i] == ' ' || arg[i] == '\t')
	    arg[i--] = '\0';
	  arg_len = i + 2;	/* Include the terminating null.  */
	}
    }
  else
    {
      /* There is no argument.  */
      arg = NULL;
      arg_len = 0;
      interp_len -= interp - interp_buf; /* Account for blanks skipped.  */
    }

  user_crdir = user_cwdir = MACH_PORT_NULL;

  pthread_rwlock_rdlock (&std_lock);

  /* Open a port on the interpreter file.  */
  e->error = lookup (interp, O_EXEC, &interp_file);

  if (! e->error)
    {
      int free_file_name = 0; /* True if we should free FILE_NAME.  */
      jmp_buf args_faulted;
      void fault_handler (int signo)
	{ longjmp (args_faulted, 1); }
      error_t setup_args (struct hurd_signal_preemptor *preemptor)
	{
	  size_t namelen;
	  char * volatile file_name = NULL;

	  if (setjmp (args_faulted))
	    file_name = NULL;
	  else if (! (flags & EXEC_SECURE))
	    {
	      /* Try to figure out the file's name.  If FILE_NAME_EXEC
		 is not NULL and not the empty string, then it's the
		 file's name.  Otherwise we guess that if ARGV[0]
		 contains a slash, it might be the name of the file;
		 and that if it contains no slash, looking for files
		 named by ARGV[0] in the `PATH' environment variable
		 might find it.  */

	      error_t error;
	      char *name;
	      int free_name = 0; /* True if we should free NAME. */
	      file_t name_file;
	      mach_port_t fileid, filefsid;
	      ino_t fileno;

	      /* Search $PATH for NAME, opening a port NAME_FILE on it.
		 This is encapsulated in a function so we can catch faults
		 reading the user's environment.  */
	      error_t search_path (struct hurd_signal_preemptor *preemptor)
		{
		  error_t err;
		  char *path = envz_get (envp, envplen, "PATH"), *pfxed_name;

		  if (! path)
		    {
		      const size_t len = confstr (_CS_PATH, NULL, 0);
		      path = alloca (len);
		      confstr (_CS_PATH, path, len);
		    }

		  err = hurd_file_name_path_lookup (user_port, user_fd, 0,
						    name, path, O_EXEC, 0,
						    &name_file, &pfxed_name);
		  if (!err && pfxed_name)
		    {
		      name = pfxed_name;
		      free_name = 1;
		    }

		  return err;
		}

	      if (file_name_exec && file_name_exec[0] != '\0')
		name = file_name_exec;
	      else
		{
		  /* Try to locate the file.  */
		  error = io_identity (file, &fileid, &filefsid, &fileno);
		  if (error)
		    goto out;
		  mach_port_deallocate (mach_task_self (), filefsid);

		  if (memchr (argv, '\0', argvlen) == NULL)
		    {
		      name = alloca (argvlen + 1);
		      memcpy (name, argv, argvlen);
		      name[argvlen] = '\0';
		    }
		  else
		    name = argv;

		  if (strchr (name, '/') != NULL)
		    error = lookup (name, 0, &name_file);
		  else if ((error = hurd_catch_signal
			    (sigmask (SIGBUS) | sigmask (SIGSEGV),
			     (vm_address_t) envp, (vm_address_t) envp + envplen,
			     &search_path, SIG_ERR)))
		    name_file = MACH_PORT_NULL;

		  /* See whether we found the right file.  */
		  if (!error && name_file != MACH_PORT_NULL)
		    {
		      mach_port_t id, fsid;
		      ino_t ino;
		      error = io_identity (name_file, &id, &fsid, &ino);
		      mach_port_deallocate (mach_task_self (), name_file);
		      if (!error)
			{
			  mach_port_deallocate (mach_task_self (), fsid);
			  mach_port_deallocate (mach_task_self (), id);
			  if (id != fileid)
			    error = 1;
			}
		    }

		  mach_port_deallocate (mach_task_self (), fileid);
		}

	      if (!error)
		{
		  file_name = name;
		  free_file_name = free_name;
		}
	      else if (free_name)
		free (name);
	    }

	  if (file_name == NULL)
	    {
	      /* We can't easily find the file.
		 Put it in a file descriptor and pass /dev/fd/N.  */
	      int fd;
	    out:

	      for (fd = 0; fd < dtablesize; ++fd)
		if (dtable[fd] == MACH_PORT_NULL)
		  break;
	      if (fd == dtablesize)
		{
		  /* Extend the descriptor table.  */
		  new_dtable = alloca ((dtablesize + 1) * sizeof (file_t));
		  memcpy (new_dtable, dtable, dtablesize * sizeof (file_t));
		  new_dtablesize = dtablesize + 1;
		  new_dtable[fd] = file;
		}
	      else
		dtable[fd] = file;
	      mach_port_mod_refs (mach_task_self (), file,
				  MACH_PORT_RIGHT_SEND, +1);

	      file_name = alloca (100);
	      sprintf (file_name, "/dev/fd/%d", fd);
	    }

	  /* Prepare the arguments to pass to the interpreter from the original
	     arguments and the name of the script file.  The args will look
	     like `INTERP {ARG} FILE_NAME ARGV[1..n]' (ARG might have been
	     omitted). */

	  namelen = strlen (file_name) + 1;

	  new_argvlen
	    = (argvlen - strlen (argv) - 1) /* existing args - old argv[0] */
	    + interp_len + arg_len + namelen; /* New args */

	  new_argv = mmap (0, new_argvlen, PROT_READ|PROT_WRITE,
			   MAP_ANON, 0, 0);
	  if (new_argv == (caddr_t) -1)
	    {
	      e->error = errno;
	      return e->error;
	    }
	  else
	    e->error = 0;

	  if (! setjmp (args_faulted))
	    {
	      char *other_args;

	      p = new_argv;

	      /* INTERP */
	      memcpy (p, interp, interp_len);
	      p += interp_len;

	      /* Maybe ARG */
	      if (arg)
		{
		  memcpy (p, arg, arg_len);
		  p += arg_len;
		}

	      /* FILE_NAME */
	      memcpy (p, file_name, namelen);
	      p += namelen;

	      /* Maybe remaining args */
	      other_args = argv + strlen (argv) + 1;
	      if (other_args - argv < argvlen)
		memcpy (p, other_args, argvlen - (other_args - argv));
	    }
	  else
	    {
	      /* We got a fault reading ARGV.  So don't use it.  */
	      char *n = stpncpy (new_argv,
				 "**fault in exec server reading argv[0]**",
				 argvlen);
	      memcpy (memcpy (n, arg, arg_len) + arg_len, file_name, namelen);
	    }

	  if (free_file_name)
	    free (file_name);

	  return 0;
	}

      /* Set up the arguments.  */
      hurd_catch_signal (sigmask (SIGSEGV) | sigmask (SIGBUS),
			 (vm_address_t) argv, (vm_address_t) argv + argvlen,
			 &setup_args, &fault_handler);
    }

  pthread_rwlock_unlock (&std_lock);

  if (user_crdir != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), user_crdir);
  if (user_cwdir != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), user_cwdir);

  if (e->error)
    /* We cannot open the interpreter file to execute it.  Lose!  */
    return;

#ifdef HAVE_FILE_EXEC_PATHS
  /* Execute the interpreter program.  */
  e->error = file_exec_paths (interp_file,
			      oldtask, flags, interp, interp,
			      new_argv, new_argvlen, envp, envplen,
			      new_dtable ?: dtable,
			      MACH_MSG_TYPE_COPY_SEND,
			      new_dtable ? new_dtablesize : dtablesize,
			      portarray, MACH_MSG_TYPE_COPY_SEND, nports,
			      intarray, nints,
			      deallocnames, ndeallocnames,
			      destroynames, ndestroynames);
  /* For backwards compatibility.  Just drop it when we kill file_exec.  */
  if (e->error == MIG_BAD_ID)
#endif
    e->error = file_exec (interp_file,
			  oldtask, flags,
			  new_argv, new_argvlen, envp, envplen,
			  new_dtable ?: dtable, MACH_MSG_TYPE_COPY_SEND,
			  new_dtable ? new_dtablesize : dtablesize,
			  portarray, MACH_MSG_TYPE_COPY_SEND, nports,
			  intarray, nints,
			  deallocnames, ndeallocnames,
			  destroynames, ndestroynames);


  mach_port_deallocate (mach_task_self (), interp_file);
  munmap (new_argv, new_argvlen);

  if (! e->error)
    {
      /* The exec of the interpreter succeeded!  Deallocate the resources
	 we passed to that exec.  We don't need to save them in a bootinfo
	 structure; the exec of the interpreter takes care of that.  */
      u_int i;
      mach_port_deallocate (mach_task_self (), file);
      task_resume (oldtask);	/* Our caller suspended it.  */
      mach_port_deallocate (mach_task_self (), oldtask);
      if (! argv_copy)
	munmap (argv, argvlen);
      if (! envp_copy)
	munmap (envp, envplen);
      for (i = 0; i < dtablesize; ++i)
	if (MACH_PORT_VALID (dtable[i]))
	  mach_port_deallocate (mach_task_self (), dtable[i]);
      if (! dtable_copy)
	munmap (dtable, dtablesize * sizeof *dtable);
      for (i = 0; i < nports; ++i)
	mach_port_deallocate (mach_task_self (), portarray[i]);
      if (! portarray_copy)
	munmap (portarray, nports * sizeof *portarray);
      if (! intarray_copy)
	munmap (intarray, nints * sizeof *intarray);
    }
}
