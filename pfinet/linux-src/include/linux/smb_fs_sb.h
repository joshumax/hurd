/*
 *  smb_fs_sb.h
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *  Copyright (C) 1997 by Volker Lendecke
 *
 */

#ifndef _SMB_FS_SB
#define _SMB_FS_SB

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/smb.h>

/* Get the server for the specified dentry */
#define server_from_dentry(dentry) &dentry->d_sb->u.smbfs_sb
#define SB_of(server) ((struct super_block *) ((char *)(server) - \
	(unsigned long)(&((struct super_block *)0)->u.smbfs_sb)))

struct smb_sb_info {
        enum smb_conn_state state;
	struct file * sock_file;

        struct smb_mount_data *mnt;
        unsigned char *temp_buf;

	/* Connections are counted. Each time a new socket arrives,
	 * generation is incremented.
	 */
	unsigned int generation;
	pid_t conn_pid;
	struct smb_conn_opt opt;

	struct semaphore sem;
	struct wait_queue * wait;

	__u32              packet_size;
	unsigned char *    packet;
        unsigned short     rcls; /* The error codes we received */
        unsigned short     err;

        /* We use our on data_ready callback, but need the original one */
        void *data_ready;
};

#endif /* __KERNEL__ */

#endif
