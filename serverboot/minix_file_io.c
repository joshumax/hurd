/*
 * Mach Operating System
 * Copyright (c) 1991,1990 Carnegie Mellon University
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
/*
 * Stand-alone file reading package.
 */

#include <device/device_types.h>
#include <device/device.h>

#include <mach/mach_traps.h>
#include <mach/mach_interface.h>

#include "file_io.h"
#include "minix_ffs_compat.h"
#include "minix_fs.h"

void	minix_close_file();	/* forward */

#define	MINIX_NAME_LEN	14
#define	MINIX_BLOCK_SIZE	1024

/*
 * Free file buffers, but don't close file.
 */
static void
free_file_buffers(fp)
	register struct file	*fp;
{
	register int level;

	/*
	 * Free the indirect blocks
	 */
	for (level = 0; level < MINIX_NIADDR; level++) {
	    if (fp->f_blk[level] != 0) {
		(void) vm_deallocate(mach_task_self(),
				     fp->f_blk[level],
				     fp->f_blksize[level]);
		fp->f_blk[level] = 0;
	    }
	    fp->f_blkno[level] = -1;
	}

	/*
	 * Free the data block
	 */
	if (fp->f_buf != 0) {
	    (void) vm_deallocate(mach_task_self(),
				 fp->f_buf,
				 fp->f_buf_size);
	    fp->f_buf = 0;
	}
	fp->f_buf_blkno = -1;
}

/*
 * Read a new inode into a file structure.
 */
static int
read_inode(inumber, fp)
	ino_t			inumber;
	register struct file	*fp;
{
	vm_offset_t		buf;
	mach_msg_type_number_t	buf_size;
	register
	struct minix_super_block	*fs;
	minix_daddr_t		disk_block;
	kern_return_t		rc;

	fs = fp->f_fs;
	disk_block = minix_ino2blk(fs, inumber);

	rc = device_read(fp->f_dev,
			 0,
			 (recnum_t) minix_fsbtodb(fp->f_fs, disk_block),
			 (int) MINIX_BLOCK_SIZE,
			 (char **)&buf,
			 &buf_size);
	if (rc != KERN_SUCCESS)
	    return (rc);

	{
	    register struct minix_inode *dp;

	    dp = (struct minix_inode *)buf;
	    dp += minix_itoo(fs, inumber);
	    fp->i_ic = *dp;
	    fp->f_size = dp->i_size;
	}

	(void) vm_deallocate(mach_task_self(), buf, buf_size);

	/*
	 * Clear out the old buffers
	 */
	free_file_buffers(fp);

	return (0);
}

/*
 * Given an offset in a file, find the disk block number that
 * contains that block.
 */
static int
block_map(fp, file_block, disk_block_p)
	struct file	*fp;
	minix_daddr_t	file_block;
	minix_daddr_t	*disk_block_p;	/* out */
{
	int		level;
	int		idx;
	minix_daddr_t	ind_block_num;
	kern_return_t	rc;

	vm_offset_t	olddata[MINIX_NIADDR+1];
	vm_size_t	oldsize[MINIX_NIADDR+1];

	/*
	 * Index structure of an inode:
	 *
	 * i_db[0..NDADDR-1]	hold block numbers for blocks
	 *			0..NDADDR-1
	 *
	 * i_ib[0]		index block 0 is the single indirect
	 *			block
	 *			holds block numbers for blocks
	 *			NDADDR .. NDADDR + NINDIR(fs)-1
	 *
	 * i_ib[1]		index block 1 is the double indirect
	 *			block
	 *			holds block numbers for INDEX blocks
	 *			for blocks
	 *			NDADDR + NINDIR(fs) ..
	 *			NDADDR + NINDIR(fs) + NINDIR(fs)**2 - 1
	 *
	 * i_ib[2]		index block 2 is the triple indirect
	 *			block
	 *			holds block numbers for double-indirect
	 *			blocks for blocks
	 *			NDADDR + NINDIR(fs) + NINDIR(fs)**2 ..
	 *			NDADDR + NINDIR(fs) + NINDIR(fs)**2
	 *				+ NINDIR(fs)**3 - 1
	 */

	mutex_lock(&fp->f_lock);

	if (file_block < MINIX_NDADDR) {
	    /* Direct block. */
	    *disk_block_p = fp->i_ic.i_zone[file_block];
	    mutex_unlock(&fp->f_lock);
	    return (0);
	}

	file_block -= MINIX_NDADDR;

	/*
	 * nindir[0] = NINDIR
	 * nindir[1] = NINDIR**2
	 * nindir[2] = NINDIR**3
	 *	etc
	 */
	for (level = 0; level < MINIX_NIADDR; level++) {
	    if (file_block < fp->f_nindir[level])
		break;
	    file_block -= fp->f_nindir[level];
	}
	if (level == MINIX_NIADDR) {
	    /* Block number too high */
	    mutex_unlock(&fp->f_lock);
	    return (FS_NOT_IN_FILE);
	}

	ind_block_num = fp->i_ic.i_zone[level + MINIX_NDADDR];

	/*
	 * Initialize array of blocks to free.
	 */
	for (idx = 0; idx < MINIX_NIADDR; idx++)
	    oldsize[idx] = 0;

	for (; level >= 0; level--) {

	    vm_offset_t	data;
	    mach_msg_type_number_t	size;

	    if (ind_block_num == 0)
		break;

	    if (fp->f_blkno[level] == ind_block_num) {
		/*
		 *	Cache hit.  Just pick up the data.
		 */

		data = fp->f_blk[level];
	    }
	    else {
		/*
		 *	Drop our lock while doing the read.
		 *	(The f_dev and f_fs fields don`t change.)
		 */
		mutex_unlock(&fp->f_lock);

		rc = device_read(fp->f_dev,
				0,
				(recnum_t) minix_fsbtodb(fp->f_fs, ind_block_num),
				MINIX_BLOCK_SIZE,
				(char **)&data,
				&size);
		if (rc != KERN_SUCCESS)
		    return (rc);

		/*
		 *	See if we can cache the data.  Need a write lock to
		 *	do this.  While we hold the write lock, we can`t do
		 *	*anything* which might block for memory.  Otherwise
		 *	a non-privileged thread might deadlock with the
		 *	privileged threads.  We can`t block while taking the
		 *	write lock.  Otherwise a non-privileged thread
		 *	blocked in the vm_deallocate (while holding a read
		 *	lock) will block a privileged thread.  For the same
		 *	reason, we can`t take a read lock and then use
		 *	lock_read_to_write.
		 */

		mutex_lock(&fp->f_lock);

		olddata[level] = fp->f_blk[level];
		oldsize[level] = fp->f_blksize[level];

		fp->f_blkno[level] = ind_block_num;
		fp->f_blk[level] = data;
		fp->f_blksize[level] = size;

		/*
		 *	Return to holding a read lock, and
		 *	dispose of old data.
		 */

	    }

	    if (level > 0) {
		idx = file_block / fp->f_nindir[level-1];
		file_block %= fp->f_nindir[level-1];
	    }
	    else
		idx = file_block;

	    ind_block_num = ((minix_daddr_t *)data)[idx];
	}

	mutex_unlock(&fp->f_lock);

	/*
	 * After unlocking the file, free any blocks that
	 * we need to free.
	 */
	for (idx = 0; idx < MINIX_NIADDR; idx++)
	    if (oldsize[idx] != 0)
		(void) vm_deallocate(mach_task_self(),
				     olddata[idx],
				     oldsize[idx]);

	*disk_block_p = ind_block_num;
	return (0);
}

/*
 * Read a portion of a file into an internal buffer.  Return
 * the location in the buffer and the amount in the buffer.
 */
static int
buf_read_file(fp, offset, buf_p, size_p)
	register struct file	*fp;
	vm_offset_t		offset;
	vm_offset_t		*buf_p;		/* out */
	vm_size_t		*size_p;	/* out */
{
	register
	struct minix_super_block	*fs;
	vm_offset_t		off;
	register minix_daddr_t	file_block;
	minix_daddr_t		disk_block;
	int			rc;
	vm_offset_t		block_size;

	if (offset >= fp->i_ic.i_size)
	    return (FS_NOT_IN_FILE);

	fs = fp->f_fs;

	off = minix_blkoff(fs, offset);
	file_block = minix_lblkno(fs, offset);
	block_size = minix_blksize(fs, fp, file_block);

	if (((daddr_t) file_block) != fp->f_buf_blkno) {
	    rc = block_map(fp, file_block, &disk_block);
	    if (rc != 0)
		return (rc);

	    if (fp->f_buf)
		(void)vm_deallocate(mach_task_self(),
				    fp->f_buf,
				    fp->f_buf_size);

	    if (disk_block == 0) {
		(void)vm_allocate(mach_task_self(),
				  &fp->f_buf,
				  block_size,
				  TRUE);
		fp->f_buf_size = block_size;
	    }
	    else {
		rc = device_read(fp->f_dev,
				0,
				(recnum_t) minix_fsbtodb(fs, disk_block),
				(int) block_size,
				(char **) &fp->f_buf,
				(mach_msg_type_number_t *)&fp->f_buf_size);
	    }
	    if (rc)
		return (rc);

	    fp->f_buf_blkno = (daddr_t) file_block;
	}

	/*
	 * Return address of byte in buffer corresponding to
	 * offset, and size of remainder of buffer after that
	 * byte.
	 */
	*buf_p = fp->f_buf + off;
	*size_p = block_size - off;

	/*
	 * But truncate buffer at end of file.
	 */
	if (*size_p > fp->i_ic.i_size - offset)
	    *size_p = fp->i_ic.i_size - offset;

	return (0);
}

/*
 * Search a directory for a name and return its
 * i_number.
 */
static int
search_directory(name, fp, inumber_p)
	char *		name;
	register struct file *fp;
	ino_t		*inumber_p;	/* out */
{
	vm_offset_t	buf;
	vm_size_t	buf_size;
	vm_offset_t	offset;
	register struct minix_directory_entry *dp;
	int		length;
	kern_return_t	rc;
	char		tmp_name[15];

	length = strlen(name);

	offset = 0;
	while (offset < fp->i_ic.i_size) {
	    rc = buf_read_file(fp, offset, &buf, &buf_size);
	    if (rc != KERN_SUCCESS)
		return (rc);

	    dp = (struct minix_directory_entry *)buf;
	    if (dp->inode != 0) {
		strncpy (tmp_name, dp->name, MINIX_NAME_LEN /* XXX it's 14 */);
		tmp_name[MINIX_NAME_LEN] = '\0';
		if (strlen(tmp_name) == length &&
		    !strcmp(name, tmp_name))
	    	{
		    /* found entry */
		    *inumber_p = dp->inode;
		    return (0);
		}
	    }
	    offset += 16 /* MINIX dir. entry length - MINIX FS Ver. 1. */;
	}
	return (FS_NO_ENTRY);
}

static int
read_fs(dev, fsp)
	mach_port_t dev;
	struct minix_super_block **fsp;
{
	register
	struct minix_super_block *fs;
	vm_offset_t		buf;
	mach_msg_type_number_t	buf_size;
	int			error;

	/*
	 * Read the super block
	 */
	error = device_read(dev, 0, (recnum_t) MINIX_SBLOCK, MINIX_SBSIZE,
			    (char **) &buf, &buf_size);
	if (error)
	    return (error);

	/*
	 * Check the superblock
	 */
	fs = (struct minix_super_block *)buf;
	if (fs->s_magic != MINIX_SUPER_MAGIC) {
		(void) vm_deallocate(mach_task_self(), buf, buf_size);
		return (FS_INVALID_FS);
	}


	*fsp = fs;

	return 0;
}

static int
mount_fs(fp)
	register struct file	*fp;
{
	register struct minix_super_block *fs;
	int error;

	error = read_fs(fp->f_dev, &fp->f_fs);
	if (error)
	    return (error);

	fs = fp->f_fs;

	/*
	 * Calculate indirect block levels.
	 */
	{
	    register int	mult;
	    register int	level;

	    mult = 1;
	    for (level = 0; level < MINIX_NIADDR; level++) {
		mult *= MINIX_NINDIR(fs);
		fp->f_nindir[level] = mult;
	    }
	}

	return (0);
}

static void
unmount_fs(fp)
	register struct file	*fp;
{
	if (file_is_structured(fp)) {
	    (void) vm_deallocate(mach_task_self(),
				 (vm_offset_t) fp->f_fs,
				 MINIX_SBSIZE);
	    fp->f_fs = 0;
	}
}

/*
 * Open a file.
 */
int
minix_open_file(master_device_port, path, fp)
	mach_port_t	master_device_port;
	char *		path;
	struct file	*fp;
{
#define	RETURN(code)	{ rc = (code); goto exit; }

	register char	*cp, *component;
	register int	c;	/* char */
	register int	rc;
	ino_t		inumber, parent_inumber;
	int		nlinks = 0;

	char	namebuf[MAXPATHLEN+1];

	if (path == 0 || *path == '\0') {
	    return FS_NO_ENTRY;
	}

	/*
	 * Copy name into buffer to allow modifying it.
	 */
	strcpy(namebuf, path);

	/*
	 * Look for '/dev/xxx' at start of path, for
	 * root device.
	 */
	if (!strprefix(namebuf, "/dev/")) {
	    printf("no device name\n");
	    return FS_NO_ENTRY;
	}

	cp = namebuf + 5;	/* device */
	component = cp;
	while ((c = *cp) != '\0' && c != '/') {
	    cp++;
	}
	*cp = '\0';

	bzero (fp, sizeof (struct file));

	rc = device_open(master_device_port,
			D_READ|D_WRITE,
			component,
			&fp->f_dev);
	if (rc)
	    return rc;

	if (c == 0) {
	    fp->f_fs = 0;
	    goto out_ok;
	}

	*cp = c;

	rc = mount_fs(fp);
	if (rc)
	    return rc;

	inumber = (ino_t) MINIX_ROOTINO;
	if ((rc = read_inode(inumber, fp)) != 0) {
	    printf("can't read root inode\n");
	    goto exit;
	}

	while (*cp) {

	    /*
	     * Check that current node is a directory.
	     */
	    if ((fp->i_ic.i_mode & IFMT) != IFDIR)
		RETURN (FS_NOT_DIRECTORY);

	    /*
	     * Remove extra separators
	     */
	    while (*cp == '/')
		cp++;

	    /*
	     * Get next component of path name.
	     */
	    component = cp;
	    {
		register int	len = 0;

		while ((c = *cp) != '\0' && c != '/') {
		    if (len++ > MINIX_MAXNAMLEN)
			RETURN (FS_NAME_TOO_LONG);
		    if (c & 0200)
			RETURN (FS_INVALID_PARAMETER);
		    cp++;
		}
		*cp = 0;
	    }

	    /*
	     * Look up component in current directory.
	     * Save directory inumber in case we find a
	     * symbolic link.
	     */
	    parent_inumber = inumber;
	    rc = search_directory(component, fp, &inumber);
	    if (rc) {
		printf("%s: not found\n", path);
		goto exit;
	    }
	    *cp = c;

	    /*
	     * Open next component.
	     */
	    if ((rc = read_inode(inumber, fp)) != 0)
		goto exit;

	    /*
	     * Check for symbolic link.
	     */
	}

	/*
	 * Found terminal component.
	 */
    out_ok:
	mutex_init(&fp->f_lock);
	return 0;

	/*
	 * At error exit, close file to free storage.
	 */
    exit:
	minix_close_file(fp);
	return rc;
}

/*
 * Close file - free all storage used.
 */
void
minix_close_file(fp)
	register struct file	*fp;
{
	register int	i;

	/*
	 * Free the disk super-block.
	 */
	unmount_fs(fp);

	/*
	 * Free the inode and data buffers.
	 */
	free_file_buffers(fp);
}

int
minix_file_is_directory(struct file *fp)
{
	return (fp->i_ic.i_mode & IFMT) == IFDIR;
}

int
minix_file_is_regular(struct file *fp)
{
	return (fp->i_ic.i_mode & IFMT) == IFREG;
}

/*
 * Copy a portion of a file into kernel memory.
 * Cross block boundaries when necessary.
 */
int
minix_read_file(fp, offset, start, size, resid)
	register struct file	*fp;
	vm_offset_t		offset;
	vm_offset_t		start;
	vm_size_t		size;
	vm_size_t		*resid;	/* out */
{
	int			rc;
	register vm_size_t	csize;
	vm_offset_t		buf;
	vm_size_t		buf_size;

	while (size != 0) {
	    rc = buf_read_file(fp, offset, &buf, &buf_size);
	    if (rc)
		return (rc);

	    csize = size;
	    if (csize > buf_size)
		csize = buf_size;
	    if (csize == 0)
		break;

	    bcopy((char *)buf, (char *)start, csize);

	    offset += csize;
	    start  += csize;
	    size   -= csize;
	}
	if (resid)
	    *resid = size;

	return (0);
}

/* simple utility: only works for 2^n */
static int
log2(n)
	register unsigned int n;
{
	register int i = 0;

	while ((n & 1) == 0) {
		i++;
		n >>= 1;
	}
	return i;
}

/*
 * Make an empty file_direct for a device.
 */
int
minix_open_file_direct(dev, fdp, is_structured)
	mach_port_t	dev;
	register struct file_direct *fdp;
	boolean_t	is_structured;
{
	struct minix_super_block *fs;
	int			rc;

	if (!is_structured) {
		fdp->fd_dev     = dev;
		fdp->fd_blocks  = (daddr_t *) 0;
		fdp->fd_bsize   = vm_page_size;
		fdp->fd_bshift  = log2(vm_page_size);
		fdp->fd_fsbtodb = 0;	/* later */
		fdp->fd_size    = 0;	/* later */
		return 0;
	}

	rc = read_fs(dev, &fs);
	if (rc)
		return rc;

	fdp->fd_dev = dev;
	fdp->fd_blocks = (daddr_t *) 0;
	fdp->fd_size = 0;
	fdp->fd_bsize = MINIX_BLOCK_SIZE;
	fdp->fd_bshift = log2(fdp->fd_bsize);
	fdp->fd_fsbtodb = log2(fdp->fd_bsize / DEV_BSIZE);

	(void) vm_deallocate(mach_task_self(),
			     (vm_offset_t) fs,
			     MINIX_SBSIZE);

	return 0;
}

/*
 * Add blocks from a file to a file_direct.
 */
int
minix_add_file_direct(fdp, fp)
	register struct file_direct *fdp;
	register struct file *fp;
{
	register struct minix_super_block *fs;
	long num_blocks, i;
	vm_offset_t buffer;
	vm_size_t size;
	int rc;

	/* the file must be on the same device */

	if (fdp->fd_dev != fp->f_dev)
		return FS_INVALID_FS;

	if (!file_is_structured(fp)) {
		int	result[DEV_GET_SIZE_COUNT];
		natural_t count;

		count = DEV_GET_SIZE_COUNT;
		rc = device_get_status(	fdp->fd_dev, DEV_GET_SIZE,
					result, &count);
		if (rc)
			return rc;
		fdp->fd_size = result[DEV_GET_SIZE_DEVICE_SIZE] >> fdp->fd_bshift;
		fdp->fd_fsbtodb = log2(fdp->fd_bsize/result[DEV_GET_SIZE_RECORD_SIZE]);
		return 0;
	}

	/* it must hold a file system */

	fs = fp->f_fs;
/*
	if (fdp->fd_bsize != fs->fs_bsize ||
	    fdp->fd_fsbtodb != fs->fs_fsbtodb)
*/
	if (fdp->fd_bsize != MINIX_BLOCK_SIZE)
		return FS_INVALID_FS;

	/* calculate number of blocks in the file, ignoring fragments */

	num_blocks = minix_lblkno(fs, fp->i_ic.i_size);

	/* allocate memory for a bigger array */

	size = (num_blocks + fdp->fd_size) * sizeof(minix_daddr_t);
	rc = vm_allocate(mach_task_self(), &buffer, size, TRUE);
	if (rc != KERN_SUCCESS)
		return rc;

	/* lookup new block addresses */

	for (i = 0; i < num_blocks; i++) {
		minix_daddr_t disk_block;

		rc = block_map(fp, (minix_daddr_t) i, &disk_block);
		if (rc != 0) {
			(void) vm_deallocate(mach_task_self(), buffer, size);
			return rc;
		}

		((minix_daddr_t *) buffer)[fdp->fd_size + i] = disk_block;
	}

	/* copy old addresses and install the new array */

	if (fdp->fd_blocks != 0) {
		bcopy((char *) fdp->fd_blocks, (char *) buffer,
		      fdp->fd_size * sizeof(minix_daddr_t));

		(void) vm_deallocate(mach_task_self(),
				(vm_offset_t) fdp->fd_blocks,
				(vm_size_t) (fdp->fd_size * sizeof(minix_daddr_t)));
	}
	fdp->fd_blocks = (daddr_t *) buffer;
	fdp->fd_size += num_blocks;

	/* deallocate cached blocks */

	free_file_buffers(fp);

	return 0;
}

int
minix_remove_file_direct(fdp)
	struct file_direct	*fdp;
{
	if (fdp->fd_blocks)
	(void) vm_deallocate(mach_task_self(),
			     (vm_offset_t) fdp->fd_blocks,
			     (vm_size_t) (fdp->fd_size * sizeof(minix_daddr_t)));
	fdp->fd_blocks = 0; /* sanity */
	/* xxx should lose a ref to fdp->fd_dev here (and elsewhere) xxx */
}
