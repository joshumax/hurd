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
#include "fs.h"
#include "dir.h"
#include "disk_inode_ffs.h"

void	close_file();	/* forward */

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
	for (level = 0; level < NIADDR; level++) {
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
	register struct fs	*fs;
	daddr_t			disk_block;
	kern_return_t		rc;

	fs = fp->f_fs;
	disk_block = itod(fs, inumber);

	rc = device_read(fp->f_dev,
			 0,
			 (recnum_t) fsbtodb(fp->f_fs, disk_block),
			 (int) fs->fs_bsize,
			 (char **)&buf,
			 &buf_size);
	if (rc != KERN_SUCCESS)
	    return (rc);

	{
	    register struct dinode *dp;

	    dp = (struct dinode *)buf;
	    dp += itoo(fs, inumber);
	    fp->i_ic = dp->di_ic;
	    fp->f_size = fp->i_size;
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
	daddr_t		file_block;
	daddr_t		*disk_block_p;	/* out */
{
	int		level;
	int		idx;
	daddr_t		ind_block_num;
	kern_return_t	rc;

	vm_offset_t	olddata[NIADDR+1];
	vm_size_t	oldsize[NIADDR+1];

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

	if (file_block < NDADDR) {
	    /* Direct block. */
	    *disk_block_p = fp->i_db[file_block];
	    mutex_unlock(&fp->f_lock);
	    return (0);
	}

	file_block -= NDADDR;

	/*
	 * nindir[0] = NINDIR
	 * nindir[1] = NINDIR**2
	 * nindir[2] = NINDIR**3
	 *	etc
	 */
	for (level = 0; level < NIADDR; level++) {
	    if (file_block < fp->f_nindir[level])
		break;
	    file_block -= fp->f_nindir[level];
	}
	if (level == NIADDR) {
	    /* Block number too high */
	    mutex_unlock(&fp->f_lock);
	    return (FS_NOT_IN_FILE);
	}

	ind_block_num = fp->i_ib[level];

	/*
	 * Initialize array of blocks to free.
	 */
	for (idx = 0; idx < NIADDR; idx++)
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
				(recnum_t) fsbtodb(fp->f_fs, ind_block_num),
				fp->f_fs->fs_bsize,
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

	    ind_block_num = ((daddr_t *)data)[idx];
	}

	mutex_unlock(&fp->f_lock);

	/*
	 * After unlocking the file, free any blocks that
	 * we need to free.
	 */
	for (idx = 0; idx < NIADDR; idx++)
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
	register struct fs	*fs;
	vm_offset_t		off;
	register daddr_t	file_block;
	daddr_t			disk_block;
	int			rc;
	vm_offset_t		block_size;

	if (offset >= fp->i_size)
	    return (FS_NOT_IN_FILE);

	fs = fp->f_fs;

	off = blkoff(fs, offset);
	file_block = lblkno(fs, offset);
	block_size = blksize(fs, fp, file_block);

	if (file_block != fp->f_buf_blkno) {
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
				(recnum_t) fsbtodb(fs, disk_block),
				(int) block_size,
				(char **) &fp->f_buf,
				(mach_msg_type_number_t *)&fp->f_buf_size);
	    }
	    if (rc)
		return (rc);

	    fp->f_buf_blkno = file_block;
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
	if (*size_p > fp->i_size - offset)
	    *size_p = fp->i_size - offset;

	return (0);
}

/* In 4.4 d_reclen is split into d_type and d_namlen */
struct dirent_44 {
	unsigned long	d_fileno;
	unsigned short	d_reclen;
	unsigned char	d_type;
	unsigned char	d_namlen;
	char		d_name[255 + 1];
};

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
	register struct dirent_44 *dp;
	int		length;
	kern_return_t	rc;

	length = strlen(name);

	offset = 0;
	while (offset < fp->i_size) {
	    rc = buf_read_file(fp, offset, &buf, &buf_size);
	    if (rc != KERN_SUCCESS)
		return (rc);

	    dp = (struct dirent_44 *)buf;
	    if (dp->d_ino != 0) {
		unsigned short namlen = dp->d_namlen;
		/*
		 * If namlen is zero, then either this is a 4.3 file
		 * system or the namlen is really zero.  In the latter
		 * case also the 4.3 d_namlen field is zero
		 * interpreted either way.
		 */
		if (namlen == 0)
		    namlen = ((struct direct *)dp)->d_namlen;

		if (namlen == length &&
		    !strcmp(name, dp->d_name))
	    	{
		    /* found entry */
		    *inumber_p = dp->d_ino;
		    return (0);
		}
	    }
	    offset += dp->d_reclen;
	}
	return (FS_NO_ENTRY);
}

static int
read_fs(dev, fsp)
	mach_port_t dev;
	struct fs **fsp;
{
	register struct fs *fs;
	vm_offset_t	buf;
	mach_msg_type_number_t	buf_size;
	int		error;

	error = device_read(dev, 0, (recnum_t) SBLOCK, SBSIZE,
			    (char **) &buf, &buf_size);
	if (error)
	    return (error);

	fs = (struct fs *)buf;
	if (fs->fs_magic != FS_MAGIC ||
	    fs->fs_bsize > MAXBSIZE ||
	    fs->fs_bsize < sizeof(struct fs)) {
		(void) vm_deallocate(mach_task_self(), buf, buf_size);
		return (FS_INVALID_FS);
	}
	/* don't read cylinder groups - we aren't modifying anything */

	*fsp = fs;
	return 0;
}

static int
mount_fs(fp)
	register struct file	*fp;
{
	register struct fs *fs;
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
	    for (level = 0; level < NIADDR; level++) {
		mult *= NINDIR(fs);
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
				 SBSIZE);
	    fp->f_fs = 0;
	}
}

/*
 * Open a file.
 */
int
ffs_open_file(master_device_port, path, fp)
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

	inumber = (ino_t) ROOTINO;
	if ((rc = read_inode(inumber, fp)) != 0) {
	    printf("can't read root inode\n");
	    goto exit;
	}

	while (*cp) {

	    /*
	     * Check that current node is a directory.
	     */
	    if ((fp->i_mode & IFMT) != IFDIR)
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
		    if (len++ > MAXNAMLEN)
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
	    if ((fp->i_mode & IFMT) == IFLNK) {

		int	link_len = fp->i_size;
		int	len;

		len = strlen(cp) + 1;

		if (link_len + len >= MAXPATHLEN - 1)
		    RETURN (FS_NAME_TOO_LONG);

		if (++nlinks > MAXSYMLINKS)
		    RETURN (FS_SYMLINK_LOOP);

		ovbcopy(cp, &namebuf[link_len], len);

#ifdef	IC_FASTLINK
		if ((fp->i_flags & IC_FASTLINK) != 0) {
		    bcopy(fp->i_symlink, namebuf, (unsigned) link_len);
		}
		else
#endif	IC_FASTLINK
#if !defined(DISABLE_BSD44_FASTLINKS)
		/*
		 * There is no bit for fastlinks in 4.4 but instead
		 * all symlinks that fit into the inode are fastlinks.
		 * If the second block (ic_db[1]) is zero the symlink
		 * can't be a fastlink if its length is at least five.
		 * For symlinks of length one to four there is no easy
		 * way of knowing whether we are looking at a 4.4
		 * fastlink or a 4.3 slowlink.  This code always
		 * guesses the 4.4 way when in doubt. THIS BREAKS 4.3
		 * SLOWLINKS OF LENGTH FOUR OR LESS.
		 */
		if ((link_len <= MAX_FASTLINK_SIZE && fp->i_ic.ic_db[1] != 0)
		     || (link_len <= 4))
		{
		    bcopy(fp->i_symlink, namebuf, (unsigned) link_len);
		}
		else
#endif        /* !DISABLE_BSD44_FASTLINKS */

		{
		    /*
		     * Read file for symbolic link
		     */
		    vm_offset_t	buf;
		    mach_msg_type_number_t	buf_size;
		    daddr_t	disk_block;
		    register struct fs *fs = fp->f_fs;

		    (void) block_map(fp, (daddr_t)0, &disk_block);
		    rc = device_read(fp->f_dev,
				     0,
				     (recnum_t) fsbtodb(fs, disk_block),
				     (int) blksize(fs, fp, 0),
				     (char **) &buf,
				     &buf_size);
		    if (rc)
			goto exit;

		    bcopy((char *)buf, namebuf, (unsigned)link_len);
		    (void) vm_deallocate(mach_task_self(), buf, buf_size);
		}

		/*
		 * If relative pathname, restart at parent directory.
		 * If absolute pathname, restart at root.
		 * If pathname begins '/dev/<device>/',
		 *	restart at root of that device.
		 */
		cp = namebuf;
		if (*cp != '/') {
		    inumber = parent_inumber;
		}
		else if (!strprefix(cp, "/dev/")) {
		    inumber = (ino_t)ROOTINO;
		}
		else {
		    cp += 5;
		    component = cp;
		    while ((c = *cp) != '\0' && c != '/') {
			cp++;
		    }
		    *cp = '\0';

		    /*
		     * Unmount current file system and free buffers.
		     */
		    close_file(fp);

		    /*
		     * Open new root device.
		     */
		    rc = device_open(master_device_port,
				D_READ,
				component,
				&fp->f_dev);
		    if (rc)
			return (rc);

		    if (c == 0) {
			fp->f_fs = 0;
			goto out_ok;
		    }

		    *cp = c;

		    rc = mount_fs(fp);
		    if (rc)
			return (rc);

		    inumber = (ino_t)ROOTINO;
		}
		if ((rc = read_inode(inumber, fp)) != 0)
		    goto exit;
	    }
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
	close_file(fp);
	return rc;
}

/*
 * Close file - free all storage used.
 */
void
ffs_close_file(fp)
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
ffs_file_is_directory(struct file *fp)
{
	return (fp->i_mode & IFMT) == IFDIR;
}

int
ffs_file_is_regular(struct file *fp)
{
	return (fp->i_mode & IFMT) == IFREG;
}

/*
 * Copy a portion of a file into kernel memory.
 * Cross block boundaries when necessary.
 */
int
ffs_read_file(fp, offset, start, size, resid)
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
ffs_open_file_direct(dev, fdp, is_structured)
	mach_port_t	dev;
	register struct file_direct *fdp;
	boolean_t	is_structured;
{
	struct fs *fs;
	int rc;

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
	fdp->fd_bsize = fs->fs_bsize;
	fdp->fd_bshift = fs->fs_bshift;
	fdp->fd_fsbtodb = fs->fs_fsbtodb;

	(void) vm_deallocate(mach_task_self(),
			     (vm_offset_t) fs,
			     SBSIZE);

	return 0;
}

/*
 * Add blocks from a file to a file_direct.
 */
int
ffs_add_file_direct(fdp, fp)
	register struct file_direct *fdp;
	register struct file *fp;
{
	register struct fs *fs;
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
	if (fdp->fd_bsize != fs->fs_bsize ||
	    fdp->fd_fsbtodb != fs->fs_fsbtodb)
		return FS_INVALID_FS;

	/* calculate number of blocks in the file, ignoring fragments */

	num_blocks = lblkno(fs, fp->i_size);

	/* allocate memory for a bigger array */

	size = (num_blocks + fdp->fd_size) * sizeof(daddr_t);
	rc = vm_allocate(mach_task_self(), &buffer, size, TRUE);
	if (rc != KERN_SUCCESS)
		return rc;

	/* lookup new block addresses */

	for (i = 0; i < num_blocks; i++) {
		daddr_t disk_block;

		rc = block_map(fp, (daddr_t) i, &disk_block);
		if (rc != 0) {
			(void) vm_deallocate(mach_task_self(), buffer, size);
			return rc;
		}

		((daddr_t *) buffer)[fdp->fd_size + i] = disk_block;
	}

	/* copy old addresses and install the new array */

	if (fdp->fd_blocks != 0) {
		bcopy((char *) fdp->fd_blocks, (char *) buffer,
		      fdp->fd_size * sizeof(daddr_t));

		(void) vm_deallocate(mach_task_self(),
				(vm_offset_t) fdp->fd_blocks,
				(vm_size_t) (fdp->fd_size * sizeof(daddr_t)));
	}
	fdp->fd_blocks = (daddr_t *) buffer;
	fdp->fd_size += num_blocks;

	/* deallocate cached blocks */

	free_file_buffers(fp);

	return 0;
}

int
ffs_remove_file_direct(fdp)
	struct file_direct	*fdp;
{
	if (fdp->fd_blocks)
	(void) vm_deallocate(mach_task_self(),
			     (vm_offset_t) fdp->fd_blocks,
			     (vm_size_t) (fdp->fd_size * sizeof(daddr_t)));
	fdp->fd_blocks = 0; /* sanity */
	/* xxx should lose a ref to fdp->fd_dev here (and elsewhere) xxx */
}
