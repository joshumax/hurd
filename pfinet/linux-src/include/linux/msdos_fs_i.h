#ifndef _MSDOS_FS_I
#define _MSDOS_FS_I

#ifndef _LINUX_PIPE_FS_I_H
#include <linux/pipe_fs_i.h>
#endif

/*
 * MS-DOS file system inode data in memory
 */

struct msdos_inode_info {
	/*
		UMSDOS manage special file and fifo as normal empty
		msdos file. fifo inode processing conflict with msdos
		processing. So I insert the pipe_inode_info so the
		information does not overlap. This increases the size of
		the msdos_inode_info, but the clear winner here is
		the ext2_inode_info. So it does not change anything to
		the total size of a struct inode.

		I have not put it conditional. With the advent of loadable
		file system drivers, it would be very easy to compile
		a MS-DOS FS driver unaware of UMSDOS and then later to
		load a (then incompatible) UMSDOS FS driver.
	*/
	struct pipe_inode_info reserved;
	int i_start;	/* first cluster or 0 */
	int i_logstart;	/* logical first cluster */
	int i_attrs;	/* unused attribute bits */
	int i_ctime_ms;	/* unused change time in milliseconds */
	int i_binary;	/* file contains non-text data */
	int i_location;	/* on-disk position of directory entry or 0 */
	struct inode *i_fat_inode;	/* struct inode of this one */
	struct list_head i_fat_hash;	/* hash by i_location */
	off_t i_last_pos;/* position of last lookup */
};

#endif
