/* GNU Hurd standard exec server, #! script execution support.
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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

extern error_t hurd_file_name_path_lookup (error_t (*use_init_port)
					   (int which,
					    error_t (*operate) (mach_port_t)),
					   file_t (*get_dtable_port) (int fd),
					   const char *file_name,
					   const char *path,
					   int flags, mode_t mode,
					   file_t *result,
					   char **prefixed_name);

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
		char *argv, u_int argvlen, boolean_t argv_copy,
		char *envp, u_int envplen, boolean_t envp_copy,
		mach_port_t *dtable, u_int dtablesize, boolean_t dtable_copy,
		mach_port_t *portarray, u_int nports, boolean_t portarray_copy,
		int *intarray, u_int nints, boolean_t intarray_copy,
		mach_port_t *deallocnames, u_int ndeallocnames,
		mach_port_t *destroynames, u_int ndestroynames)
{
  char *ibuf = NULL;
  size_t ibufsiz = 0;
  char *p;
  char *interp, *arg;		/* Interpreter file name, and first argument */
  size_t interp_len, len;
  file_t interp_file;		/* Port open on the interpreter file.  */
  FILE *f = &e->stream;
  char *new_argv;
  size_t new_argvlen;
  mach_port_t *new_dtable = NULL;
  u_int new_dtablesize;

  file_t user_fd (int fd)
    {
      if (fd < 0 || fd >= dtablesize || dtable[fd] == MACH_PORT_NULL)
	{
	  errno = EBADF;
	  return MACH_PORT_NULL;
	}
      return dtable[fd];
    }
  file_t user_crdir, user_cwdir;
  error_t user_port (int which, error_t (*operate) (mach_port_t))
    {
      error_t reauthenticate (file_t unauth, file_t *result)
	{
	  error_t err;
	  mach_port_t ref;
	  error_t uauth (auth_t auth)
	    {
	      return auth_user_authenticate (auth,
					     unauth,
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

      mach_port_t port = ((which < nports &&
			   portarray[which] != MACH_PORT_NULL)
			  ? portarray[which] :
			  (flags & EXEC_DEFAULTS) ? std_ports[which]
			  : MACH_PORT_NULL);

      switch (which)
	{
	case INIT_PORT_CRDIR:
	  if ((flags & EXEC_SECURE) || port == std_ports[which])
	    return (reauthenticate (port, &user_crdir) ?:
		    (*operate) (user_crdir));
	  break;
	case INIT_PORT_CWDIR:
	  if ((flags & EXEC_SECURE) || port == std_ports[which])
	    return (reauthenticate (port, &user_cwdir) ?:
		    (*operate) (user_cwdir));
	  break;
	}
      return (*operate) (port);
    }
  /* Look up NAME on behalf of the client.  */
  inline error_t lookup (const char *name, int flags, mach_port_t *result)
    {
      return hurd_file_name_lookup (&user_port, &user_fd,
				    name, flags, 0, result);
    }

  rewind (f);

  /* Check for our ``magic number''--"#!".  */

  errno = 0;
  if (getc (f) != '#' || getc (f) != '!')
    {
      /* No `#!' here.  If there was a read error (not including EOF),
	 return that error indication.  Otherwise return ENOEXEC to
	 say it's not a file we know how to execute.  */
      e->error = ferror (f) ? errno : ENOEXEC;
      return;
    }

  /* Read the rest of the first line of the file.  */

  interp_len = getline (&ibuf, &ibufsiz, f);
  if (ferror (f))
    {
      e->error = errno ?: EIO;
      return;
    }
  if (ibuf[interp_len - 1] == '\n')
    ibuf[--interp_len] = '\0';

  /* Find the name of the interpreter.  */
  p = ibuf + strspn (ibuf, " \t");
  interp = strsep (&p, " \t");

  if (p)
    /* Skip remaining blanks, and the rest of the line is the argument.  */
    {
      p += strspn (p, " \t");
      arg = p;
      len = interp_len - (arg - ibuf);
    }
  else
    /* There is no argument.  */
    len = 0;

  if (len == 0)
    arg = NULL;
  else
    ++len;			/* Include the terminating null.  */

  user_crdir = user_cwdir = MACH_PORT_NULL;

  rwlock_reader_lock (&std_lock);

  /* Open a port on the interpreter file.  */
  e->error = lookup (interp, O_EXEC, &interp_file);

  if (! e->error)
    {
      int free_file_name = 0; /* True if we should free FILE_NAME.  */
      jmp_buf args_faulted;
      void fault_handler (int signo)
	{ longjmp (args_faulted, 1); }
      error_t setup_args (struct hurd_signal_preempter *preempter)
	{
	  size_t namelen;
	  char * volatile file_name = NULL;

	  if (setjmp (args_faulted))
	    file_name = NULL;
	  else if (! (flags & EXEC_SECURE))
	    {
	      /* Try to figure out the file's name.  We guess that if ARGV[0]
		 contains a slash, it might be the name of the file; and that
		 if it contains no slash, looking for files named by ARGV[0] in
		 the `PATH' environment variable might find it.  */

	      error_t error;
	      char *name;
	      int free_name = 0; /* True if we should free NAME. */
	      file_t name_file;
	      struct stat st;
	      int file_fstype;
	      fsid_t file_fsid;
	      ino_t file_fileno;

	      /* Search $PATH for NAME, opening a port NAME_FILE on it.
		 This is encapsulated in a function so we can catch faults
		 reading the user's environment.  */
	      error_t search_path (struct hurd_signal_preempter *preempter)
		{
		  error_t err;
		  char *path = envz_get (envp, envplen, "PATH"), *pfxed_name;

		  if (! path)
		    {
		      const size_t len = confstr (_CS_PATH, NULL, 0);
		      path = alloca (len);
		      confstr (_CS_PATH, path, len);
		    }

		  err = hurd_file_name_path_lookup (user_port, user_fd,
						    name, path, O_EXEC, 0,
						    &name_file, &pfxed_name);
		  if (!err && pfxed_name)
		    {
		      name = pfxed_name;
		      free_name = 1;
		    }

		  return err;
		}

	      error = io_stat (file, &st); /* XXX insecure */
	      if (error)
		goto out;
	      file_fstype = st.st_fstype;
	      file_fsid = st.st_fsid;
	      file_fileno = st.st_ino;

	      if (memchr (argv, '\0', argvlen) == NULL)
		{
		  name = alloca (argvlen + 1);
		  bcopy (argv, name, argvlen);
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

	      if (!error && name_file != MACH_PORT_NULL)
		{
		  if (!io_stat (name_file, &st) && /* XXX insecure */
		      st.st_fstype == file_fstype &&
		      st.st_fsid == file_fsid &&
		      st.st_ino == file_fileno)
		    {
		      file_name = name;
		      free_file_name = free_name;
		    }
		  else if (free_name)
		    free (name);
		  mach_port_deallocate (mach_task_self (), name_file);
		}
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
	     like `ARGV[0] {ARG} FILE_NAME ARGV[1..n]' (ARG might have been
	     omitted). */

	  namelen = strlen (file_name) + 1;

	  new_argvlen = argvlen + len + namelen;
	  e->error = vm_allocate (mach_task_self (),
				  (vm_address_t *) &new_argv,
				  new_argvlen, 1);
	  if (e->error)
	    return e->error;

	  if (! setjmp (args_faulted))
	    {
	      char *other_args;
	      other_args = memccpy (new_argv, argv, '\0', argvlen);
	      p = &new_argv[other_args ? other_args - new_argv : argvlen];
	      if (arg)
		{
		  memcpy (p, arg, len);
		  p += len;
		}
	      memcpy (p, file_name, namelen);
	      p += namelen;
	      if (other_args)
		memcpy (p, other_args - new_argv + argv,
			argvlen - (other_args - new_argv));
	    }
	  else
	    {
	      /* We got a fault reading ARGV.  So don't use it.  */
	      char *n = stpncpy (new_argv,
				 "**fault in exec server reading argv[0]**",
				 argvlen);
	      memcpy (memcpy (n, arg, len) + len, file_name, namelen);
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

  /* We are now done reading the script file.  */
  finish (e, 0);
  free (ibuf);

  rwlock_reader_unlock (&std_lock);

  if (user_crdir != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), user_crdir);
  if (user_cwdir != MACH_PORT_NULL)
    mach_port_deallocate (mach_task_self (), user_cwdir);

  if (e->error)
    /* We cannot open the interpreter file to execute it.  Lose!  */
    return;

  /* Execute the interpreter program.  */
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
	vm_deallocate (mach_task_self (), (vm_address_t) argv, argvlen);
      if (! envp_copy)
	vm_deallocate (mach_task_self (), (vm_address_t) envp, envplen);
      for (i = 0; i < dtablesize; ++i)
	mach_port_deallocate (mach_task_self (), dtable[i]);
      if (! dtable_copy)
	vm_deallocate (mach_task_self (), (vm_address_t) dtable,
		       dtablesize * sizeof *dtable);
      for (i = 0; i < nports; ++i)
	mach_port_deallocate (mach_task_self (), portarray[i]);
      if (! portarray_copy)
	vm_deallocate (mach_task_self (), (vm_address_t) portarray,
		       nports * sizeof *portarray);
      if (! intarray_copy)
	vm_deallocate (mach_task_self (), (vm_address_t) intarray,
		       nints * sizeof *intarray);
    }
}
