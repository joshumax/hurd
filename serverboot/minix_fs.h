/*
 * minix_fs.h
 * stolen (and slightly extended by csb) from the Linux distribution
 * Copyright (C) 1994 Linus Torvalds
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _LINUX_MINIX_FS_H
#define _LINUX_MINIX_FS_H

/*
 * The minix filesystem constants/structures
 */

/*
 * Thanks to Kees J Bot for sending me the definitions of the new
 * minix filesystem (aka V2) with bigger inodes and 32-bit block
 * pointers. It's not actually implemented yet, but I'll look into
 * it.
 */

#define MINIX_ROOT_INO 1

/* Not the same as the bogus LINK_MAX in <linux/limits.h>. Oh well. */
#define MINIX_LINK_MAX	250

#define MINIX_I_MAP_SLOTS	8
#define MINIX_Z_MAP_SLOTS	8
#define MINIX_SUPER_MAGIC	0x137F		/* original minix fs */
#define MINIX_SUPER_MAGIC2	0x138F		/* minix fs, 30 char names */
#define NEW_MINIX_SUPER_MAGIC	0x2468		/* minix V2 - not implemented */
#define MINIX_VALID_FS		0x0001		/* Clean fs. */
#define MINIX_ERROR_FS		0x0002		/* fs has errors. */

#define MINIX_INODES_PER_BLOCK ((MINIX_BLOCK_SIZE)/(sizeof (struct minix_inode)))

struct minix_inode {
	unsigned short i_mode;
	unsigned short i_uid;
	unsigned long i_size;
	unsigned long i_time;
	unsigned char i_gid;
	unsigned char i_nlinks;
	unsigned short i_zone[9];
};

/*
 * The new minix inode has all the time entries, as well as
 * long block numbers and a third indirect block (7+1+1+1
 * instead of 7+1+1). Also, some previously 8-bit values are
 * now 16-bit. The inode is now 64 bytes instead of 32.
 */
struct new_minix_inode {
	unsigned short i_mode;
	unsigned short i_nlinks;
	unsigned short i_uid;
	unsigned short i_gid;
	unsigned long i_size;
	unsigned long i_atime;
	unsigned long i_mtime;
	unsigned long i_ctime;
	unsigned long i_zone[10];
};

/*
 * minix super-block data on disk
 */
struct minix_super_block {
	unsigned short s_ninodes;
	unsigned short s_nzones;
	unsigned short s_imap_blocks;
	unsigned short s_zmap_blocks;
	unsigned short s_firstdatazone;
	unsigned short s_log_zone_size;
	unsigned long s_max_size;
	unsigned short s_magic;
	unsigned short s_state;
};

struct minix_dir_entry {
	unsigned short inode;
	char name[0];
};

struct minix_directory_entry {
	unsigned short inode;
	char name[14];
};

#define	MINIX_NIADDR	2

typedef	unsigned short	minix_daddr_t;

#endif
