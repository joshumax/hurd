/*
 *  linux/include/linux/adfs_fs_sb.h
 *
 * Copyright (C) 1997 Russell King
 */

#ifndef _ADFS_FS_SB
#define _ADFS_FS_SB

#include <linux/adfs_fs.h>

/*
 * adfs file system superblock data in memory
 */
struct adfs_sb_info {
	struct buffer_head *s_sbh;	/* buffer head containing disc record	 */
	struct adfs_discrecord *s_dr;	/* pointer to disc record in s_sbh	 */
	uid_t	s_uid;			/* owner uid				 */
	gid_t	s_gid;			/* owner gid				 */
	int	s_owner_mask;		/* ADFS Owner perm -> unix perm		 */
	int	s_other_mask;		/* ADFS Other perm -> unix perm		 */
	__u16	s_zone_size;		/* size of a map zone in bits		 */
	__u16	s_ids_per_zone;		/* max. no ids in one zone		 */
	__u32	s_idlen;		/* length of ID in map			 */
	__u32	s_map_size;		/* size of a map			 */
	__u32	s_zonesize;		/* zone size (in map bits)		 */
	__u32	s_map_block;		/* block address of map			 */
	struct buffer_head **s_map;	/* bh list containing map		 */
	__u32	s_root;			/* root disc address			 */
	__s8	s_map2blk;		/* shift left by this for map->sector	 */
};

#endif
