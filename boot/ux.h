/* Hacks to make boot work under UX

   Copyright (C) 1993, 1994, 1995, 1996 Free Software Foundation, Inc.

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

#define sigmask(m) (1 << ((m)-1))

#define IOCPARM_MASK 0x7f
#define IOC_OUT 0x40000000
#define IOC_IN 0x80000000
#define _IOR(x,y,t) (IOC_OUT|((sizeof(t)&IOCPARM_MASK)<<16)|(x<<8)|y)
#define _IOW(x,y,t) (IOC_IN|((sizeof(t)&IOCPARM_MASK)<<16)|(x<<8)|y)
#define FIONREAD _IOR('f', 127, int)
#define FIOASYNC _IOW('f', 125, int)
#define TIOCGETP _IOR('t', 8, struct sgttyb)
#define TIOCLGET _IOR('t', 124, int)
#define TIOCLSET _IOW('t', 125, int)
#define TIOCSETN _IOW('t', 10, struct sgttyb)
#define LDECCTQ 0x4000
#define LLITOUT 0x0020
#define LPASS8  0x0800
#define LNOFLSH 0x8000
#define RAW 0x0020
#define ANYP 0x00c0
#define ECHO 8


struct sgttyb
{
  char unused[4];
  short sg_flags;
};

#define SIGIO 23

struct sigvec
{
  void (*sv_handler)();
  int sv_mask;
  int sv_flags;
};

struct uxstat
  {
    short int st_dev;		/* Device containing the file.	*/
    __ino_t st_ino;		/* File serial number.		*/
    unsigned short int st_mode;	/* File mode.  */
    __nlink_t st_nlink;		/* Link count.  */
    unsigned short int st_uid;	/* User ID of the file's owner.	*/
    unsigned short int st_gid;	/* Group ID of the file's group.*/
    short int st_rdev;		/* Device number, if device.  */
    __off_t st_size;		/* Size of file, in bytes.  */
    __time_t st_atime;		/* Time of last access.  */
    unsigned long int st_atime_usec;
    __time_t st_mtime;		/* Time of last modification.  */
    unsigned long int st_mtime_usec;
    __time_t st_ctime;		/* Time of last status change.  */
    unsigned long int st_ctime_usec;
    unsigned long int st_blksize; /* Optimal block size for I/O.  */
    unsigned long int st_blocks; /* Number of 512-byte blocks allocated.  */
    long int st_spare[2];
  };

void get_privileged_ports (mach_port_t *host_port, mach_port_t *device_port);

/* We can't include <unistd.h> for this, because that will fight witho
   our definitions of syscalls below. */
int syscall (int, ...);

int open (const char *name, int flags, int mode);
int write (int fd, const void *buf, int len);
int read (int fd, void *buf, int len);
int uxfstat (int fd, struct uxstat *buf);
int close (int fd);
int lseek (int fd, int off, int whence);
int uxexit (int code);
int getpid ();
int ioctl (int fd, int code, void *buf);
int sigblock (int mask);
int sigsetmask (int mask);
int sigpause (int mask);
int sigvec (int sig, struct sigvec *vec, struct sigvec *ovec);

#undef O_RDONLY
#undef O_WRONLY
#undef O_RDWR
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

#define host_exit(c) uxexit(c)

typedef struct uxstat host_stat_t;
#define host_fstat(fd, st) uxfstat (fd, st)

void init_stdio ();

#undef errno
int errno;
