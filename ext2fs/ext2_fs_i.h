/*
 *  linux/include/linux/ext2_fs_i.h
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/include/linux/minix_fs_i.h
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#ifndef _LINUX_EXT2_FS_I
#define _LINUX_EXT2_FS_I

/*
 * second extended file system inode data in memory
 */
struct ext2_inode_info {
	u32	i_data[15];
	u32	i_flags;
	u32	i_faddr;
	u8	i_frag_no;
	u8	i_frag_size;
	u16	i_osync;
	u32	i_file_acl;
	u32	i_dir_acl;
	u32	i_dtime;
	u32	i_version;
	u32	i_block_group;
	u32	i_next_alloc_block;
	u32	i_next_alloc_goal;
	u32	i_prealloc_block;
	u32	i_prealloc_count;
};

#endif	/* _LINUX_EXT2_FS_I */
