#ifndef UMSDOS_FS_I_H
#define UMSDOS_FS_I_H

#ifndef _LINUX_TYPES_H
#include <linux/types.h>
#endif

#include <linux/msdos_fs_i.h>
#include <linux/pipe_fs_i.h>

/* #Specification: strategy / in memory inode
 * Here is the information specific to the inode of the UMSDOS file
 * system. This information is added to the end of the standard struct
 * inode. Each file system has its own extension to struct inode,
 * so do the umsdos file system.
 * 
 * The strategy is to have the umsdos_inode_info as a superset of
 * the msdos_inode_info, since most of the time the job is done
 * by the msdos fs code.
 * 
 * So we duplicate the msdos_inode_info, and add our own info at the
 * end.
 * 
 * For all file type (and directory) the inode has a reference to:
 * the directory which hold this entry: i_dir_owner
 * The EMD file of i_dir_owner: i_emd_owner
 * The offset in this EMD file of the entry: pos
 * 
 * For directory, we also have a reference to the inode of its
 * own EMD file. Also, we have dir_locking_info to help synchronise
 * file creation and file lookup. This data is sharing space with
 * the pipe_inode_info not used by directory. See also msdos_fs_i.h
 * for more information about pipe_inode_info and msdos_inode_info.
 * 
 * Special file and fifo do have an inode which correspond to an
 * empty MSDOS file.
 * 
 * symlink are processed mostly like regular file. The content is the
 * link.
 * 
 * fifos add there own extension to the inode. I have reserved some
 * space for fifos side by side with msdos_inode_info. This is just
 * to for the show, because msdos_inode_info already include the
 * pipe_inode_info.
 * 
 * The UMSDOS specific extension is placed after the union.
 */

struct dir_locking_info {
	struct wait_queue *p;
	short int looking;	/* How many process doing a lookup */
	short int creating;	/* Is there any creation going on here
				 *  Only one at a time, although one
				 *  may recursively lock, so it is a counter
				 */
	long pid;		/* pid of the process owning the creation */
	/* lock */
};

struct umsdos_inode_info {
	union {
		struct msdos_inode_info msdos_info;
		struct pipe_inode_info pipe_info;
		struct dir_locking_info dir_info;
	} u;
	int i_patched;			/* Inode has been patched */
	int i_is_hlink;			/* Resolved hardlink inode? */
	unsigned long i_emd_owner;	/* Is this the EMD file inode? */
	off_t pos;			/* Entry offset in the emd_owner file */
	/* The rest is used only if this inode describes a directory */
	struct dentry *i_emd_dentry;	/* EMD dentry for this directory */
	unsigned long i_emd_dir;	/* Inode of the EMD file */
};

#endif
