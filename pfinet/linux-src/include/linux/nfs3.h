/*
 * NFSv3 protocol definitions
 */
#ifndef _LINUX_NFS3_H
#define _LINUX_NFS3_H

#include <linux/sunrpc/msg_prot.h>
#include <linux/nfs.h>

#define NFS3_PORT		2049
#define NFS3_MAXDATA		8192
#define NFS3_MAXPATHLEN		PATH_MAX
#define NFS3_MAXNAMLEN		NAME_MAX
#define NFS3_MAXGROUPS		16
#define NFS3_FHSIZE		NFS_FHSIZE
#define NFS3_COOKIESIZE		4
#define NFS3_FIFO_DEV		(-1)
#define NFS3MODE_FMT		0170000
#define NFS3MODE_DIR		0040000
#define NFS3MODE_CHR		0020000
#define NFS3MODE_BLK		0060000
#define NFS3MODE_REG		0100000
#define NFS3MODE_LNK		0120000
#define NFS3MODE_SOCK		0140000
#define NFS3MODE_FIFO		0010000

	
enum nfs3_stat {
	NFS3_OK			= 0,
	NFS3ERR_PERM		= 1,
	NFS3ERR_NOENT		= 2,
	NFS3ERR_IO		= 5,
	NFS3ERR_NXIO		= 6,
	NFS3ERR_EAGAIN		= 11,
	NFS3ERR_ACCES		= 13,
	NFS3ERR_EXIST		= 17,
	NFS3ERR_XDEV		= 18,	/* new in NFSv3 */
	NFS3ERR_NODEV		= 19,
	NFS3ERR_NOTDIR		= 20,
	NFS3ERR_ISDIR		= 21,
	NFS3ERR_INVAL		= 22,	/* new in NFSv3 */
	NFS3ERR_FBIG		= 27,
	NFS3ERR_NOSPC		= 28,
	NFS3ERR_ROFS		= 30,
	NFS3ERR_MLINK		= 31,	/* new in NFSv3 */
	NFS3ERR_NAMETOOLONG	= 63,
	NFS3ERR_NOTEMPTY	= 66,
	NFS3ERR_DQUOT		= 69,
	NFS3ERR_STALE		= 70,
	NFS3ERR_REMOTE		= 71,	/* new in NFSv3 */
	NFS3ERR_BADHANDLE	= 10001,/* ditto */
	NFS3ERR_NOT_SYNC	= 10002,/* ditto */
	NFS3ERR_BAD_COOKIE	= 10003,/* ditto */
	NFS3ERR_NOTSUPP		= 10004,/* ditto */
	NFS3ERR_TOOSMALL	= 10005,/* ditto */
	NFS3ERR_SERVERFAULT	= 10006,/* ditto */
	NFS3ERR_BADTYPE		= 10007,/* ditto */
	NFS3ERR_JUKEBOX		= 10008,/* ditto */
};

enum nfs3_ftype {
	NF3NON  = 0,
	NF3REG  = 1,
	NF3DIR  = 2,
	NF3BLK  = 3,
	NF3CHR  = 4,
	NF3LNK  = 5,
	NF3SOCK = 6,
	NF3FIFO = 7,	/* changed from NFSv2 (was 8) */
	NF3BAD  = 8
};

#define NFS3_VERSION		3
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

#if defined(__KERNEL__) || defined(NFS_NEED_KERNEL_TYPES)

struct nfs3_fh {
	__u32			size;
	__u8			data[NFS3_FHSIZE];
};

struct nfs3_fattr {
	enum nfs3_ftype		type;
	__u32			mode;
	__u32			nlink;
	__u32			uid;
	__u32			gid;
	__u64			size;
	__u64			used;
	__u32			rdev_maj;
	__u32			rdev_min;
	__u32			fsid;
	__u32			fileid;
	struct nfs_time		atime;
	struct nfs_time		mtime;
	struct nfs_time		ctime;
};

struct nfs3_wcc_attr {
	__u64			size;
	struct nfs_time		mtime;
	struct nfs_time		ctime;
};

struct nfs3_wcc_data {
	struct nfs3_wcc_attr	before;
	struct nfs3_wcc_attr	after;
};

struct nfs3_sattr {
	__u32			valid;
	__u32			mode;
	__u32			uid;
	__u32			gid;
	__u64			size;
	struct nfs_time		atime;
	struct nfs_time		mtime;
};

struct nfs3_entry {
	__u32			fileid;
	char *			name;
	unsigned int		length;
	__u32			cookie;
	__u32			eof;
};

struct nfs3_fsinfo {
	__u32			tsize;
	__u32			bsize;
	__u32			blocks;
	__u32			bfree;
	__u32			bavail;
};

#ifdef NFS_NEED_XDR_TYPES

struct nfs3_sattrargs {
	struct nfs_fh *		fh;
	struct nfs_sattr *	sattr;
};

struct nfs3_diropargs {
	struct nfs_fh *		fh;
	const char *		name;
};

struct nfs3_readargs {
	struct nfs_fh *		fh;
	__u32			offset;
	__u32			count;
	void *			buffer;
};

struct nfs3_writeargs {
	struct nfs_fh *		fh;
	__u32			offset;
	__u32			count;
	const void *		buffer;
};

struct nfs3_createargs {
	struct nfs_fh *		fh;
	const char *		name;
	struct nfs_sattr *	sattr;
};

struct nfs3_renameargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	struct nfs_fh *		tofh;
	const char *		toname;
};

struct nfs3_linkargs {
	struct nfs_fh *		fromfh;
	struct nfs_fh *		tofh;
	const char *		toname;
};

struct nfs3_symlinkargs {
	struct nfs_fh *		fromfh;
	const char *		fromname;
	const char *		topath;
	struct nfs_sattr *	sattr;
};

struct nfs3_readdirargs {
	struct nfs_fh *		fh;
	__u32			cookie;
	void *			buffer;
	unsigned int		bufsiz;
};

struct nfs3_diropok {
	struct nfs_fh *		fh;
	struct nfs_fattr *	fattr;
};

struct nfs3_readres {
	struct nfs_fattr *	fattr;
	unsigned int		count;
};

struct nfs3_readlinkres {
	char **			string;
	unsigned int *		lenp;
	unsigned int		maxlen;
	void *			buffer;
};

struct nfs3_readdirres {
	void *			buffer;
	unsigned int		bufsiz;
};

/*
 * The following are for NFSv3
 */
struct nfs3_fh {
	__u32			size;
	__u8			data[NFS3_FHSIZE]
};

struct nfs3_wcc_attr {
	__u64			size;
	struct nfs_time		mtime;
	struct nfs_time		ctime;
};

#endif /* NFS_NEED_XDR_TYPES */
#endif /* __KERNEL__ */

#endif
