/* C declarations for Hurd server interfaces
   Copyright (C) 1993, 1994 Free Software Foundation, Inc.

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

#ifndef _HURD_TYPES_H
#define _HURD_TYPES_H

#include <mach/std_types.h>	/* For mach_port_t et al. */
#include <sys/types.h>		/* For pid_t and uid_t.  */

/* A string identifying this release of the GNU Hurd.  Our
   interpretation of the term "release" is that it refers to a set of
   server interface definitions.  A "version" in Posix terminology is
   a distribution of the Hurd; there may be more than one distribution
   without changing the release number.  */
#define HURD_RELEASE "0.0 pre-alpha"

typedef mach_port_t file_t;
typedef mach_port_t fsys_t;
typedef mach_port_t io_t;
typedef mach_port_t process_t;
typedef mach_port_t auth_t;
typedef mach_port_t socket_t;
typedef mach_port_t addr_port_t;
typedef mach_port_t startup_t;
typedef mach_port_t proccoll_t;

/* XXX temp hack --roland */
#include <errno.h>
#ifndef errno
typedef kern_return_t error_t;
#endif

/* These names exist only because of MiG deficiencies.
   You should not use them in C source; use the normal C types instead.  */
typedef char *data_t;
typedef char string_t [1024];
typedef int *intarray_t;
typedef int *fd_mask_t;
typedef mach_port_t *portarray_t;
typedef pid_t *pidarray_t;
typedef uid_t *idarray_t;
typedef struct rusage rusage_t;
typedef struct flock flock_t;
typedef struct utsname utsname_t;

typedef struct stat io_statbuf_t;

/* stb_fstype is one of: */
#define FSTYPE_UFS     0x00000000 /* 4.x BSD Fast File System */
#define FSTYPE_NFS     0x00000001 /* Network File System ala Sun */
#define FSTYPE_GFS     0x00000002 /* GNU file system */
#define FSTYPE_LFS     0x00000003 /* Logging File System ala Sprite */
#define FSTYPE_SYSV    0x00000004 /* Old U*x filesystem ala System V */
#define FSTYPE_FTP     0x00000005 /* Transparent FTP */
#define FSTYPE_TAR     0x00000006 /* Transparent TAR */
#define FSTYPE_AR      0x00000007 /* Transparent AR */
#define FSTYPE_CPIO    0x00000008 /* Transparent CPIO */
#define FSTYPE_MSLOSS  0x00000009 /* MS-DOS */
#define FSTYPE_CPM     0x0000000a /* CP/M */
#define FSTYPE_HFS     0x0000000b /* Don't ask */
#define FSTYPE_DTFS    0x0000000c /* used by desktop to provide more info */
#define FSTYPE_GRFS    0x0000000d /* GNU Remote File System */
#define FSTYPE_TERM    0x0000000e /* GNU Terminal driver */
#define FSTYPE_DEV     0x0000000f /* GNU Special file server */
#define FSTYPE_PROC    0x00000010 /* /proc filesystem ala Version 9 */
#define FSTYPE_IFSOCK  0x00000011 /* PF_LOCAL socket naming point */
#define FSTYPE_AFS     0x00000012 /* Andrew File System 3.xx */
#define FSTYPE_DFS     0x00000013 /* Distributed File Sys (OSF) == AFS 4.xx */
#define FSTYPE_PROC9   0x00000014 /* /proc filesystem ala Plan 9 */

/* Bits for flags in file_exec and exec_* calls are as follows: */
#define EXEC_NEWTASK	0x00000001 /* create new task; kill old one */
#define EXEC_SECURE	0x00000002 /* use secure values of portarray, etc. */
#define EXEC_DEFAULTS	0x00000004 /* Use defaults for unspecified ports */

/* Standard port assignments for file_exec and exec_* */
enum
  {
    INIT_PORT_CWDIR,
    INIT_PORT_CRDIR,
    INIT_PORT_AUTH,
    INIT_PORT_PROC,
    INIT_PORT_LOGINCOLL,
    INIT_PORT_CTTYID,
    /* If MACH_PORT_NULL is given for the bootstrap port,
       the bootstrap port of the old task is used.  */
    INIT_PORT_BOOTSTRAP,
    INIT_PORT_MAX
  };

/* Standard ints for file_exec and exec_* */
enum
  {
    INIT_UMASK,
    INIT_SIGMASK,
    INIT_SIGIGN,
    INIT_SIGPENDING,
    INIT_INT_MAX,
  };

/* Bits for flags in file_set_translator call are as follows: */
#define FS_TRANS_FORCE     0x00000001 /* must use translator(no sht circuit) */
#define FS_TRANS_EXCL      0x00000002 /* don't do it if already translated */

/* Values for retry field in dir_pathtrans */
enum retry_type
{
  FS_RETRY_NONE,		/* no retry necessary */
  FS_RETRY_NORMAL,		/* retry normally */
  FS_RETRY_REAUTH,		/* retry after reauthenticating retry port */
  FS_RETRY_MAGICAL,		/* retry string is magical */
  /* "tty" means controlling tty;
     "fd%u" means file descriptor N;
     */
};
typedef enum retry_type retry_type;

/* Select types for io_select */
#define SELECT_READ  0x00000001
#define SELECT_WRITE 0x00000002
#define SELECT_URG   0x00000004

/* Flags for fsys_goaway.  Also, these flags are sent as the oldtrans_flags
   in file_set_translator to describe how to terminate the old translator. */
#define FSYS_GOAWAY_NOWAIT    0x00000001 /* Return immediately */
#define FSYS_GOAWAY_NOSYNC    0x00000002 /* Don't update physical media */
#define FSYS_GOAWAY_FORCE     0x00000004 /* Go away despite current users */
#define FSYS_GOAWAY_UNLINK    0x00000008 /* Go away only if non-dir */
#define FSYS_GOAWAY_RECURSE   0x00000010 /* Shutdown children too */

/* This structure is known to be 19 ints long in hurd_types.defs */
struct fsys_statfsbuf
{
  long fsys_stb_type;
  long fsys_stb_bsize;
  long fsys_stb_fsize;
  long fsys_stb_blocks;
  long fsys_stb_bfree;
  long fsys_stb_bavail;
  long fsys_stb_files;
  long fsys_stb_ffree;
  fsid_t fsys_stb_fsid;
  long fsys_stb_spare[9];
};
typedef struct fsys_statfsbuf fsys_statfsbuf_t;

/* Possible types of version info for proc_version.  */
enum verstype
{
  SYSNAME,
  RELEASE,
  VERSION,
  MACHINE,
};

#include <mach/task_info.h>
#include <mach/thread_info.h>

struct procinfo
{
  int state;
  uid_t owner;
  pid_t ppid;
  pid_t pgrp;
  pid_t session;

  int nthreads;			/* size of pi_threadinfos */
  
  struct task_basic_info taskinfo;
  struct
    {
      struct thread_basic_info pis_bi;
      struct thread_sched_info pis_si;
    } threadinfos[0];
};
typedef int *procinfo_t;

/* Bits in state: */
#define PI_STOPPED 0x00000001	/* Proc server thinks is stopped  */
#define PI_EXECED  0x00000002	/* Has called proc_exec */
#define PI_ORPHAN  0x00000008	/* Process group is orphaned */
#define PI_NOMSG   0x00000010	/* Process has no message port */
#define PI_SESSLD  0x00000020	/* Session leader */
#define PI_NOTOWNED 0x0000040	/* Process has no owner */
#define PI_NOPARENT 0x0000080	/* Hasn't identified a parent */
#define PI_ZOMBIE  0x00000100	/* Has no associated task */

/* Types of ports the terminal driver can run on top of. */
#define TERM_ON_MACHDEV		1
#define TERM_ON_HURDIO		2
#define TERM_ON_MASTERPTY	3

#endif
