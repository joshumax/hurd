#ifndef _HPFS_FS_I
#define _HPFS_FS_I

struct hpfs_inode_info {
	ino_t i_parent_dir;	/* (directories) gives fnode of parent dir */
	unsigned i_dno;		/* (directories) root dnode */
	unsigned i_dpos;	/* (directories) temp for readdir */
	unsigned i_dsubdno;	/* (directories) temp for readdir */
	unsigned i_file_sec;	/* (files) minimalist cache of alloc info */
	unsigned i_disk_sec;	/* (files) minimalist cache of alloc info */
	unsigned i_n_secs;	/* (files) minimalist cache of alloc info */
	unsigned i_conv : 2;	/* (files) crlf->newline hackery */
};

#define i_hpfs_dno u.hpfs_i.i_dno
#define i_hpfs_parent_dir u.hpfs_i.i_parent_dir
#define i_hpfs_n_secs u.hpfs_i.i_n_secs
#define i_hpfs_file_sec u.hpfs_i.i_file_sec
#define i_hpfs_disk_sec u.hpfs_i.i_disk_sec
#define i_hpfs_dpos u.hpfs_i.i_dpos
#define i_hpfs_dsubdno u.hpfs_i.i_dsubdno
#define i_hpfs_conv u.hpfs_i.i_conv

#endif
