#ifndef _ADFS_FS_H
#define _ADFS_FS_H

#include <linux/types.h>
/*
 * Structures of data on the disk
 */

/*
 * Disc Record at disc address 0xc00
 */
struct adfs_discrecord {
    unsigned char  log2secsize;
    unsigned char  secspertrack;
    unsigned char  heads;
    unsigned char  density;
    unsigned char  idlen;
    unsigned char  log2bpmb;
    unsigned char  skew;
    unsigned char  bootoption;
    unsigned char  lowsector;
    unsigned char  nzones;
    unsigned short zone_spare;
    unsigned long  root;
    unsigned long  disc_size;
    unsigned short disc_id;
    unsigned char  disc_name[10];
    unsigned long  disc_type;
    unsigned long  disc_size_high;
    unsigned char  log2sharesize:4;
    unsigned char  unused:4;
    unsigned char  big_flag:1;
};

#define ADFS_DISCRECORD		(0xc00)
#define ADFS_DR_OFFSET		(0x1c0)
#define ADFS_DR_SIZE		 60
#define ADFS_SUPER_MAGIC	 0xadf5
#define ADFS_FREE_FRAG		 0
#define ADFS_BAD_FRAG		 1
#define ADFS_ROOT_FRAG		 2

/*
 * Directory header
 */
struct adfs_dirheader {
	unsigned char startmasseq;
	unsigned char startname[4];
};

#define ADFS_NEWDIR_SIZE	2048
#define ADFS_OLDDIR_SIZE	1024
#define ADFS_NUM_DIR_ENTRIES	77

/*
 * Directory entries
 */
struct adfs_direntry {
	char dirobname[10];
#define ADFS_NAME_LEN 10
	__u8 dirload[4];
	__u8 direxec[4];
	__u8 dirlen[4];
	__u8 dirinddiscadd[3];
	__u8 newdiratts;
#define ADFS_NDA_OWNER_READ	(1 << 0)
#define ADFS_NDA_OWNER_WRITE	(1 << 1)
#define ADFS_NDA_LOCKED		(1 << 2)
#define ADFS_NDA_DIRECTORY	(1 << 3)
#define ADFS_NDA_EXECUTE	(1 << 4)
#define ADFS_NDA_PUBLIC_READ	(1 << 5)
#define ADFS_NDA_PUBLIC_WRITE	(1 << 6)
};

#define ADFS_MAX_NAME_LEN	255
struct adfs_idir_entry {
	__u32		inode_no;			/* Address		*/
	__u32		file_id;			/* file id		*/
	__u32		name_len;			/* name length		*/
	__u32		size;				/* size			*/
	__u32		mtime;				/* modification time	*/
	__u32		filetype;			/* RiscOS file type	*/
	__u8		mode;				/* internal mode	*/
	char		name[ADFS_MAX_NAME_LEN];	/* file name		*/
};

/*
 * Directory tail
 */
union adfs_dirtail {
	struct {
		unsigned char dirlastmask;
		char dirname[10];
		unsigned char dirparent[3];
		char dirtitle[19];
		unsigned char reserved[14];
		unsigned char endmasseq;
		unsigned char endname[4];
		unsigned char dircheckbyte;
	} old;
	struct {
		unsigned char dirlastmask;
		unsigned char reserved[2];
		unsigned char dirparent[3];
		char dirtitle[19];
		char dirname[10];
		unsigned char endmasseq;
		unsigned char endname[4];
		unsigned char dircheckbyte;
	} new;
};

#ifdef __KERNEL__
/*
 * Calculate the boot block checksum on an ADFS drive.  Note that this will
 * appear to be correct if the sector contains all zeros, so also check that
 * the disk size is non-zero!!!
 */
extern inline int adfs_checkbblk(unsigned char *ptr)
{
	unsigned int result = 0;
	unsigned char *p = ptr + 511;

	do {
	        result = (result & 0xff) + (result >> 8);
        	result = result + *--p;
	} while (p != ptr);

	return (result & 0xff) != ptr[511];
}

/* dir.c */
extern unsigned int adfs_val (unsigned char *p, int len);
extern int adfs_dir_read_parent (struct inode *inode, struct buffer_head **bhp);
extern int adfs_dir_read (struct inode *inode, struct buffer_head **bhp);
extern int adfs_dir_check (struct inode *inode, struct buffer_head **bhp,
			   int buffers, union adfs_dirtail *dtp);
extern void adfs_dir_free (struct buffer_head **bhp, int buffers);
extern int adfs_dir_get (struct super_block *sb, struct buffer_head **bhp,
			 int buffers, int pos, unsigned long parent_object_id,
			 struct adfs_idir_entry *ide);
extern int adfs_dir_find_entry (struct super_block *sb, struct buffer_head **bhp,
				int buffers, unsigned int index,
				struct adfs_idir_entry *ide);

/* inode.c */
extern int adfs_inode_validate (struct inode *inode);
extern unsigned long adfs_inode_generate (unsigned long parent_id, int diridx);
extern unsigned long adfs_inode_objid (struct inode *inode);
extern unsigned int adfs_parent_bmap (struct inode *inode, int block);
extern int adfs_bmap (struct inode *inode, int block);
extern void adfs_read_inode (struct inode *inode);

/* map.c */
extern int adfs_map_lookup (struct super_block *sb, int frag_id, int offset);

/* namei.c */
extern struct dentry *adfs_lookup (struct inode *dir, struct dentry *dentry);

/* super.c */
extern int init_adfs_fs (void);
extern void adfs_error (struct super_block *, const char *, const char *, ...);

/*
 * Inodes and file operations
 */

/* dir.c */
extern struct inode_operations adfs_dir_inode_operations;

/* file.c */
extern struct inode_operations adfs_file_inode_operations;
#endif

#endif
