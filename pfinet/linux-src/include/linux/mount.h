/*
 *
 * Definitions for mount interface. This describes the in the kernel build 
 * linkedlist with mounted filesystems.
 *
 * Author:  Marco van Wieringen <mvw@planets.elm.net>
 *
 * Version: $Id: mount.h,v 2.0 1996/11/17 16:48:14 mvw Exp mvw $
 *
 */
#ifndef _LINUX_MOUNT_H
#define _LINUX_MOUNT_H

#define DQUOT_USR_ENABLED	0x01		/* User diskquotas enabled */
#define DQUOT_GRP_ENABLED	0x02		/* Group diskquotas enabled */

struct quota_mount_options
{
	unsigned int flags;			/* Flags for diskquotas on this device */
	struct semaphore dqio_sem;		/* lock device while I/O in progress */
	struct semaphore dqoff_sem;		/* serialize quota_off() and quota_on() on device */
	struct file *files[MAXQUOTAS];		/* fp's to quotafiles */
	time_t inode_expire[MAXQUOTAS];		/* expiretime for inode-quota */
	time_t block_expire[MAXQUOTAS];		/* expiretime for block-quota */
	char rsquash[MAXQUOTAS];		/* for quotas treat root as any other user */
};

struct vfsmount
{
  kdev_t mnt_dev;			/* Device this applies to */
  char *mnt_devname;			/* Name of device e.g. /dev/dsk/hda1 */
  char *mnt_dirname;			/* Name of directory mounted on */
  unsigned int mnt_flags;		/* Flags of this device */
  struct super_block *mnt_sb;		/* pointer to superblock */
  struct quota_mount_options mnt_dquot;	/* Diskquota specific mount options */
  struct vfsmount *mnt_next;		/* pointer to next in linkedlist */
};

struct vfsmount *lookup_vfsmnt(kdev_t dev);

/*
 *	Umount options
 */
 
#define MNT_FORCE	0x00000001	/* Attempt to forcibily umount */

#endif /* _LINUX_MOUNT_H */
