/*
 *  Name                         : qnx4_fs_i.h
 *  Author                       : Richard Frowijn
 *  Function                     : qnx4 inode definitions
 *  Version                      : 1.0
 *  Last modified                : 25-05-1998
 * 
 *  History                      : 23-03-1998 created
 * 
 */
#ifndef _QNX4_FS_I
#define _QNX4_FS_I

#include <linux/qnxtypes.h>

/*
 * qnx4 fs inode entry 
 */
struct qnx4_inode_info {
	char i_reserved[16];	/* 16 */
	off_t i_size;		/*  4 */
	_xtnt_t i_first_xtnt;	/*  8 */
	long i_xblk;		/*  4 */
	time_t i_ftime;		/*  4 */
	time_t i_mtime;		/*  4 */
	time_t i_atime;		/*  4 */
	time_t i_ctime;		/*  4 */
	_nxtnt_t i_num_xtnts;	/*  2 */
	mode_t i_mode;		/*  2 */
	muid_t i_uid;		/*  2 */
	mgid_t i_gid;		/*  2 */
	nlink_t i_nlink;	/*  2 */
	char i_zero[4];		/*  4 */
	_ftype_t i_type;	/*  1 */
	unsigned char i_status;	/*  1 */
};

#endif
