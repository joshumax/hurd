/*
 *  Name                         : qnx4_fs.h
 *  Author                       : Richard Frowijn
 *  Function                     : qnx4 global filesystem definitions
 *  Version                      : 1.0
 *  Last modified                : 23-03-1998
 *
 *  History                      : 23-03-1998 created
 */
#ifndef _LINUX_QNX4_FS_H
#define _LINUX_QNX4_FS_H

#include <linux/qnxtypes.h>

#define QNX4_ROOT_INO 1

#define _MAX_XTNTS_PER_XBLK	60
/* for di_status */
#define QNX4_FILE_USED          0x01
#define QNX4_FILE_MODIFIED      0x02
#define QNX4_FILE_BUSY          0x04
#define QNX4_FILE_LINK          0x08
#define QNX4_FILE_INODE         0x10
#define QNX4_FILE_FSYSCLEAN     0x20

#define QNX4_I_MAP_SLOTS	8
#define QNX4_Z_MAP_SLOTS	64
#define QNX4_SUPER_MAGIC	0x002f	/* qnx4 fs detection */
#define QNX4_VALID_FS		0x0001	/* Clean fs. */
#define QNX4_ERROR_FS		0x0002	/* fs has errors. */
#define QNX4_BLOCK_SIZE         0x200	/* blocksize of 512 bytes */
#define QNX4_DIR_ENTRY_SIZE     0x040	/* dir entry size */
#define QNX4_XBLK_ENTRY_SIZE    0x200	/* xblk entry size */
#define QNX4_INODES_PER_BLOCK   0x08	/* 512 / 64 */

/* for filenames */
#define _SHORT_NAME_MAX        16
#define QNX4_NAME_MAX          48

/*
 * This is the original qnx4 inode layout on disk.
 */
struct qnx4_inode_entry {
	char di_fname[16];
	off_t di_size;
	_xtnt_t di_first_xtnt;
	long di_xblk;
	time_t di_ftime;
	time_t di_mtime;
	time_t di_atime;
	time_t di_ctime;
	_nxtnt_t di_num_xtnts;
	mode_t di_mode;
	muid_t di_uid;
	mgid_t di_gid;
	nlink_t di_nlink;
	char di_zero[4];
	_ftype_t di_type;
	unsigned char di_status;
};

struct qnx4_link_info {
	char dl_fname[QNX4_NAME_MAX];
	long dl_inode_blk;
	unsigned char dl_inode_ndx;
	unsigned char dl_spare[10];
	unsigned char dl_status;
};

struct qnx4_xblk {
	long xblk_next_xblk;
	long xblk_prev_xblk;
	unsigned char xblk_num_xtnts;
	char xblk_spare[3];
	long xblk_num_blocks;
	_xtnt_t xblk_xnts[_MAX_XTNTS_PER_XBLK];
	char xblk_signature[8];
	_xtnt_t xblk_first_xtnt;
};

struct qnx4_super_block {
	struct qnx4_inode_entry RootDir;
	struct qnx4_inode_entry Inode;
	struct qnx4_inode_entry Boot;
	struct qnx4_inode_entry AltBoot;
};

#ifdef __KERNEL__

#define QNX4_DEBUG 0

#if QNX4_DEBUG
#define QNX4DEBUG(X) printk X
#else
#define QNX4DEBUG(X) (void) 0
#endif

extern struct dentry *qnx4_lookup(struct inode *dir, struct dentry *dentry);
extern unsigned long qnx4_count_free_inodes(struct super_block *sb);
extern unsigned long qnx4_count_free_blocks(struct super_block *sb);

extern struct buffer_head *qnx4_getblk(struct inode *, int, int);
extern struct buffer_head *qnx4_bread(struct inode *, int, int);

extern int init_qnx4_fs(void);
extern int qnx4_create(struct inode *dir, struct dentry *dentry, int mode);
extern struct inode_operations qnx4_file_inode_operations;
extern struct inode_operations qnx4_dir_inode_operations;
extern struct inode_operations qnx4_symlink_inode_operations;
extern int qnx4_is_free(struct super_block *sb, int block);
extern int qnx4_set_bitmap(struct super_block *sb, int block, int busy);
extern int qnx4_create(struct inode *inode, struct dentry *dentry, int mode);
extern void qnx4_truncate(struct inode *inode);
extern void qnx4_free_inode(struct inode *inode);
extern int qnx4_unlink(struct inode *dir, struct dentry *dentry);
extern int qnx4_rmdir(struct inode *dir, struct dentry *dentry);
extern int qnx4_sync_file(struct file *file, struct dentry *dentry);
extern int qnx4_sync_inode(struct inode *inode);
extern int qnx4_bmap(struct inode *inode, int block);

#endif				/* __KERNEL__ */

#endif
