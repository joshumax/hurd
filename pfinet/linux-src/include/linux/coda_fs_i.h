/*
 *  coda_fs_i.h
 *
 *  Copyright (C) 1998 Carnegie Mellon University
 *
 */

#ifndef _LINUX_CODA_FS_I
#define _LINUX_CODA_FS_I

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/list.h>
#include <linux/coda.h>



#define CODA_CNODE_MAGIC        0x47114711
/*
 * coda fs inode data
 */
struct coda_inode_info {
	/*
	 * This is a place holder so named pipes work (more or less
	 * correctly). This must be first in the struct because the
	 * data is really accessed via inode->u.pipe_i.
	 */
	struct pipe_inode_info  pipeinfo;

        struct ViceFid     c_fid;	/* Coda identifier */
        u_short	           c_flags;     /* flags (see below) */
        u_short	           c_ocount;    /* count of openers */
        u_short            c_owrite;    /* count of open for write */
        u_short            c_mmcount;   /* count of mmappers */
        struct inode      *c_ovp;       /* open inode  pointer */
        struct list_head   c_cnhead;    /* head of cache entries */
	struct list_head   c_volrootlist; /* list of volroot cnoddes */
        struct inode      *c_vnode;     /*  inode associated with cnode */
        int                c_magic;     /* to verify the data structure */
};

/* flags */
#define C_VATTR       0x1   /* Validity of vattr in inode */
#define C_PURGE       0x8
#define C_ZAPDIR      0x10
#define C_DYING       0x4   /* from venus (which died) */
#define C_INITED      0x20
#define C_FLUSH       0x2   /* used after a flush */

int coda_cnode_make(struct inode **, struct ViceFid *, struct super_block *);
int coda_cnode_makectl(struct inode **inode, struct super_block *sb);
struct inode *coda_fid_to_inode(ViceFid *fid, struct super_block *sb);
void coda_replace_fid(struct inode *, ViceFid *, ViceFid *);

#endif
#endif
