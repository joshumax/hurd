/*
 * Copyright (c) 1994 The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 *      Author: Bryan Ford, University of Utah CSL
 *      MINIX FS patches: Csizmazia Balazs, University ELTE, Hungary
 */
/* This is just an icky kludgy "VFS layer" (harhar) for ffs and ext2 and minix.  */

#include "file_io.h"

int
open_file(master_device_port, path, fp)
	mach_port_t	master_device_port;
	char *		path;
	struct file	*fp;
{
	int rc;

	if ((rc = ext2_open_file(master_device_port, path, fp))
	    != FS_INVALID_FS)
	{
		if (rc == 0)
			fp->f_fstype = EXT2_FS;
		return rc;
	}
	if ( (rc = minix_open_file(master_device_port, path, fp))
	    != FS_INVALID_FS )
	{
		if (rc == 0)
			fp->f_fstype = MINIX_FS;
		return rc;
	}
	fp->f_fstype = BSD_FFS;
	return ffs_open_file(master_device_port, path, fp);
}

void
close_file(fp)
	register struct file	*fp;
{
	switch (fp->f_fstype) {
		case	EXT2_FS:
				ext2_close_file(fp);
				return;
		case	MINIX_FS:
				minix_close_file(fp);
				return;
		default:
				ffs_close_file(fp);
				return;
	}
}

int
read_file(fp, offset, start, size, resid)
	register struct file	*fp;
	vm_offset_t		offset;
	vm_offset_t		start;
	vm_size_t		size;
	vm_size_t		*resid;	/* out */
{
	switch (fp->f_fstype) {
		case	EXT2_FS:
				return ext2_read_file(fp, offset, start, size, resid);
		case	MINIX_FS:
				return minix_read_file(fp, offset, start, size, resid);
		default:
				return ffs_read_file(fp, offset, start, size, resid);
	}

}

int
file_is_directory(struct file *f)
{
	switch (f->f_fstype) {
		case	EXT2_FS:
				return ext2_file_is_directory(f);
		case	MINIX_FS:
				return minix_file_is_directory(f);
		default:
				return ffs_file_is_directory(f);
	}
}

int
file_is_regular(struct file *f)
{
	switch (f->f_fstype) {
		case	EXT2_FS:
				return ext2_file_is_regular(f);
		case	MINIX_FS:
				return minix_file_is_regular(f);
		default:
				return ffs_file_is_regular(f);
	}

}

int
open_file_direct(dev, fdp, is_structured)
	mach_port_t	dev;
	register struct file_direct *fdp;
	boolean_t	is_structured;
{
	int rc;


	if ((rc = ext2_open_file_direct(dev, fdp, is_structured))
	    != FS_INVALID_FS)
	{
		if (rc == 0)
			fdp->f_fstype = EXT2_FS;
		return rc;
	}
	if ( (rc = minix_open_file_direct(dev, fdp, is_structured) )
	    != FS_INVALID_FS )
	{
		if (rc == 0)
			fdp->f_fstype = MINIX_FS;
		return rc;
	}
	fdp->f_fstype = BSD_FFS;
	return ffs_open_file_direct(dev, fdp, is_structured);
}

int
add_file_direct(fdp, fp)
	register struct file_direct *fdp;
	register struct file *fp;
{
	switch (fp->f_fstype) {
		case	EXT2_FS:
				return ext2_add_file_direct(fdp, fp);
		case	MINIX_FS:
				return minix_add_file_direct(fdp, fp);
		default:
				return ffs_add_file_direct(fdp, fp);
	}
}


int
remove_file_direct(fdp)
	struct file_direct	*fdp;
{
	switch (fdp->f_fstype) {
		case	EXT2_FS:
			return ext2_remove_file_direct(fdp);
		case	MINIX_FS:
			return minix_remove_file_direct(fdp);
		default:
			return ffs_remove_file_direct(fdp);
	}
}

/*
 * some other stuff, that was previously defined as macro
 */

int
file_is_structured(fp)
	register struct	file *fp;
{
	switch (fp->f_fstype) {
		case	EXT2_FS:
			return (fp)->u.ext2.ext2_fs != 0;
		case	MINIX_FS:
			return (fp)->u.minix.minix_fs != 0;
		default:
			return (fp)->u.ffs.ffs_fs != 0;
	}
}

/*
 * Special read and write routines for default pager.
 * Assume that all offsets and sizes are multiples
 * of DEV_BSIZE.
 */

#define	fdir_blkoff(fdp, offset)	/* offset % fd_bsize */ \
	((offset) & ((fdp)->fd_bsize - 1))
#define	fdir_lblkno(fdp, offset)	/* offset / fd_bsize */ \
	((offset) >> (fdp)->fd_bshift)

#define	fdir_fsbtodb(fdp, block)	/* offset * fd_bsize / DEV_BSIZE */ \
	((block) << (fdp)->fd_fsbtodb)

/*
 * Read all or part of a data block, and
 * return a pointer to the appropriate part.
 * Caller must deallocate the block when done.
 */
int
page_read_file_direct(fdp, offset, size, addr, size_read)
	register struct file_direct *fdp;
	vm_offset_t offset;
	vm_size_t size;
	vm_offset_t *addr;			/* out */
	mach_msg_type_number_t *size_read;	/* out */
{
	vm_offset_t		off;
	register daddr_t	file_block;
	daddr_t			disk_block;

	if (offset % DEV_BSIZE != 0 ||
	    size % DEV_BSIZE != 0)
	    panic("page_read_file_direct");

	if (offset >= (fdp->fd_size << fdp->fd_bshift))
	    return (FS_NOT_IN_FILE);

	off = fdir_blkoff(fdp, offset);
	file_block = fdir_lblkno(fdp, offset);

	if (file_is_device(fdp)) {
	    disk_block = file_block;
	} else {
	    disk_block = fdp->fd_blocks[file_block];
	    if (disk_block == 0)
		return (FS_NOT_IN_FILE);

	    if (size > fdp->fd_bsize) {
	        /* Read only as much as is contiguous on disk.  */
		daddr_t b = file_block + 1;
		while (b < file_block + fdp->fd_size &&
		       fdp->fd_blocks[b] == disk_block + fdir_fsbtodb(fdp, 1))
		  ++b;
	        size = (b - file_block) * fdp->fd_bsize;
	    }
	}

	return (device_read(fdp->fd_dev,
			0,
			(recnum_t) (fdir_fsbtodb(fdp, disk_block) + btodb(off)),
			(int) size,
			(char **) addr,
			size_read));
}

/*
 * Write all or part of a data block, and
 * return the amount written.
 */
int
page_write_file_direct(fdp, offset, addr, size, size_written)
	register struct file_direct *fdp;
	vm_offset_t offset;
	vm_offset_t addr;
	vm_size_t size;
	vm_offset_t *size_written;	/* out */
{
	vm_offset_t		off;
	register daddr_t	file_block;
	daddr_t			disk_block;
	int			rc, num_written;
	vm_offset_t		block_size;

	if (offset % DEV_BSIZE != 0 ||
	    size % DEV_BSIZE != 0)
	    panic("page_write_file");

	if (offset >= (fdp->fd_size << fdp->fd_bshift))
	    return (FS_NOT_IN_FILE);

	off = fdir_blkoff(fdp, offset);
	file_block = fdir_lblkno(fdp, offset);

	if (file_is_device(fdp)) {
	    disk_block = file_block;
	} else {
	    disk_block = fdp->fd_blocks[file_block];
	    if (disk_block == 0)
		return (FS_NOT_IN_FILE);

	    if (size > fdp->fd_bsize) {
	        /* Write only as much as is contiguous on disk.  */
		daddr_t b = file_block + 1;
		while (b < file_block + fdp->fd_size &&
		       fdp->fd_blocks[b] == disk_block + fdir_fsbtodb(fdp, 1))
		  ++b;
	        size = (b - file_block) * fdp->fd_bsize;
	    }
	}

	/*
	 * Write the data.  Wait for completion to keep
	 * reads from getting ahead of writes and reading
	 * stale data.
	 */
	rc = device_write(
			fdp->fd_dev,
			0,
			(recnum_t) (fdir_fsbtodb(fdp, disk_block) + btodb(off)),
			(char *) addr,
			size,
			&num_written);
	*size_written = num_written;
	return rc;
}
