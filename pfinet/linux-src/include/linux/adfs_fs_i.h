/*
 *  linux/include/linux/adfs_fs_i.h
 *
 * Copyright (C) 1997 Russell King
 */

#ifndef _ADFS_FS_I
#define _ADFS_FS_I

/*
 * adfs file system inode data in memory
 */
struct adfs_inode_info {
	unsigned long	file_id;		/* id of fragments containing actual data */
};

#endif
