#ifndef _NFS_FS_SB
#define _NFS_FS_SB

#include <linux/nfs.h>
#include <linux/in.h>

/*
 * NFS client parameters stored in the superblock.
 */
struct nfs_server {
	struct rpc_clnt *	client;		/* RPC client handle */
	int			flags;		/* various flags */
	int			rsize;		/* read size */
	int			wsize;		/* write size */
	unsigned int		bsize;		/* server block size */
	unsigned int		acregmin;	/* attr cache timeouts */
	unsigned int		acregmax;
	unsigned int		acdirmin;
	unsigned int		acdirmax;
	char *			hostname;	/* remote hostname */
};

/*
 * nfs super-block data in memory
 */
struct nfs_sb_info {
	struct nfs_server	s_server;
	struct nfs_fh		s_root;
};

#endif
