/*
 *  ncp_fs_i.h
 *
 *  Copyright (C) 1995 Volker Lendecke
 *
 */

#ifndef _LINUX_NCP_FS_I
#define _LINUX_NCP_FS_I

#include <linux/ncp.h>

#ifdef __KERNEL__

enum ncp_inode_state {
	NCP_INODE_VALID = 19,	/* Inode currently in use */
	NCP_INODE_LOOKED_UP,	/* directly before iget */
	NCP_INODE_CACHED,	/* in a path to an inode which is in use */
	NCP_INODE_INVALID
};

/*
 * ncp fs inode data (in memory only)
 */
struct ncp_inode_info {
	enum ncp_inode_state state;
	int nused;		/* for directories:
				   number of references in memory */
	struct ncp_inode_info *dir;
	struct ncp_inode_info *next, *prev;
	struct inode *inode;
	struct nw_file_info finfo;
};

#endif
#endif
