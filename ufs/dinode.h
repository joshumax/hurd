/* Format of an inode on disk
   Copyright (C) 1991, 1993 Free Software Foundation

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

/* Modified from UCB by Michael I. Bushnell.  */

/*
 * Copyright (c) 1982, 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution is only permitted until one year after the first shipment
 * of 4.4BSD by the Regents.  Otherwise, redistribution and use in source and
 * binary forms are permitted provided that: (1) source distributions retain
 * this entire copyright notice and comment, and (2) distributions including
 * binaries display the following acknowledgement:  This product includes
 * software developed by the University of California, Berkeley and its
 * contributors'' in the documentation or other materials provided with the
 * distribution and in all advertising materials mentioning features or use
 * of this software.  Neither the name of the University nor the names of
 * its contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	@(#)dinode.h	7.9 (Berkeley) 6/28/90
 */

/*
 * This structure defines the on-disk format of an inode.
 */

#define	NDADDR	12		/* direct addresses in inode */
#define	NIADDR	3		/* indirect addresses in inode */

/* Indexes into di_ib */
#define INDIR_SINGLE 0
#define INDIR_DOUBLE 1
#define INDIR_TRIPLE 2		/* NOT SUPPORTED */

struct dinode {
	u_short	di_model;	/*  0: mode and type of file (low bits) */
	nlink_t	di_nlink;	/*  2: number of links to file */
	u_short	di_uidl;	/*  4: owner's user id (low bits) */
	u_short	di_gidl;	/*  6: owner's group id (low bits) */
	u_quad	di_qsize;	/*  8: number of bytes in file */
	time_t	di_atime;	/* 16: time last accessed */
	long	di_atusec;
	time_t	di_mtime;	/* 24: time last modified */
	long	di_mtusec;
	time_t	di_ctime;	/* 32: last time inode changed */
	long	di_ctusec;
	daddr_t	di_db[NDADDR];	/* 40: disk block addresses */
	daddr_t	di_ib[NIADDR];	/* 88: indirect blocks */
	long	di_flags;	/* 100: status, currently unused */
	long	di_blocks;	/* 104: blocks actually held */
	long	di_gen;		/* 108: generation number */
	long    di_trans;	/* 112: filesystem tranlator */
	uid_t   di_author;	/* 116: author id */
	u_short	di_uidh;	/* 120: user id (high bits) */
	u_short	di_gidh;	/* 122: group id (high bits) */
	u_short di_modeh;	/* 124: mode (high bits) */
	short	di_spare;	/* 126: reserved, currently unused */
};

#define DI_UID(di) ((di)->di_uidl | ((int)(di)->di_uidh << 16))
#define DI_GID(di) ((di)->di_gidl | ((int)(di)->di_gidh << 16))
#define DI_MODE(di) ((di)->di_model | ((int)(di)->di_modeh << 16))

#define LINK_MAX 32767		/* limited by width of nlink_t == 16 bits */

#if BYTE_ORDER == LITTLE_ENDIAN || defined(tahoe) /* ugh! -- must be fixed */
#define	di_size		di_qsize.val[0]
#else /* BYTE_ORDER == BIG_ENDIAN */
#define	di_size		di_qsize.val[1]
#endif
#define	di_rdev		di_db[0]

/* file modes  --  these are known to match appropriate values in gnu/stat.h */
#define	IFMT		000000170000 /* type of file */
#define	IFIFO		000000010000 /* named pipe (fifo) */
#define	IFCHR		000000020000 /* character special */
#define	IFDIR		000000040000 /* directory */
#define	IFBLK		000000060000 /* block special */
#define	IFREG		000000100000 /* regular */
#define	IFLNK		000000120000 /* symbolic link */
#define	IFSOCK		000000140000 /* socket */

#define ISPEC		000000607000 /*  special user-changeable bits */
#define INOCACHE	000000400000 /* don't cache contents */
#define IUSEUNK		000000200000 /* use IUNK in pref to IKNOWN */
#define	ISUID		000000004000 /* set user id on execution */
#define	ISGID		000000002000 /* set group id on execution */
#define	ISVTX		000000001000 /* caching preference / append only dir */

/* masks for various sets of permissions: */
#define IOWNER		000000000700 /* owner of file */
#define IGROUP		000000000070 /* group of file */
#define IKNOWN		000000000007 /* anyone who possesses a uid */
#define IUNKNOWN	000007000000 /* anyone who doesn't possess a uid */

#define ISPARE		037770000000 /* unused (yet) */

#define	IREAD		0400		/* read, write, execute permissions */
#define	IWRITE		0200
#define	IEXEC		0100

