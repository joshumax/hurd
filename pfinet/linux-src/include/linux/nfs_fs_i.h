#ifndef _NFS_FS_I
#define _NFS_FS_I

#include <linux/nfs.h>
#include <linux/pipe_fs_i.h>

/*
 * nfs fs inode data in memory
 */
struct nfs_inode_info {
	/*
	 * This is a place holder so named pipes on NFS filesystems
	 * work (more or less correctly). This must be first in the
	 * struct because the data is really accessed via inode->u.pipe_i.
	 */
	struct pipe_inode_info	pipeinfo;

	/*
	 * Various flags
	 */
	unsigned short		flags;

	/*
	 * read_cache_jiffies is when we started read-caching this inode,
	 * and read_cache_mtime is the mtime of the inode at that time.
	 * attrtimeo is for how long the cached information is assumed
	 * to be valid. A successful attribute revalidation doubles
	 * attrtimeo (up to acregmax/acdirmax), a failure resets it to
	 * acregmin/acdirmin.
	 *
	 * We need to revalidate the cached attrs for this inode if
	 *
	 *	jiffies - read_cache_jiffies > attrtimeo
	 *
	 * and invalidate any cached data/flush out any dirty pages if
	 * we find that
	 *
	 *	mtime != read_cache_mtime
	 */
	unsigned long		read_cache_jiffies;
	unsigned long		read_cache_mtime;
	unsigned long		attrtimeo;

	/*
	 * This is the list of dirty unwritten pages.
	 * NFSv3 will want to add a list for written but uncommitted
	 * pages.
	 */
	struct nfs_wreq *	writeback;
};

/*
 * Legal inode flag values
 */
#define NFS_INO_REVALIDATE	0x0001		/* revalidating attrs */
#define NFS_IS_SNAPSHOT		0x0010		/* a snapshot file */

/*
 * NFS lock info
 */
struct nfs_lock_info {
	u32		state;
	u32		flags;
};

/*
 * Lock flag values
 */
#define NFS_LCK_GRANTED		0x0001		/* lock has been granted */

#endif
