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
page_read_file_direct(fdp, offset, size, addr, size_read)
	register struct file_direct *fdp;
	vm_offset_t offset;
	vm_size_t size;
	vm_offset_t *addr;			/* out */
	mach_msg_type_number_t *size_read;	/* out */
{
	switch (fdp->f_fstype) {
		case	EXT2_FS:
				return ext2_page_read_file_direct(fdp, offset, size, addr, size_read);
		case	MINIX_FS:
				return minix_page_read_file_direct(fdp, offset, size, addr, size_read);
		default:
				return ffs_page_read_file_direct(fdp, offset, size, addr, size_read);
	}
}

int
page_write_file_direct(fdp, offset, addr, size, size_written)
	register struct file_direct *fdp;
	vm_offset_t offset;
	vm_offset_t addr;
	vm_size_t size;
	vm_offset_t *size_written;	/* out */
{
	switch (fdp->f_fstype) {
		case	EXT2_FS:
				return ext2_page_write_file_direct(fdp, offset, addr, size, size_written);
		case	MINIX_FS:
				return minix_page_write_file_direct(fdp, offset, addr, size, size_written);
		default:
				return ffs_page_write_file_direct(fdp, offset, addr, size, size_written);
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
