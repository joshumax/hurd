/* <sys/procfs.h> -- data structures describing ELF core file formats
   Copyright (C) 2002, 2015, 2016 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, write to the Free
   Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307 USA.  */

#ifndef _SYS_PROCFS_H
#define _SYS_PROCFS_H	1

/* This poorly-named file describes the format of the note segments in ELF
   core files.  It doesn't have anything to do with a `/proc' file system.

   Anyway, the whole purpose of this file is for GDB and GDB only.
   Don't read too much into it.  Don't use it for anything other than
   GDB unless you know what you are doing.  */

#include <features.h>
#include <inttypes.h>
#include <mach/std_types.h>
#include <sys/types.h>
#include <sys/time.h>
#include <ucontext.h>

__BEGIN_DECLS

/* This structure gives some general information about the process, things
   that `ps' would tell you (hence the name).  We have chosen the names and
   the layout of the structure to coincide with the Solaris 8 definition.
   The `file' program happens to know to look at 84 bytes into this
   structure for the 16-byte `pr_fname' member, so we don't disappoint it.  */
#define ELF_PRARGSZ     (80)    /* Number of chars of argument list saved.  */
struct elf_psinfo
{
  int pr_flag;			/* Meaningless flag bits.  */
  int pr_nlwp;			/* Number of threads in this process.  */
  pid_t pr_pid;			/* Process ID.  */
  pid_t pr_ppid;		/* Parent's process ID.  */
  pid_t pr_pgid;		/* Process group ID.  */
  pid_t pr_sid;			/* Session ID.  */
  uid_t pr_uid, pr_euid;	/* Real and effective UID (first one).  */
  gid_t pr_gid, pr_egid;	/* Real and effective GID (first one).  */
  size_t pr_size;		/* Virtual memory size of process (KB).  */
  size_t pr_rssize;		/* Resident set size of process (KB).  */
  uint16_t pr_pctcpu;		/* % of CPU used by all threads.  */
  uint16_t pr_pctmem;		/* % of virtual memory used by process.  */
  struct timespec pr_start;	/* Date & time of task creation.  */
  struct timespec pr_time;	/* CPU time used by this process.  */
  struct timespec pr_ctime;	/* CPU time used by dead children.  */
  uint32_t pr_reserved1[2];	/* Padding to place pr_psargs at offset 84.  */
  char pr_fname[16];		/* Initial part of executable file name.  */
  char pr_psargs[ELF_PRARGSZ];	/* Initial part of argument list.  */
  int pr_wstat;			/* Zombie exit status (not really used).  */
  int pr_argc;			/* The argument count at startup.  */
  vm_address_t pr_argv;		/* Original argument vector address.  */
  vm_address_t pr_envp;		/* Original environment vector address.  */
};
typedef struct elf_psinfo psinfo_t;

/* This structure also gives general information about the process.
   The Solaris version gives the status of "the representative thread",
   but GDB does not actually use this structure for anything but the PID.  */
struct elf_pstatus
{
  int pr_flags;			/* Meaningless flags bits.  */
  int pr_nlwp;			/* Number of threads in this process.  */
  pid_t pr_pid;			/* Process ID.  */
  pid_t pr_ppid;		/* Parent's process ID.  */
  pid_t pr_pgid;		/* Process group ID.  */
  pid_t pr_sid;			/* Session ID.  */
  struct timespec pr_utime;	/* User CPU time used by this process.  */
  struct timespec pr_stime;	/* System CPU time used by this process.  */
  struct timespec pr_cutime;	/* User CPU time used by dead children.  */
  struct timespec pr_cstime;	/* System CPU time used by dead children.  */
};
typedef struct elf_pstatus pstatus_t;

/* Information about a signal, the signal that killed the process.  */
struct elf_siginfo
{
  int si_signo;			/* Signal number.  */
  int si_code;			/* Extra code.  */
  int si_errno;			/* Errno.  */
};

typedef gregset_t prgregset_t;
typedef fpregset_t prfpregset_t;

/* This structure describes the state of one thread.  GDB examines
   pr_cursig to see what killed the process, so those are set in the
   record for every thread.  The `pr_lwpid' member contains a value
   that is unique among the threads in this code file, but otherwise
   meaningless.  GDB examines that and the `pr_reg' and `pr_fpreg'
   members for the register state of the thread.  */
struct elf_lwpstatus
{
  int pr_flags;			/* Meaningless flags bits.  */
  int pr_lwpid;			/* Identifies this thread from others.  */
  int pr_cursig;		/* Signal that killed the thread.  */
  struct elf_siginfo pr_info;	/* Details about the signal.  */
  prgregset_t pr_reg;		/* State of thread's general registers.  */
  prfpregset_t pr_fpreg;	/* State of its floating-point registers.  */
};
typedef struct elf_lwpstatus lwpstatus_t;


__END_DECLS

#endif	/* sys/procfs.h */
