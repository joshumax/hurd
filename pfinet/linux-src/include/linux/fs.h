#ifndef _LINUX_FS_H
#define _LINUX_FS_H

/*
 * This file has definitions for some important file table
 * structures etc.
 */

#include <linux/config.h>
#include <linux/linkage.h>
#include <linux/limits.h>
#include <linux/wait.h>
#include <linux/types.h>
#include <linux/vfs.h>
#include <linux/net.h>
#include <linux/kdev_t.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/dcache.h>
#include <linux/stat.h>

#include <asm/atomic.h>
#include <linux/bitops.h>
#include <asm/cache.h>
#include <linux/stddef.h>	/* just in case the #define NULL previously in here was needed */

struct poll_table_struct;


/*
 * It's silly to have NR_OPEN bigger than NR_FILE, but you can change
 * the file limit at runtime and only root can increase the per-process
 * nr_file rlimit, so it's safe to set up a ridiculously high absolute
 * upper limit on files-per-process.
 *
 * Some programs (notably those using select()) may have to be 
 * recompiled to take full advantage of the new limits..  
 */

/* Fixed constants first: */
#undef NR_OPEN
#define NR_OPEN (1024*1024)	/* Absolute upper limit on fd num */
#define INR_OPEN 1024		/* Initial setting for nfile rlimits */

#define BLOCK_SIZE_BITS 10
#define BLOCK_SIZE (1<<BLOCK_SIZE_BITS)

/* And dynamically-tunable limits and defaults: */
extern int max_inodes;
extern int max_files, nr_files, nr_free_files;
extern int max_super_blocks, nr_super_blocks;

#define NR_FILE  4096	/* this can well be larger on a larger system */
#define NR_RESERVED_FILES 10 /* reserved for root */
#define NR_SUPER 256

#define MAY_EXEC 1
#define MAY_WRITE 2
#define MAY_READ 4

#define FMODE_READ 1
#define FMODE_WRITE 2

#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead  - don't block if no resources */
#define WRITEA 3	/* write-ahead - don't block if no resources */

#define NIL_FILP	((struct file *)0)
#define SEL_IN		1
#define SEL_OUT		2
#define SEL_EX		4

/* public flags for file_system_type */
#define FS_REQUIRES_DEV 1 
#define FS_NO_DCACHE    2 /* Only dcache the necessary things. */
#define FS_NO_PRELIM    4 /* prevent preloading of dentries, even if
			   * FS_NO_DCACHE is not set.
			   */
#define FS_IBASKET      8 /* FS does callback to free_ibasket() if space gets low. */

/*
 * These are the fs-independent mount-flags: up to 16 flags are supported
 */
#define MS_RDONLY	 1	/* Mount read-only */
#define MS_NOSUID	 2	/* Ignore suid and sgid bits */
#define MS_NODEV	 4	/* Disallow access to device special files */
#define MS_NOEXEC	 8	/* Disallow program execution */
#define MS_SYNCHRONOUS	16	/* Writes are synced at once */
#define MS_REMOUNT	32	/* Alter flags of a mounted FS */
#define MS_MANDLOCK	64	/* Allow mandatory locks on an FS */
#define S_QUOTA		128	/* Quota initialized for file/directory/symlink */
#define S_APPEND	256	/* Append-only file */
#define S_IMMUTABLE	512	/* Immutable file */
#define MS_NOATIME	1024	/* Do not update access times. */
#define MS_NODIRATIME   2048    /* Do not update directory access times */

#define MS_ODD_RENAME   32768    /* Temporary stuff; will go away as soon
				  * as nfs_rename() will be cleaned up
				  */

/*
 * Flags that can be altered by MS_REMOUNT
 */
#define MS_RMT_MASK (MS_RDONLY|MS_NOSUID|MS_NODEV|MS_NOEXEC|MS_SYNCHRONOUS|MS_MANDLOCK|MS_NOATIME|MS_NODIRATIME)

/*
 * Magic mount flag number. Has to be or-ed to the flag values.
 */
#define MS_MGC_VAL 0xC0ED0000	/* magic flag number to indicate "new" flags */
#define MS_MGC_MSK 0xffff0000	/* magic flag number mask */

/*
 * Note that read-only etc flags are inode-specific: setting some file-system
 * flags just means all the inodes inherit those flags by default. It might be
 * possible to override it selectively if you really wanted to with some
 * ioctl() that is not currently implemented.
 *
 * Exception: MS_RDONLY is always applied to the entire file system.
 *
 * Unfortunately, it is possible to change a filesystems flags with it mounted
 * with files in use.  This means that all of the inodes will not have their
 * i_flags updated.  Hence, i_flags no longer inherit the superblock mount
 * flags, so these have to be checked separately. -- rmk@arm.uk.linux.org
 */
#define __IS_FLG(inode,flg) (((inode)->i_sb && (inode)->i_sb->s_flags & (flg)) \
				|| (inode)->i_flags & (flg))

#define IS_RDONLY(inode) (((inode)->i_sb) && ((inode)->i_sb->s_flags & MS_RDONLY))
#define IS_NOSUID(inode)	__IS_FLG(inode, MS_NOSUID)
#define IS_NODEV(inode)		__IS_FLG(inode, MS_NODEV)
#define IS_NOEXEC(inode)	__IS_FLG(inode, MS_NOEXEC)
#define IS_SYNC(inode)		__IS_FLG(inode, MS_SYNCHRONOUS)
#define IS_MANDLOCK(inode)	__IS_FLG(inode, MS_MANDLOCK)

#define IS_QUOTAINIT(inode)	((inode)->i_flags & S_QUOTA)
#define IS_APPEND(inode)	((inode)->i_flags & S_APPEND)
#define IS_IMMUTABLE(inode)	((inode)->i_flags & S_IMMUTABLE)
#define IS_NOATIME(inode)	__IS_FLG(inode, MS_NOATIME)
#define IS_NODIRATIME(inode)	__IS_FLG(inode, MS_NODIRATIME)

/* the read-only stuff doesn't really belong here, but any other place is
   probably as bad and I don't want to create yet another include file. */

#define BLKROSET   _IO(0x12,93)	/* set device read-only (0 = read-write) */
#define BLKROGET   _IO(0x12,94)	/* get read-only status (0 = read_write) */
#define BLKRRPART  _IO(0x12,95)	/* re-read partition table */
#define BLKGETSIZE _IO(0x12,96)	/* return device size */
#define BLKFLSBUF  _IO(0x12,97)	/* flush buffer cache */
#define BLKRASET   _IO(0x12,98)	/* Set read ahead for block device */
#define BLKRAGET   _IO(0x12,99)	/* get current read ahead setting */
#define BLKFRASET  _IO(0x12,100)/* set filesystem (mm/filemap.c) read-ahead */
#define BLKFRAGET  _IO(0x12,101)/* get filesystem (mm/filemap.c) read-ahead */
#define BLKSECTSET _IO(0x12,102)/* set max sectors per request (ll_rw_blk.c) */
#define BLKSECTGET _IO(0x12,103)/* get max sectors per request (ll_rw_blk.c) */
#define BLKSSZGET  _IO(0x12,104) /* get block device sector size */

#define BMAP_IOCTL 1		/* obsolete - kept for compatibility */
#define FIBMAP	   _IO(0x00,1)	/* bmap access */
#define FIGETBSZ   _IO(0x00,2)	/* get the block size used for bmap */

#ifdef __KERNEL__

#include <asm/semaphore.h>
#include <asm/byteorder.h>
#include <asm/bitops.h>

extern void update_atime (struct inode *inode);
#define UPDATE_ATIME(inode) update_atime (inode)

extern void buffer_init(unsigned long);
extern void inode_init(void);
extern void file_table_init(void);
extern void dcache_init(void);

typedef char buffer_block[BLOCK_SIZE];

/* bh state bits */
#define BH_Uptodate	0	/* 1 if the buffer contains valid data */
#define BH_Dirty	1	/* 1 if the buffer is dirty */
#define BH_Lock		2	/* 1 if the buffer is locked */
#define BH_Req		3	/* 0 if the buffer has been invalidated */
#define BH_Protected	6	/* 1 if the buffer is protected */

/*
 * Try to keep the most commonly used fields in single cache lines (16
 * bytes) to improve performance.  This ordering should be
 * particularly beneficial on 32-bit processors.
 * 
 * We use the first 16 bytes for the data which is used in searches
 * over the block hash lists (ie. getblk(), find_buffer() and
 * friends).
 * 
 * The second 16 bytes we use for lru buffer scans, as used by
 * sync_buffers() and refill_freelist().  -- sct
 */
struct buffer_head {
	/* First cache line: */
	struct buffer_head * b_next;	/* Hash queue list */
	unsigned long b_blocknr;	/* block number */
	unsigned long b_size;		/* block size */
	kdev_t b_dev;			/* device (B_FREE = free) */
	kdev_t b_rdev;			/* Real device */
	unsigned long b_rsector;	/* Real buffer location on disk */
	struct buffer_head * b_this_page;	/* circular list of buffers in one page */
	unsigned long b_state;		/* buffer state bitmap (see above) */
	struct buffer_head * b_next_free;
	unsigned int b_count;		/* users using this block */

	/* Non-performance-critical data follows. */
	char * b_data;			/* pointer to data block (1024 bytes) */
	unsigned int b_list;		/* List that this buffer appears */
	unsigned long b_flushtime;      /* Time when this (dirty) buffer
					 * should be written */
	struct wait_queue * b_wait;
	struct buffer_head ** b_pprev;		/* doubly linked list of hash-queue */
	struct buffer_head * b_prev_free;	/* doubly linked list of buffers */
	struct buffer_head * b_reqnext;		/* request queue */

	/*
	 * I/O completion
	 */
	void (*b_end_io)(struct buffer_head *bh, int uptodate);
	void *b_dev_id;
};

typedef void (bh_end_io_t)(struct buffer_head *bh, int uptodate);
void init_buffer(struct buffer_head *bh, kdev_t dev, int block,
		 bh_end_io_t *handler, void *dev_id);

static inline int buffer_uptodate(struct buffer_head * bh)
{
	return test_bit(BH_Uptodate, &bh->b_state);
}	

static inline int buffer_dirty(struct buffer_head * bh)
{
	return test_bit(BH_Dirty, &bh->b_state);
}

static inline int buffer_locked(struct buffer_head * bh)
{
	return test_bit(BH_Lock, &bh->b_state);
}

static inline int buffer_req(struct buffer_head * bh)
{
	return test_bit(BH_Req, &bh->b_state);
}

static inline int buffer_protected(struct buffer_head * bh)
{
	return test_bit(BH_Protected, &bh->b_state);
}

#define buffer_page(bh)		(mem_map + MAP_NR((bh)->b_data))
#define touch_buffer(bh)	set_bit(PG_referenced, &buffer_page(bh)->flags)

#include <linux/pipe_fs_i.h>
#include <linux/minix_fs_i.h>
#include <linux/ext2_fs_i.h>
#include <linux/hpfs_fs_i.h>
#include <linux/ntfs_fs_i.h>
#include <linux/msdos_fs_i.h>
#include <linux/umsdos_fs_i.h>
#include <linux/iso_fs_i.h>
#include <linux/nfs_fs_i.h>
#include <linux/sysv_fs_i.h>
#include <linux/affs_fs_i.h>
#include <linux/ufs_fs_i.h>
#include <linux/efs_fs_i.h>
#include <linux/coda_fs_i.h>
#include <linux/romfs_fs_i.h>
#include <linux/smb_fs_i.h>
#include <linux/hfs_fs_i.h>
#include <linux/adfs_fs_i.h>
#include <linux/qnx4_fs_i.h>

/*
 * Attribute flags.  These should be or-ed together to figure out what
 * has been changed!
 */
#define ATTR_MODE	1
#define ATTR_UID	2
#define ATTR_GID	4
#define ATTR_SIZE	8
#define ATTR_ATIME	16
#define ATTR_MTIME	32
#define ATTR_CTIME	64
#define ATTR_ATIME_SET	128
#define ATTR_MTIME_SET	256
#define ATTR_FORCE	512	/* Not a change, but a change it */
#define ATTR_ATTR_FLAG	1024

/*
 * This is the Inode Attributes structure, used for notify_change().  It
 * uses the above definitions as flags, to know which values have changed.
 * Also, in this manner, a Filesystem can look at only the values it cares
 * about.  Basically, these are the attributes that the VFS layer can
 * request to change from the FS layer.
 *
 * Derek Atkins <warlord@MIT.EDU> 94-10-20
 */
struct iattr {
	unsigned int	ia_valid;
	umode_t		ia_mode;
	uid_t		ia_uid;
	gid_t		ia_gid;
	off_t		ia_size;
	time_t		ia_atime;
	time_t		ia_mtime;
	time_t		ia_ctime;
	unsigned int	ia_attr_flags;
};

/*
 * This is the inode attributes flag definitions
 */
#define ATTR_FLAG_SYNCRONOUS	1 	/* Syncronous write */
#define ATTR_FLAG_NOATIME	2 	/* Don't update atime */
#define ATTR_FLAG_APPEND	4 	/* Append-only file */
#define ATTR_FLAG_IMMUTABLE	8 	/* Immutable file */
#define ATTR_FLAG_NODIRATIME	16 	/* Don't update atime for directory */

/*
 * Includes for diskquotas and mount structures.
 */
#include <linux/quota.h>
#include <linux/mount.h>

struct inode {
	struct list_head	i_hash;
	struct list_head	i_list;
	struct list_head	i_dentry;

	unsigned long		i_ino;
	unsigned int		i_count;
	kdev_t			i_dev;
	umode_t			i_mode;
	nlink_t			i_nlink;
	uid_t			i_uid;
	gid_t			i_gid;
	kdev_t			i_rdev;
	off_t			i_size;
	time_t			i_atime;
	time_t			i_mtime;
	time_t			i_ctime;
	unsigned long		i_blksize;
	unsigned long		i_blocks;
	unsigned long		i_version;
	unsigned long		i_nrpages;
	struct semaphore	i_sem;
	struct semaphore	i_atomic_write;
	struct inode_operations	*i_op;
	struct super_block	*i_sb;
	struct wait_queue	*i_wait;
	struct file_lock	*i_flock;
	struct vm_area_struct	*i_mmap;
	struct page		*i_pages;
	struct dquot		*i_dquot[MAXQUOTAS];

	unsigned long		i_state;

	unsigned int		i_flags;
	unsigned char		i_pipe;
	unsigned char		i_sock;

	int			i_writecount;
	unsigned int		i_attr_flags;
	__u32			i_generation;
	union {
		struct pipe_inode_info		pipe_i;
		struct minix_inode_info		minix_i;
		struct ext2_inode_info		ext2_i;
		struct hpfs_inode_info		hpfs_i;
		struct ntfs_inode_info          ntfs_i;
		struct msdos_inode_info		msdos_i;
		struct umsdos_inode_info	umsdos_i;
		struct iso_inode_info		isofs_i;
		struct nfs_inode_info		nfs_i;
		struct sysv_inode_info		sysv_i;
		struct affs_inode_info		affs_i;
		struct ufs_inode_info		ufs_i;
		struct efs_inode_info		efs_i;
		struct romfs_inode_info		romfs_i;
		struct coda_inode_info		coda_i;
		struct smb_inode_info		smbfs_i;
		struct hfs_inode_info		hfs_i;
		struct adfs_inode_info		adfs_i;
		struct qnx4_inode_info		qnx4_i;
		struct socket			socket_i;
		void				*generic_ip;
	} u;
};

/* Inode state bits.. */
#define I_DIRTY		1
#define I_LOCK		2
#define I_FREEING	4

extern void __mark_inode_dirty(struct inode *);
static inline void mark_inode_dirty(struct inode *inode)
{
	if (!(inode->i_state & I_DIRTY))
		__mark_inode_dirty(inode);
}

struct fown_struct {
	int pid;		/* pid or -pgrp where SIGIO should be sent */
	uid_t uid, euid;	/* uid/euid of process setting the owner */
	int signum;		/* posix.1b rt signal to be delivered on IO */
};

struct file {
	struct file		*f_next, **f_pprev;
	struct dentry		*f_dentry;
	struct file_operations	*f_op;
	mode_t			f_mode;
	loff_t			f_pos;
	unsigned int 		f_count, f_flags;
	unsigned long 		f_reada, f_ramax, f_raend, f_ralen, f_rawin;
	struct fown_struct	f_owner;
	unsigned int		f_uid, f_gid;
	int			f_error;

	unsigned long		f_version;

	/* needed for tty driver, and maybe others */
	void			*private_data;
};

extern int init_private_file(struct file *, struct dentry *, int);

#define FL_POSIX	1
#define FL_FLOCK	2
#define FL_BROKEN	4	/* broken flock() emulation */
#define FL_ACCESS	8	/* for processes suspended by mandatory locking */
#define FL_LOCKD	16	/* lock held by rpc.lockd */

/*
 * The POSIX file lock owner is determined by
 * the "struct files_struct" in the thread group
 * (or NULL for no owner - BSD locks).
 *
 * Lockd stuffs a "host" pointer into this.
 */
typedef struct files_struct *fl_owner_t;

struct file_lock {
	struct file_lock *fl_next;	/* singly linked list for this inode  */
	struct file_lock *fl_nextlink;	/* doubly linked list of all locks */
	struct file_lock *fl_prevlink;	/* used to simplify lock removal */
	struct file_lock *fl_nextblock; /* circular list of blocked processes */
	struct file_lock *fl_prevblock;
	fl_owner_t fl_owner;
	unsigned int fl_pid;
	struct wait_queue *fl_wait;
	struct file *fl_file;
	unsigned char fl_flags;
	unsigned char fl_type;
	off_t fl_start;
	off_t fl_end;

	void (*fl_notify)(struct file_lock *);	/* unblock callback */

	union {
		struct nfs_lock_info	nfs_fl;
	} fl_u;
};

extern struct file_lock			*file_lock_table;

#include <linux/fcntl.h>

extern int fcntl_getlk(unsigned int fd, struct flock *l);
extern int fcntl_setlk(unsigned int fd, unsigned int cmd, struct flock *l);

/* fs/locks.c */
extern void locks_remove_posix(struct file *, fl_owner_t id);
extern void locks_remove_flock(struct file *);
extern struct file_lock *posix_test_lock(struct file *, struct file_lock *);
extern int posix_lock_file(struct file *, struct file_lock *, unsigned int);
extern void posix_block_lock(struct file_lock *, struct file_lock *);
extern void posix_unblock_lock(struct file_lock *);

struct fasync_struct {
	int    magic;
	int    fa_fd;
	struct fasync_struct	*fa_next; /* singly linked list */
	struct file 		*fa_file;
};

#define FASYNC_MAGIC 0x4601

extern int fasync_helper(int, struct file *, int, struct fasync_struct **);

#include <linux/minix_fs_sb.h>
#include <linux/ext2_fs_sb.h>
#include <linux/hpfs_fs_sb.h>
#include <linux/ntfs_fs_sb.h>
#include <linux/msdos_fs_sb.h>
#include <linux/iso_fs_sb.h>
#include <linux/nfs_fs_sb.h>
#include <linux/sysv_fs_sb.h>
#include <linux/affs_fs_sb.h>
#include <linux/ufs_fs_sb.h>
#include <linux/efs_fs_sb.h>
#include <linux/romfs_fs_sb.h>
#include <linux/smb_fs_sb.h>
#include <linux/hfs_fs_sb.h>
#include <linux/adfs_fs_sb.h>
#include <linux/qnx4_fs_sb.h>

extern struct list_head super_blocks;

#define sb_entry(list)	list_entry((list), struct super_block, s_list)
struct super_block {
	struct list_head	s_list;		/* Keep this first */
	kdev_t			s_dev;
	unsigned long		s_blocksize;
	unsigned char		s_blocksize_bits;
	unsigned char		s_lock;
	unsigned char		s_rd_only;
	unsigned char		s_dirt;
	struct file_system_type	*s_type;
	struct super_operations	*s_op;
	struct dquot_operations	*dq_op;
	unsigned long		s_flags;
	unsigned long		s_magic;
	unsigned long		s_time;
	struct dentry		*s_root;
	struct wait_queue	*s_wait;

	struct inode		*s_ibasket;
	short int		s_ibasket_count;
	short int		s_ibasket_max;
	struct list_head	s_dirty;	/* dirty inodes */

	union {
		struct minix_sb_info	minix_sb;
		struct ext2_sb_info	ext2_sb;
		struct hpfs_sb_info	hpfs_sb;
		struct ntfs_sb_info     ntfs_sb;
		struct msdos_sb_info	msdos_sb;
		struct isofs_sb_info	isofs_sb;
		struct nfs_sb_info	nfs_sb;
		struct sysv_sb_info	sysv_sb;
		struct affs_sb_info	affs_sb;
		struct ufs_sb_info	ufs_sb;
		struct efs_sb_info	efs_sb;
		struct romfs_sb_info	romfs_sb;
		struct smb_sb_info	smbfs_sb;
		struct hfs_sb_info	hfs_sb;
		struct adfs_sb_info	adfs_sb;
		struct qnx4_sb_info	qnx4_sb;	   
		void			*generic_sbp;
	} u;
	/*
	 * The next field is for VFS *only*. No filesystems have any business
	 * even looking at it. You had been warned.
	 */
	struct semaphore s_vfs_rename_sem;	/* Kludge */
};

/*
 * VFS helper functions..
 */
extern int vfs_rmdir(struct inode *, struct dentry *);
extern int vfs_unlink(struct inode *, struct dentry *);
extern int vfs_rename(struct inode *, struct dentry *, struct inode *, struct dentry *);

/*
 * This is the "filldir" function type, used by readdir() to let
 * the kernel specify what kind of dirent layout it wants to have.
 * This allows the kernel to read directories into kernel space or
 * to have different dirent layouts depending on the binary type.
 */
typedef int (*filldir_t)(void *, const char *, int, off_t, ino_t);
	
struct file_operations {
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char *, size_t, loff_t *);
	int (*readdir) (struct file *, void *, filldir_t);
	unsigned int (*poll) (struct file *, struct poll_table_struct *);
	int (*ioctl) (struct inode *, struct file *, unsigned int, unsigned long);
	int (*mmap) (struct file *, struct vm_area_struct *);
	int (*open) (struct inode *, struct file *);
	int (*flush) (struct file *);
	int (*release) (struct inode *, struct file *);
	int (*fsync) (struct file *, struct dentry *);
	int (*fasync) (int, struct file *, int);
	int (*check_media_change) (kdev_t dev);
	int (*revalidate) (kdev_t dev);
	int (*lock) (struct file *, int, struct file_lock *);
};

struct inode_operations {
	struct file_operations * default_file_ops;
	int (*create) (struct inode *,struct dentry *,int);
	struct dentry * (*lookup) (struct inode *,struct dentry *);
	int (*link) (struct dentry *,struct inode *,struct dentry *);
	int (*unlink) (struct inode *,struct dentry *);
	int (*symlink) (struct inode *,struct dentry *,const char *);
	int (*mkdir) (struct inode *,struct dentry *,int);
	int (*rmdir) (struct inode *,struct dentry *);
	int (*mknod) (struct inode *,struct dentry *,int,int);
	int (*rename) (struct inode *, struct dentry *,
			struct inode *, struct dentry *);
	int (*readlink) (struct dentry *, char *,int);
	struct dentry * (*follow_link) (struct dentry *, struct dentry *, unsigned int);
	int (*readpage) (struct file *, struct page *);
	int (*writepage) (struct file *, struct page *);
	int (*bmap) (struct inode *,int);
	void (*truncate) (struct inode *);
	int (*permission) (struct inode *, int);
	int (*smap) (struct inode *,int);
	int (*updatepage) (struct file *, struct page *, unsigned long, unsigned int, int);
	int (*revalidate) (struct dentry *);
};

struct super_operations {
	void (*read_inode) (struct inode *);
	void (*write_inode) (struct inode *);
	void (*put_inode) (struct inode *);
	void (*delete_inode) (struct inode *);
	int (*notify_change) (struct dentry *, struct iattr *);
	void (*put_super) (struct super_block *);
	void (*write_super) (struct super_block *);
	int (*statfs) (struct super_block *, struct statfs *, int);
	int (*remount_fs) (struct super_block *, int *, char *);
	void (*clear_inode) (struct inode *);
	void (*umount_begin) (struct super_block *);
};

struct dquot_operations {
	void (*initialize) (struct inode *, short);
	void (*drop) (struct inode *);
	int (*alloc_block) (const struct inode *, unsigned long, uid_t, char);
	int (*alloc_inode) (const struct inode *, unsigned long, uid_t);
	void (*free_block) (const struct inode *, unsigned long);
	void (*free_inode) (const struct inode *, unsigned long);
	int (*transfer) (struct dentry *, struct iattr *, uid_t);
};

struct file_system_type {
	const char *name;
	int fs_flags;
	struct super_block *(*read_super) (struct super_block *, void *, int);
	struct file_system_type * next;
};

extern int register_filesystem(struct file_system_type *);
extern int unregister_filesystem(struct file_system_type *);

/* Return value for VFS lock functions - tells locks.c to lock conventionally
 * REALLY kosha for root NFS and nfs_lock
 */ 
#define LOCK_USE_CLNT 1

#define FLOCK_VERIFY_READ  1
#define FLOCK_VERIFY_WRITE 2

extern int locks_mandatory_locked(struct inode *inode);
extern int locks_mandatory_area(int read_write, struct inode *inode,
				struct file *filp, loff_t offset,
				size_t count);

extern inline int locks_verify_locked(struct inode *inode)
{
	/* Candidates for mandatory locking have the setgid bit set
	 * but no group execute bit -  an otherwise meaningless combination.
	 */
	if (IS_MANDLOCK(inode) &&
	    (inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return (locks_mandatory_locked(inode));
	return (0);
}

extern inline int locks_verify_area(int read_write, struct inode *inode,
				    struct file *filp, loff_t offset,
				    size_t count)
{
	/* Candidates for mandatory locking have the setgid bit set
	 * but no group execute bit -  an otherwise meaningless combination.
	 */
	if (IS_MANDLOCK(inode) &&
	    (inode->i_mode & (S_ISGID | S_IXGRP)) == S_ISGID)
		return (locks_mandatory_area(read_write, inode, filp, offset,
					     count));
	return (0);
}


/* fs/open.c */

asmlinkage int sys_open(const char *, int, int);
asmlinkage int sys_close(unsigned int);		/* yes, it's really unsigned */
extern int do_truncate(struct dentry *, unsigned long);
extern int get_unused_fd(void);
extern void put_unused_fd(unsigned int);

extern struct file *filp_open(const char *, int, int);
extern int filp_close(struct file *, fl_owner_t id);

extern char * getname(const char * filename);
#define __getname()	((char *) __get_free_page(GFP_KERNEL))
#define putname(name)	free_page((unsigned long)(name))

extern void kill_fasync(struct fasync_struct *fa, int sig);
extern int register_blkdev(unsigned int, const char *, struct file_operations *);
extern int unregister_blkdev(unsigned int major, const char * name);
extern int blkdev_open(struct inode * inode, struct file * filp);
extern int blkdev_release (struct inode * inode);
extern struct file_operations def_blk_fops;
extern struct inode_operations blkdev_inode_operations;

/* fs/devices.c */
extern int register_chrdev(unsigned int, const char *, struct file_operations *);
extern int unregister_chrdev(unsigned int major, const char * name);
extern int chrdev_open(struct inode * inode, struct file * filp);
extern struct file_operations def_chr_fops;
extern struct inode_operations chrdev_inode_operations;
extern char * bdevname(kdev_t dev);
extern char * cdevname(kdev_t dev);
extern char * kdevname(kdev_t dev);


extern void init_fifo(struct inode * inode);
extern struct inode_operations fifo_inode_operations;

/* Invalid inode operations -- fs/bad_inode.c */
extern void make_bad_inode(struct inode * inode);
extern int is_bad_inode(struct inode * inode);

extern struct file_operations connecting_fifo_fops;
extern struct file_operations read_fifo_fops;
extern struct file_operations write_fifo_fops;
extern struct file_operations rdwr_fifo_fops;
extern struct file_operations read_pipe_fops;
extern struct file_operations write_pipe_fops;
extern struct file_operations rdwr_pipe_fops;

extern struct file_system_type *get_fs_type(const char *name);

extern int fs_may_remount_ro(struct super_block *);
extern int fs_may_mount(kdev_t dev);

extern struct file *inuse_filps;

extern void refile_buffer(struct buffer_head * buf);
extern void set_writetime(struct buffer_head * buf, int flag);
extern int try_to_free_buffers(struct page *);

extern int nr_buffers;
extern long buffermem;
extern int nr_buffer_heads;

#define BUF_CLEAN	0
#define BUF_LOCKED	1	/* Buffers scheduled for write */
#define BUF_DIRTY	2	/* Dirty buffers, not yet scheduled for write */
#define NR_LIST		3

void mark_buffer_uptodate(struct buffer_head * bh, int on);

extern inline void mark_buffer_clean(struct buffer_head * bh)
{
	if (test_and_clear_bit(BH_Dirty, &bh->b_state)) {
		if (bh->b_list == BUF_DIRTY)
			refile_buffer(bh);
	}
}

extern inline void mark_buffer_dirty(struct buffer_head * bh, int flag)
{
	if (!test_and_set_bit(BH_Dirty, &bh->b_state)) {
		set_writetime(bh, flag);
		if (bh->b_list != BUF_DIRTY)
			refile_buffer(bh);
	}
}

extern int check_disk_change(kdev_t dev);
extern int invalidate_inodes(struct super_block * sb);
extern void invalidate_inode_pages(struct inode *);
#define invalidate_buffers(dev)	__invalidate_buffers((dev), 0)
#define destroy_buffers(dev)	__invalidate_buffers((dev), 1)
extern void __invalidate_buffers(kdev_t dev, int);
extern int floppy_is_wp(int minor);
extern void sync_inodes(kdev_t dev);
extern void write_inode_now(struct inode *inode);
extern void sync_dev(kdev_t dev);
extern int fsync_dev(kdev_t dev);
extern void sync_supers(kdev_t dev);
extern int bmap(struct inode * inode,int block);
extern int notify_change(struct dentry *, struct iattr *);
extern int permission(struct inode * inode,int mask);
extern int get_write_access(struct inode *inode);
extern void put_write_access(struct inode *inode);
extern struct dentry * open_namei(const char * pathname, int flag, int mode);
extern struct dentry * do_mknod(const char * filename, int mode, dev_t dev);
extern int do_pipe(int *);

/* fs/dcache.c -- generic fs support functions */
extern int is_subdir(struct dentry *, struct dentry *);
extern ino_t find_inode_number(struct dentry *, struct qstr *);

/*
 * Kernel pointers have redundant information, so we can use a
 * scheme where we can return either an error code or a dentry
 * pointer with the same return value.
 *
 * This should be a per-architecture thing, to allow different
 * error and pointer decisions.
 */
#define ERR_PTR(err)	((void *)((long)(err)))
#define PTR_ERR(ptr)	((long)(ptr))
#define IS_ERR(ptr)	((unsigned long)(ptr) > (unsigned long)(-1000))

/*
 * The bitmask for a lookup event:
 *  - follow links at the end
 *  - require a directory
 *  - ending slashes ok even for nonexistent files
 *  - internal "there are more path compnents" flag
 */
#define LOOKUP_FOLLOW		(1)
#define LOOKUP_DIRECTORY	(2)
#define LOOKUP_SLASHOK		(4)
#define LOOKUP_CONTINUE		(8)

extern struct dentry * lookup_dentry(const char *, struct dentry *, unsigned int);
extern struct dentry * __namei(const char *, unsigned int);

#define namei(pathname)		__namei(pathname, 1)
#define lnamei(pathname)	__namei(pathname, 0)

extern void iput(struct inode *);
extern struct inode * igrab(struct inode *inode);
extern ino_t iunique(struct super_block *, ino_t);
extern struct inode * iget(struct super_block *, unsigned long);
extern struct inode * iget_in_use (struct super_block *, unsigned long);
extern void clear_inode(struct inode *);
extern struct inode * get_empty_inode(void);

extern void insert_inode_hash(struct inode *);
extern void remove_inode_hash(struct inode *);
extern struct file * get_empty_filp(void);
extern struct buffer_head * get_hash_table(kdev_t, int, int);
extern struct buffer_head * getblk(kdev_t, int, int);
extern struct buffer_head * find_buffer(kdev_t dev, int block, int size);
extern void ll_rw_block(int, int, struct buffer_head * bh[]);
extern int is_read_only(kdev_t);
extern void __brelse(struct buffer_head *);
extern inline void brelse(struct buffer_head *buf)
{
	if (buf)
		__brelse(buf);
}
extern void __bforget(struct buffer_head *buf);
extern inline void bforget(struct buffer_head *buf)
{
	if (buf)
		__bforget(buf);
}
extern void set_blocksize(kdev_t dev, int size);
extern unsigned int get_hardblocksize(kdev_t dev);
extern struct buffer_head * bread(kdev_t dev, int block, int size);
extern struct buffer_head * breada(kdev_t dev,int block, int size, 
				   unsigned int pos, unsigned int filesize);

extern int brw_page(int, struct page *, kdev_t, int [], int, int);

extern int generic_readpage(struct file *, struct page *);
extern int generic_file_mmap(struct file *, struct vm_area_struct *);
extern ssize_t generic_file_read(struct file *, char *, size_t, loff_t *);
extern ssize_t generic_file_write(struct file *, const char*, size_t, loff_t*);

extern struct super_block *get_super(kdev_t dev);
extern void put_super(kdev_t dev);
unsigned long generate_cluster(kdev_t dev, int b[], int size);
unsigned long generate_cluster_swab32(kdev_t dev, int b[], int size);
extern kdev_t ROOT_DEV;

extern void show_buffers(void);
extern void mount_root(void);

#ifdef CONFIG_BLK_DEV_INITRD
extern kdev_t real_root_dev;
extern int change_root(kdev_t new_root_dev,const char *put_old);
#endif

extern ssize_t char_read(struct file *, char *, size_t, loff_t *);
extern ssize_t block_read(struct file *, char *, size_t, loff_t *);
extern int read_ahead[];

extern ssize_t char_write(struct file *, const char *, size_t, loff_t *);
extern ssize_t block_write(struct file *, const char *, size_t, loff_t *);

extern int block_fsync(struct file *, struct dentry *dir);
extern int file_fsync(struct file *, struct dentry *dir);

extern int inode_change_ok(struct inode *, struct iattr *);
extern void inode_setattr(struct inode *, struct iattr *);

extern __u32 inode_generation_count;

#endif /* __KERNEL__ */

#endif
