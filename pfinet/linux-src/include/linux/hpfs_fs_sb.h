#ifndef _HPFS_FS_SB
#define _HPFS_FS_SB

struct hpfs_sb_info {
	ino_t sb_root;			/* inode number of root dir */
	unsigned sb_fs_size;		/* file system size, sectors */
	unsigned sb_bitmaps;		/* sector number of bitmap list */
	unsigned sb_dirband_size;	/* directory band size, dnodes */
	unsigned sb_dmap;		/* sector number of dnode bit map */
	unsigned sb_n_free;		/* free blocks for statfs, or -1 */
	unsigned sb_n_free_dnodes;	/* free dnodes for statfs, or -1 */
	uid_t sb_uid;			/* uid from mount options */
	gid_t sb_gid;			/* gid from mount options */
	umode_t sb_mode;		/* mode from mount options */
	unsigned sb_lowercase : 1;	/* downcase filenames hackery */
	unsigned sb_conv : 2;		/* crlf->newline hackery */
};

#define s_hpfs_root u.hpfs_sb.sb_root
#define s_hpfs_fs_size u.hpfs_sb.sb_fs_size
#define s_hpfs_bitmaps u.hpfs_sb.sb_bitmaps
#define s_hpfs_dirband_size u.hpfs_sb.sb_dirband_size
#define s_hpfs_dmap u.hpfs_sb.sb_dmap
#define s_hpfs_uid u.hpfs_sb.sb_uid
#define s_hpfs_gid u.hpfs_sb.sb_gid
#define s_hpfs_mode u.hpfs_sb.sb_mode
#define s_hpfs_n_free u.hpfs_sb.sb_n_free
#define s_hpfs_n_free_dnodes u.hpfs_sb.sb_n_free_dnodes
#define s_hpfs_lowercase u.hpfs_sb.sb_lowercase
#define s_hpfs_conv u.hpfs_sb.sb_conv

#endif
