/*
 * Mach Operating System
 * Copyright (c) 1991,1990,1989,1988 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#ifndef	_FILE_IO_H_
#define	_FILE_IO_H_

/*
 * Read-only file IO.
 */

#include <mach.h>
#include <cthreads.h>

#include <stdint.h>
#include <device/device_types.h>

/* Types used by the ext2 header files.  */
typedef u_int32_t __u32;
typedef int32_t   __s32;
typedef u_int16_t __u16;
typedef int16_t   __s16;
typedef u_int8_t  __u8;
typedef int8_t    __s8;

#include <defs.h>
#include "minix_fs.h"
#include "../ext2fs/ext2_fs.h"	/* snarf stolen linux header from ext2fs */
#include "disk_inode.h"

#define	BSD_FFS		0
#define	EXT2_FS		1
#define	MINIX_FS	2

#define	EXT2_NIADDR (EXT2_N_BLOCKS - EXT2_NDIR_BLOCKS)

/*
 * In-core open file.
 */
struct file {
	struct mutex		f_lock;		/* lock */
	mach_port_t		f_dev;		/* port to device */
	vm_offset_t		f_buf;		/* buffer for data block */
	vm_size_t		f_buf_size;	/* size of data block */
	daddr_t			f_buf_blkno;	/* block number of data block */
	vm_size_t		f_size;	/* size in bytes of the file */

	int			f_fstype;	/* contains fs-id */

	union {
		struct {
			struct fs *	ffs_fs;	/* pointer to super-block */
			struct icommon	ffs_ic;	/* copy of on-disk inode */

			/* number of blocks mapped by
			   indirect block at level i */
			int			ffs_nindir[FFS_NIADDR+1];

			/* buffer for indirect block at level i */
			vm_offset_t		ffs_blk[FFS_NIADDR];

			/* size of buffer */
			vm_size_t		ffs_blksize[FFS_NIADDR];

			/* disk address of block in buffer */
			daddr_t			ffs_blkno[FFS_NIADDR];
		} ffs;
		struct {
			/* pointer to super-block */
			struct ext2_super_block*ext2_fs;

			/* pointer to group descriptors */
			struct ext2_group_desc*	ext2_gd;

			/* size of group descriptors */
			vm_size_t		ext2_gd_size;

			/* copy of on-disk inode */
			struct ext2_inode	ext2_ic;

			/* number of blocks mapped by
			   indirect block at level i */
			int			ext2_nindir[EXT2_NIADDR+1];

			/* buffer for indirect block at level i */
			vm_offset_t		ext2_blk[EXT2_NIADDR];

			/* size of buffer */
			vm_size_t		ext2_blksize[EXT2_NIADDR];

			/* disk address of block in buffer */
			daddr_t			ext2_blkno[EXT2_NIADDR];
		} ext2;
		struct {
			/* pointer to super-block */
			struct minix_super_block*	minix_fs;

			/* copy of on-disk inode */
			struct minix_inode	minix_ic;

			/* number of blocks mapped by
			   indirect block at level i */
			int			minix_nindir[MINIX_NIADDR+1];

			/* buffer for indirect block at level i */
			vm_offset_t		minix_blk[MINIX_NIADDR];

			/* size of buffer */
			vm_size_t		minix_blksize[MINIX_NIADDR];

			/* disk address of block in buffer */
			minix_daddr_t		minix_blkno[MINIX_NIADDR];
		} minix;
	} u;
};

/*
 * In-core open file, with in-core block map.
 */
struct file_direct {
	int			f_fstype; /* XXX was: true if ext2, false if ffs */

	mach_port_t	fd_dev;		/* port to device */
	daddr_t *	fd_blocks;	/* array of disk block addresses */
	long		fd_size;	/* number of blocks in the array */
	long		fd_bsize;	/* disk block size */
	long		fd_bshift;	/* log2(fd_bsize) */
	long		fd_fsbtodb;	/* log2(fd_bsize / disk sector size) */
};

#define	file_is_device(_fd_)		((_fd_)->fd_blocks == 0)

/*
 * Exported routines.
 */

extern int	open_file();
extern void	close_file();
extern int	read_file();

extern int	open_file_direct();
extern int	add_file_direct();
extern int	remove_file_direct();
extern int	file_wire_direct();
extern int	page_read_file_direct();
extern int	page_write_file_direct();

/*
 * Error codes for file system errors.
 */

#include <errno.h>

/* Just use the damn Hurd error numbers.  This is old CMU/Utah code from
   the days of personality-independent Mach where it made sense for this to
   be a standalone universe.  In the Hurd, we compile serverboot against
   the regular C library anyway.  */

#define	FS_NOT_DIRECTORY	ENOTDIR
#define	FS_NO_ENTRY		ENOENT
#define	FS_NAME_TOO_LONG	ENAMETOOLONG
#define	FS_SYMLINK_LOOP		ELOOP
#define	FS_INVALID_FS		EFTYPE /* ? */
#define	FS_NOT_IN_FILE		EINVAL
#define	FS_INVALID_PARAMETER	EINVAL

#if 0
#define	FS_NOT_DIRECTORY	5000		/* not a directory */
#define	FS_NO_ENTRY		5001		/* name not found */
#define	FS_NAME_TOO_LONG	5002		/* name too long */
#define	FS_SYMLINK_LOOP		5003		/* symbolic link loop */
#define	FS_INVALID_FS		5004		/* bad file system */
#define	FS_NOT_IN_FILE		5005		/* offset not in file */
#define	FS_INVALID_PARAMETER	5006		/* bad parameter to routine */
#endif


#endif	/* _FILE_IO_H_ */
