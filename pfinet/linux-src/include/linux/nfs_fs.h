/*
 *  linux/include/linux/nfs_fs.h
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  OS-specific nfs filesystem definitions and declarations
 */

#ifndef _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_H

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/in.h>

#include <linux/sunrpc/sched.h>
#include <linux/nfs.h>
#include <linux/nfs_mount.h>

/*
 * Enable debugging support for nfs client.
 * Requires RPC_DEBUG.
 */
#ifdef RPC_DEBUG
# define NFS_DEBUG
#endif

/*
 * NFS_MAX_DIRCACHE controls the number of simultaneously cached
 * directory chunks. Each chunk holds the list of nfs_entry's returned
 * in a single readdir call in a memory region of size PAGE_SIZE.
 *
 * Note that at most server->rsize bytes of the cache memory are used.
 */
#define NFS_MAX_DIRCACHE		16

#define NFS_MAX_FILE_IO_BUFFER_SIZE	16384
#define NFS_DEF_FILE_IO_BUFFER_SIZE	4096

/*
 * The upper limit on timeouts for the exponential backoff algorithm.
 */
#define NFS_MAX_RPC_TIMEOUT		(6*HZ)

/*
 * Size of the lookup cache in units of number of entries cached.
 * It is better not to make this too large although the optimum
 * depends on a usage and environment.
 */
#define NFS_LOOKUP_CACHE_SIZE		64

/*
 * superblock magic number for NFS
 */
#define NFS_SUPER_MAGIC			0x6969

#define NFS_FH(dentry)			((struct nfs_fh *) ((dentry)->d_fsdata))
#define NFS_DSERVER(dentry)		(&(dentry)->d_sb->u.nfs_sb.s_server)
#define NFS_SERVER(inode)		(&(inode)->i_sb->u.nfs_sb.s_server)
#define NFS_CLIENT(inode)		(NFS_SERVER(inode)->client)
#define NFS_ADDR(inode)			(RPC_PEERADDR(NFS_CLIENT(inode)))
#define NFS_CONGESTED(inode)		(RPC_CONGESTED(NFS_CLIENT(inode)))

#define NFS_READTIME(inode)		((inode)->u.nfs_i.read_cache_jiffies)
#define NFS_OLDMTIME(inode)		((inode)->u.nfs_i.read_cache_mtime)
#define NFS_CACHEINV(inode) \
do { \
	NFS_READTIME(inode) = jiffies - 1000000; \
	NFS_OLDMTIME(inode) = 0; \
} while (0)
#define NFS_ATTRTIMEO(inode)		((inode)->u.nfs_i.attrtimeo)
#define NFS_MINATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmin \
			       : NFS_SERVER(inode)->acregmin)
#define NFS_MAXATTRTIMEO(inode) \
	(S_ISDIR(inode->i_mode)? NFS_SERVER(inode)->acdirmax \
			       : NFS_SERVER(inode)->acregmax)

#define NFS_FLAGS(inode)		((inode)->u.nfs_i.flags)
#define NFS_REVALIDATING(inode)		(NFS_FLAGS(inode) & NFS_INO_REVALIDATE)
#define NFS_WRITEBACK(inode)		((inode)->u.nfs_i.writeback)

/*
 * These are the default flags for swap requests
 */
#define NFS_RPC_SWAPFLAGS		(RPC_TASK_SWAPPER|RPC_TASK_ROOTCREDS)

/* Flags in the RPC client structure */
#define NFS_CLNTF_BUFSIZE	0x0001	/* readdir buffer in longwords */

#ifdef __KERNEL__

/*
 * This struct describes a file region to be written.
 * It's kind of a pity we have to keep all these lists ourselves, rather
 * than sticking an extra pointer into struct page.
 */
struct nfs_wreq {
	struct rpc_listitem	wb_list;	/* linked list of req's */
	struct rpc_task		wb_task;	/* RPC task */
	struct file *		wb_file;	/* dentry referenced */
	struct page *		wb_page;	/* page to be written */
	struct wait_queue *	wb_wait;	/* wait for completion */
	unsigned int		wb_offset;	/* offset within page */
	unsigned int		wb_bytes;	/* dirty range */
	unsigned int		wb_count;	/* user count */
	int			wb_status;
	pid_t			wb_pid;		/* owner process */
	unsigned short		wb_flags;	/* status flags */

	struct nfs_writeargs	wb_args;	/* NFS RPC stuff */
	struct nfs_fattr	wb_fattr;	/* file attributes */
};

#define WB_NEXT(req)		((struct nfs_wreq *) ((req)->wb_list.next))

/*
 * Various flags for wb_flags
 */
#define NFS_WRITE_CANCELLED	0x0004	/* has been cancelled */
#define NFS_WRITE_UNCOMMITTED	0x0008	/* written but uncommitted (NFSv3) */
#define NFS_WRITE_INVALIDATE	0x0010	/* invalidate after write */
#define NFS_WRITE_INPROGRESS	0x0100	/* RPC call in progress */
#define NFS_WRITE_COMPLETE	0x0200	/* RPC call completed */

#define WB_CANCELLED(req)	((req)->wb_flags & NFS_WRITE_CANCELLED)
#define WB_UNCOMMITTED(req)	((req)->wb_flags & NFS_WRITE_UNCOMMITTED)
#define WB_INVALIDATE(req)	((req)->wb_flags & NFS_WRITE_INVALIDATE)
#define WB_INPROGRESS(req)	((req)->wb_flags & NFS_WRITE_INPROGRESS)
#define WB_COMPLETE(req)	((req)->wb_flags & NFS_WRITE_COMPLETE)

/*
 * linux/fs/nfs/proc.c
 */
extern int nfs_proc_getattr(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr);
extern int nfs_proc_setattr(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_sattr *sattr, struct nfs_fattr *fattr);
extern int nfs_proc_lookup(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct nfs_fh *fhandle,
			struct nfs_fattr *fattr);
extern int nfs_proc_readlink(struct nfs_server *server, struct nfs_fh *fhandle,
			void **p0, char **string, unsigned int *len,
			unsigned int maxlen);
extern int nfs_proc_read(struct nfs_server *server, struct nfs_fh *fhandle,
			int swap, unsigned long offset, unsigned int count,
			void *buffer, struct nfs_fattr *fattr);
extern int nfs_proc_write(struct nfs_server *server, struct nfs_fh *fhandle,
			int swap, unsigned long offset, unsigned int count,
			const void *buffer, struct nfs_fattr *fattr);
extern int nfs_proc_create(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct nfs_sattr *sattr,
			struct nfs_fh *fhandle, struct nfs_fattr *fattr);
extern int nfs_proc_remove(struct nfs_server *server, struct nfs_fh *dir,
			const char *name);
extern int nfs_proc_rename(struct nfs_server *server,
			struct nfs_fh *old_dir, const char *old_name,
			struct nfs_fh *new_dir, const char *new_name);
extern int nfs_proc_link(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fh *dir, const char *name);
extern int nfs_proc_symlink(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, const char *path,
			struct nfs_sattr *sattr);
extern int nfs_proc_mkdir(struct nfs_server *server, struct nfs_fh *dir,
			const char *name, struct nfs_sattr *sattr,
			struct nfs_fh *fhandle, struct nfs_fattr *fattr);
extern int nfs_proc_rmdir(struct nfs_server *server, struct nfs_fh *dir,
			const char *name);
extern int nfs_proc_readdir(struct nfs_server *server, struct nfs_fh *fhandle,
			u32 cookie, unsigned int size, __u32 *entry);
extern int nfs_proc_statfs(struct nfs_server *server, struct nfs_fh *fhandle,
			struct nfs_fsinfo *res);


/*
 * linux/fs/nfs/inode.c
 */
extern struct super_block *nfs_read_super(struct super_block *, void *, int);
extern int init_nfs_fs(void);
extern struct inode *nfs_fhget(struct dentry *, struct nfs_fh *,
				struct nfs_fattr *);
extern int nfs_refresh_inode(struct inode *, struct nfs_fattr *);
extern int nfs_revalidate(struct dentry *);
extern int nfs_open(struct inode *, struct file *);
extern int nfs_release(struct inode *, struct file *);
extern int _nfs_revalidate_inode(struct nfs_server *, struct dentry *);

/*
 * linux/fs/nfs/file.c
 */
extern struct inode_operations nfs_file_inode_operations;

/*
 * linux/fs/nfs/dir.c
 */
extern struct inode_operations nfs_dir_inode_operations;
extern struct dentry_operations nfs_dentry_operations;
extern void nfs_free_dircache(void);
extern void nfs_invalidate_dircache(struct inode *);
extern void nfs_invalidate_dircache_sb(struct super_block *);

/*
 * linux/fs/nfs/symlink.c
 */
extern struct inode_operations nfs_symlink_inode_operations;

/*
 * linux/fs/nfs/locks.c
 */
extern int nfs_lock(struct file *, int, struct file_lock *);

/*
 * linux/fs/nfs/write.c
 */
extern int  nfs_writepage(struct file *, struct page *);
extern int  nfs_check_failed_request(struct inode *);

/*
 * Try to write back everything synchronously (but check the
 * return value!)
 */
extern int  nfs_wb_all(struct inode *);
extern int  nfs_wb_page(struct inode *, struct page *);
extern int  nfs_wb_file(struct inode *, struct file *);

/*
 * Invalidate write-backs, possibly trying to write them
 * back first..
 */
extern void nfs_inval(struct inode *);
extern int  nfs_updatepage(struct file *, struct page *, unsigned long, unsigned int, int);

/*
 * linux/fs/nfs/read.c
 */
extern int  nfs_readpage(struct file *, struct page *);

/*
 * linux/fs/mount_clnt.c
 * (Used only by nfsroot module)
 */
extern int  nfs_mount(struct sockaddr_in *, char *, struct nfs_fh *);

/*
 * inline functions
 */
static inline int
nfs_revalidate_inode(struct nfs_server *server, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	if (jiffies - NFS_READTIME(inode) < NFS_ATTRTIMEO(inode))
		return 0;
	return _nfs_revalidate_inode(server, dentry);
}

/* NFS root */

extern int nfs_root_mount(struct super_block *sb);

#endif /* __KERNEL__ */

/*
 * NFS debug flags
 */
#define NFSDBG_VFS		0x0001
#define NFSDBG_DIRCACHE		0x0002
#define NFSDBG_LOOKUPCACHE	0x0004
#define NFSDBG_PAGECACHE	0x0008
#define NFSDBG_PROC		0x0010
#define NFSDBG_XDR		0x0020
#define NFSDBG_FILE		0x0040
#define NFSDBG_ROOT		0x0080
#define NFSDBG_ALL		0xFFFF

#ifdef __KERNEL__
# undef ifdebug
# ifdef NFS_DEBUG
#  define ifdebug(fac)		if (nfs_debug & NFSDBG_##fac)
# else
#  define ifdebug(fac)		if (0)
# endif
#endif /* __KERNEL */

#endif
