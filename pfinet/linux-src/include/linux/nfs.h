/*
 * NFS protocol definitions
 */
#ifndef _LINUX_NFS_H
#define _LINUX_NFS_H

#include <linux/sunrpc/msg_prot.h>

#define NFS_PORT	2049
#define NFS_MAXDATA	8192
#define NFS_MAXPATHLEN	1024
#define NFS_MAXNAMLEN	255
#define NFS_MAXGROUPS	16
#define NFS_FHSIZE	32
#define NFS_COOKIESIZE	4
#define NFS_FIFO_DEV	(-1)
#define NFSMODE_FMT	0170000
#define NFSMODE_DIR	0040000
#define NFSMODE_CHR	0020000
#define NFSMODE_BLK	0060000
#define NFSMODE_REG	0100000
#define NFSMODE_LNK	0120000
#define NFSMODE_SOCK	0140000
#define NFSMODE_FIFO	0010000

	
enum nfs_stat {
	NFS_OK = 0,
	NFSERR_PERM = 1,
	NFSERR_NOENT = 2,
	NFSERR_IO = 5,
	NFSERR_NXIO = 6,
	NFSERR_EAGAIN = 11,
	NFSERR_ACCES = 13,
	NFSERR_EXIST = 17,
	NFSERR_XDEV = 18,
	NFSERR_NODEV = 19,
	NFSERR_NOTDIR = 20,
	NFSERR_ISDIR = 21,
	NFSERR_INVAL = 22,	/* that Sun forgot */
	NFSERR_FBIG = 27,
	NFSERR_NOSPC = 28,
	NFSERR_ROFS = 30,
	NFSERR_OPNOTSUPP = 45,
	NFSERR_NAMETOOLONG = 63,
	NFSERR_NOTEMPTY = 66,
	NFSERR_DQUOT = 69,
	NFSERR_STALE = 70,
	NFSERR_WFLUSH = 99
};

enum nfs_ftype {
	NFNON = 0,
	NFREG = 1,
	NFDIR = 2,
	NFBLK = 3,
	NFCHR = 4,
	NFLNK = 5,
	NFSOCK = 6,
	NFBAD = 7,
	NFFIFO = 8
};

struct nfs_fh {
	char			data[NFS_FHSIZE];
};

#define NFS_PROGRAM		100003
#define NFS_VERSION		2
#define NFSPROC_NULL		0
#define NFSPROC_GETATTR		1
#define NFSPROC_SETATTR		2
#define NFSPROC_ROOT		3
#define NFSPROC_LOOKUP		4
#define NFSPROC_READLINK	5
#define NFSPROC_READ		6
#define NFSPROC_WRITECACHE	7
#define NFSPROC_WRITE		8
#define NFSPROC_CREATE		9
#define NFSPROC_REMOVE		10
#define NFSPROC_RENAME		11
#define NFSPROC_LINK		12
#define NFSPROC_SYMLINK		13
#define NFSPROC_MKDIR		14
#define NFSPROC_RMDIR		15
#define NFSPROC_READDIR		16
#define NFSPROC_STATFS		17

/* Mount support for NFSroot */
#ifdef __KERNEL__
#define NFS_MNT_PROGRAM		100005
#define NFS_MNT_VERSION		1
#define NFS_MNT_PORT		627
#define NFS_MNTPROC_MNT		1
#define NFS_MNTPROC_UMNT	3
#endif

#if defined(__KERNEL__) || defined(NFS_NEED_KERNEL_TYPES)

extern struct rpc_program	nfs_program;
extern struct rpc_stat		nfs_rpcstat;

struct nfs_time {
	__u32			seconds;
	__u32			useconds;
};

struct nfs_fattr {
	enum nfs_ftype		type;
	__u32			mode;
	__u32			nlink;
	__u32			uid;
	__u32			gid;
	__u32			size;
	__u32			blocksize;
	__u32			rdev;
	__u32			blocks;
	__u32			fsid;
	__u32			fileid;
	struct nfs_time		atime;
	struct nfs_time		mtime;
	struct nfs_time		ctime;
};

struct nfs_sattr {
	__u32			mode;
	__u32			uid;
	__u32			gid;
	__u32			size;
	struct nfs_time		atime;
	struct nfs_time		mtime;
};

struct nfs_fsinfo {
	__u32			tsize;
	__u32			bsize;
	__u32			blocks;
	__u32			bfree;
	__u32			bavail;
};

struct nfs_writeargs {
	struct nfs_fh *		fh;
	__u32			offset;
	__u32			count;
	const void *		buffer;
};

#ifdef NFS_NEED_XDR_TYPES

struct nfs_sattrargs {
	struct nfs_fh *		fh;
	struct nfs_sattr *	sattr;
};

struct nfs_diropargs {
	struct nfs_fh *		fh;
	const char *		name;
};

struct nfs_readargs {
	struct nfs_fh *		fh;
	__u32			offset;
	__u32			count;
	void *			buffer;
};

struct nfs_createargs {
	struct nfs_fh *		fh;
	const char *		name;
	struct nfs_sattr *	sattr;
};

struct nfs_renameargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	struct nfs_fh *		tofh;
	const char *		toname;
};

struct nfs_linkargs {
	struct nfs_fh *		fromfh;
	struct nfs_fh *		tofh;
	const char *		toname;
};

struct nfs_symlinkargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	const char *		topath;
	struct nfs_sattr *	sattr;
};

struct nfs_readdirargs {
	struct nfs_fh *		fh;
	__u32			cookie;
	void *			buffer;
	unsigned int		bufsiz;
};

struct nfs_diropok {
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

struct nfs_readres {
	struct nfs_fattr *	fattr;
	unsigned int		count;
};

struct nfs_readlinkres {
	char **			string;
	unsigned int *		lenp;
	unsigned int		maxlen;
	void *			buffer;
};

struct nfs_readdirres {
	void *			buffer;
	unsigned int		bufsiz;
};

#endif /* NFS_NEED_XDR_TYPES */
#endif /* __KERNEL__ */

#endif
