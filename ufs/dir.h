/* Modified from BSD by Michael I. Bushnell for GNU Hurd ufs server. */
/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)dir.h	8.2 (Berkeley) 1/21/94
 */

#ifndef _DIR_H_
#define	_DIR_H_

#include <endian.h>

/*
 * A directory consists of some number of blocks of DIRBLKSIZ
 * bytes, where DIRBLKSIZ is chosen such that it can be transferred
 * to disk in a single atomic operation (e.g. 512 bytes on most machines).
 *
 * Each DIRBLKSIZ byte block contains some number of directory entry
 * structures, which are of variable length.  Each directory entry has
 * a struct direct at the front of it, containing its inode number,
 * the length of the entry, and the length of the name contained in
 * the entry.  These are followed by the name padded to a 4 byte boundary
 * with null bytes.  All names are guaranteed null terminated.
 * The maximum length of a name in a directory is MAXNAMLEN.
 *
 * The macro DIRSIZ(fmt, dp) gives the amount of space required to represent
 * a directory entry.  Free space in a directory is represented by
 * entries which have dp->d_reclen > DIRSIZ(fmt, dp).  All DIRBLKSIZ bytes
 * in a directory block are claimed by the directory entries.  This
 * usually results in the last entry in a directory having a large
 * dp->d_reclen.  When entries are deleted from a directory, the
 * space is returned to the previous entry in the same directory
 * block by increasing its dp->d_reclen.  If the first entry of
 * a directory block is free, then its dp->d_ino is set to 0.
 * Entries other than the first in a directory do not normally have
 * dp->d_ino set to 0.
 */
#define DIRBLKSIZ	DEV_BSIZE
#undef	MAXNAMLEN
#define	MAXNAMLEN	255

/* Don't call this struct DIRECT because the library defines that
   (sometimes) in a possible different way. */

struct	directory_entry {
	u_long	d_ino;			/* inode number of entry */
	u_short	d_reclen;		/* length of this record */
	u_char	d_type; 		/* file type, see below */
	u_char	d_namlen;		/* length of string in d_name */
	char	d_name[MAXNAMLEN + 1];	/* name with length <= MAXNAMLEN */
};

/* Return the type from a struct directory_entry, paying attention to whether
   this filesystem supports the type extension */
#define DIRECT_TYPE(dp) (direct_symlink_extension ? (dp)->d_type : DT_UNKNOWN)

/* Return the namlen from a struct direct, paying attention to whether
   this filesystem supports the type extension */
#if (BYTE_ORDER == LITTLE_ENDIAN)
#define DIRECT_NAMLEN(dp) (direct_symlink_extension || swab_disk	    \
			   ? (dp)->d_namlen 				    \
			   : (dp)->d_type)
#else
#define DIRECT_NAMLEN(dp) (!direct_symlink_extension && swab_disk	    \
			   ? (dp)->d_type				    \
			   : (dp)->d_namlen)
#endif

/*
 * The DIRSIZ macro gives the minimum record length which will hold
 * the directory entry.  This requires the amount of space in struct direct
 * without the d_name field, plus enough space for the name with a terminating
 * null byte (dp->d_namlen+1), rounded up to a 4 byte boundary.
 */
/* In BSD this macro takes a struct direct.  Modified by MIB here to
   take the namelen (as computed by strlen).  */
#define DIRSIZ(namelen) \
    ((sizeof (struct directory_entry) - (MAXNAMLEN+1)) + (((namelen)+1 + 3) &~ 3))

#if 0 /* This is the BSD definition */
#if (BYTE_ORDER == LITTLE_ENDIAN)
#define DIRSIZ(oldfmt, dp) \
    ((oldfmt) ? \
    ((sizeof (struct direct) - (MAXNAMLEN+1)) + (((dp)->d_type+1 + 3) &~ 3)) : \
    ((sizeof (struct direct) - (MAXNAMLEN+1)) + (((dp)->d_namlen+1 + 3) &~ 3)))
#else
#define DIRSIZ(oldfmt, dp) \
    ((sizeof (struct direct) - (MAXNAMLEN+1)) + (((dp)->d_namlen+1 + 3) &~ 3))
#endif
#endif /* 0 */

#define OLDDIRFMT	1
#define NEWDIRFMT	0

#if 0 /* Not used in GNU */
/*
 * Template for manipulating directories.
 * Should use struct direct's, but the name field
 * is MAXNAMLEN - 1, and this just won't do.
 */
struct dirtemplate {
	u_long	dot_ino;
	short	dot_reclen;
	u_char	dot_type;
	u_char	dot_namlen;
	char	dot_name[4];		/* must be multiple of 4 */
	u_long	dotdot_ino;
	short	dotdot_reclen;
	u_char	dotdot_type;
	u_char	dotdot_namlen;
	char	dotdot_name[4];		/* ditto */
};

/*
 * This is the old format of directories, sanz type element.
 */
struct odirtemplate {
	u_long	dot_ino;
	short	dot_reclen;
	u_short	dot_namlen;
	char	dot_name[4];		/* must be multiple of 4 */
	u_long	dotdot_ino;
	short	dotdot_reclen;
	u_short	dotdot_namlen;
	char	dotdot_name[4];		/* ditto */
};
#endif /* 0 */

#endif /* !_DIR_H_ */
