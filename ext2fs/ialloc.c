/*
 * Largely stolen from: linux/fs/ext2/ialloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  BSD ufs-inspired inode and directory allocation by 
 *  Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 */

/*
 * ialloc.c contains the inodes allocation and deallocation routines
 */

/*
 * The free inodes are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.  The descriptors are loaded in memory
 * when a file system is mounted (see ext2_read_super).
 */

#include "ext2fs.h"

void ext2_free_inode (struct inode * inode)
{
	char * bh;
	char * bh2;
	unsigned long block_group;
	unsigned long bit;
	struct ext2_group_desc * gdp;
	ino_t inum = node->dn->number;

	if (!inode)
		return;

	ext2_debug ("freeing inode %lu\n", inode->i_ino);

	lock_super (sb);
	if (inum < EXT2_FIRST_INO ||
	    inum > sb->u.ext2_sb.s_es->s_inodes_count) {
		ext2_error (sb, "free_inode",
			    "reserved inode or nonexistent inode");
		unlock_super (sb);
		return;
	}
	es = sb->u.ext2_sb.s_es;
	block_group = (inum - 1) / EXT2_INODES_PER_GROUP(sb);
	bit = (inum - 1) % EXT2_INODES_PER_GROUP(sb);
	bitmap_nr = load_inode_bitmap (sb, block_group);
	bh = sb->u.ext2_sb.s_inode_bitmap[bitmap_nr];
	if (!clear_bit (bit, bh))
		ext2_warning (sb, "ext2_free_inode",
			      "bit already cleared for inode %lu", inum);
	else {
		gdp = get_group_desc (sb, block_group, &bh2);
		gdp->bg_free_inodes_count++;
		if (S_ISDIR(inode->i_mode))
			gdp->bg_used_dirs_count--;
		mark_buffer_dirty(bh2, 1);
		es->s_free_inodes_count++;
		mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
#if 0
		set_inode_dtime (inode, gdp);
#endif
	}
	mark_buffer_dirty(bh, 1);
	if (sb->s_flags & MS_SYNCHRONOUS) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}

	sb->s_dirt = 1;
	clear_inode (inode);
	unlock_super (sb);
}

/*
 * There are two policies for allocating an inode.  If the new inode is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-inode ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other inodes, search forward from the parent directory\'s block
 * group to find a free inode.
 */
struct inode * ext2_new_inode (const struct inode * dir, int mode)
{
	struct super_block * sb;
	char * bh;
	char * bh2;
	int i, j, avefreei;
	struct inode * inode;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	struct ext2_group_desc * tmp;
	struct ext2_super_block * es;

	if (!dir || !(inode = get_empty_inode ()))
		return NULL;
	sb = dir->i_sb;
	inode->i_sb = sb;
	inode->i_flags = sb->s_flags;
	lock_super (sb);
	es = sb->u.ext2_sb.s_es;
repeat:
	gdp = NULL; i=0;
	
	if (S_ISDIR(mode)) {
		avefreei = es->s_free_inodes_count /
			sb->u.ext2_sb.s_groups_count;
/* I am not yet convinced that this next bit is necessary.
		i = dir->u.ext2_i.i_block_group;
		for (j = 0; j < sb->u.ext2_sb.s_groups_count; j++) {
			tmp = get_group_desc (sb, i, &bh2);
			if ((tmp->bg_used_dirs_count << 8) < 
			    tmp->bg_free_inodes_count) {
				gdp = tmp;
				break;
			}
			else
			i = ++i % sb->u.ext2_sb.s_groups_count;
		}
*/
		if (!gdp) {
			for (j = 0; j < sb->u.ext2_sb.s_groups_count; j++) {
				tmp = get_group_desc (sb, j, &bh2);
				if (tmp->bg_free_inodes_count &&
					tmp->bg_free_inodes_count >= avefreei) {
					if (!gdp || 
					    (tmp->bg_free_blocks_count >
					     gdp->bg_free_blocks_count)) {
						i = j;
						gdp = tmp;
					}
				}
			}
		}
	}
	else 
	{
		/*
		 * Try to place the inode in its parent directory
		 */
		i = dir->u.ext2_i.i_block_group;
		tmp = get_group_desc (sb, i, &bh2);
		if (tmp->bg_free_inodes_count)
			gdp = tmp;
		else
		{
			/*
			 * Use a quadratic hash to find a group with a
			 * free inode
			 */
			for (j = 1; j < sb->u.ext2_sb.s_groups_count; j <<= 1) {
				i += j;
				if (i >= sb->u.ext2_sb.s_groups_count)
					i -= sb->u.ext2_sb.s_groups_count;
				tmp = get_group_desc (sb, i, &bh2);
				if (tmp->bg_free_inodes_count) {
					gdp = tmp;
					break;
				}
			}
		}
		if (!gdp) {
			/*
			 * That failed: try linear search for a free inode
			 */
			i = dir->u.ext2_i.i_block_group + 1;
			for (j = 2; j < sb->u.ext2_sb.s_groups_count; j++) {
				if (++i >= sb->u.ext2_sb.s_groups_count)
					i = 0;
				tmp = get_group_desc (sb, i, &bh2);
				if (tmp->bg_free_inodes_count) {
					gdp = tmp;
					break;
				}
			}
		}
	}

	if (!gdp) {
		unlock_super (sb);
		iput(inode);
		return NULL;
	}
	bitmap_nr = load_inode_bitmap (sb, i);
	bh = sb->u.ext2_sb.s_inode_bitmap[bitmap_nr];
	if ((j = find_first_zero_bit ((unsigned long *) bh,
				      EXT2_INODES_PER_GROUP(sb))) <
	    EXT2_INODES_PER_GROUP(sb)) {
		if (set_bit (j, bh)) {
			ext2_warning (sb, "ext2_new_inode",
				      "bit already set for inode %d", j);
			goto repeat;
		}
		mark_buffer_dirty(bh, 1);
		if (sb->s_flags & MS_SYNCHRONOUS) {
			ll_rw_block (WRITE, 1, &bh);
			wait_on_buffer (bh);
		}
	} else {
		if (gdp->bg_free_inodes_count != 0) {
			ext2_error (sb, "ext2_new_inode",
				    "Free inodes count corrupted in group %d",
				    i);
			unlock_super (sb);
			iput (inode);
			return NULL;
		}
		goto repeat;
	}
	j += i * EXT2_INODES_PER_GROUP(sb) + 1;
	if (j < EXT2_FIRST_INO || j > es->s_inodes_count) {
		ext2_error (sb, "ext2_new_inode",
			    "reserved inode or inode > inodes count - "
			    "block_group = %d,inode=%d", i, j);
		unlock_super (sb);
		iput (inode);
		return NULL;
	}
	gdp->bg_free_inodes_count--;
	if (S_ISDIR(mode))
		gdp->bg_used_dirs_count++;
	mark_buffer_dirty(bh2, 1);
	es->s_free_inodes_count--;
	mark_buffer_dirty(sb->u.ext2_sb.s_sbh, 1);
	sb->s_dirt = 1;
	inode->i_mode = mode;
	inode->i_sb = sb;
	inode->i_count = 1;
	inode->i_nlink = 1;
	inode->i_dev = sb->s_dev;
	inode->i_uid = current->fsuid;
	if (test_opt (sb, GRPID))
		inode->i_gid = dir->i_gid;
	else if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode->i_gid = current->fsgid;
	inode->i_dirt = 1;
	node->dn->number = j;
	inode->i_blksize = sb->s_blocksize;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->u.ext2_i.i_flags = dir->u.ext2_i.i_flags;
	if (S_ISLNK(mode))
		inode->u.ext2_i.i_flags &= ~(EXT2_IMMUTABLE_FL | EXT2_APPEND_FL);
	inode->u.ext2_i.i_faddr = 0;
	inode->u.ext2_i.i_frag_no = 0;
	inode->u.ext2_i.i_frag_size = 0;
	inode->u.ext2_i.i_file_acl = 0;
	inode->u.ext2_i.i_dir_acl = 0;
	inode->u.ext2_i.i_dtime = 0;
	inode->u.ext2_i.i_block_group = i;
	inode->i_op = NULL;
	if (inode->u.ext2_i.i_flags & EXT2_SYNC_FL)
		inode->i_flags |= MS_SYNCHRONOUS;
	insert_inode_hash(inode);
	inc_inode_version (inode, gdp, mode);

	ext2_debug ("allocating inode %lu\n", node->dn->number);

	unlock_super (sb);
	return inode;
}

unsigned long ext2_count_free_inodes (struct super_block * sb)
{
#ifdef EXT2FS_DEBUG
	struct ext2_super_block * es;
	unsigned long desc_count, bitmap_count, x;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	int i;

	lock_super (sb);
	es = sb->u.ext2_sb.s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < sb->u.ext2_sb.s_groups_count; i++) {
		gdp = get_group_desc (sb, i, NULL);
		desc_count += gdp->bg_free_inodes_count;
		bitmap_nr = load_inode_bitmap (sb, i);
		x = ext2_count_free (sb->u.ext2_sb.s_inode_bitmap[bitmap_nr],
				     EXT2_INODES_PER_GROUP(sb) / 8);
		printk ("group %d: stored = %d, counted = %lu\n",
			i, gdp->bg_free_inodes_count, x);
		bitmap_count += x;
	}
	printk("ext2_count_free_inodes: stored = %lu, computed = %lu, %lu\n",
		es->s_free_inodes_count, desc_count, bitmap_count);
	unlock_super (sb);
	return desc_count;
#else
	return sb->u.ext2_sb.s_es->s_free_inodes_count;
#endif
}

void ext2_check_inodes_bitmap (struct super_block * sb)
{
	struct ext2_super_block * es;
	unsigned long desc_count, bitmap_count, x;
	int bitmap_nr;
	struct ext2_group_desc * gdp;
	int i;

	lock_super (sb);
	es = sb->u.ext2_sb.s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < sb->u.ext2_sb.s_groups_count; i++) {
		gdp = get_group_desc (sb, i, NULL);
		desc_count += gdp->bg_free_inodes_count;
		bitmap_nr = load_inode_bitmap (sb, i);
		x = ext2_count_free (sb->u.ext2_sb.s_inode_bitmap[bitmap_nr],
				     EXT2_INODES_PER_GROUP(sb) / 8);
		if (gdp->bg_free_inodes_count != x)
			ext2_error (sb, "ext2_check_inodes_bitmap",
				    "Wrong free inodes count in group %d, "
				    "stored = %d, counted = %lu", i,
				    gdp->bg_free_inodes_count, x);
		bitmap_count += x;
	}
	if (es->s_free_inodes_count != bitmap_count)
		ext2_error (sb, "ext2_check_inodes_bitmap",
			    "Wrong free inodes count in super block, "
			    "stored = %lu, counted = %lu",
			    (unsigned long) es->s_free_inodes_count, bitmap_count);
	unlock_super (sb);
}
