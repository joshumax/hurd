/*
 * BSD FFS like declarations used to ease porting bootstrap to Linux ext2 fs
 * Copyright (C) 1994 Remy Card
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

#define	SBSIZE		EXT2_MIN_BLOCK_SIZE	/* Size of superblock */
#define	SBLOCK		((daddr_t) 2)		/* Location of superblock */

#define	NDADDR		EXT2_NDIR_BLOCKS
#define	NIADDR		(EXT2_N_BLOCKS - EXT2_NDIR_BLOCKS)

#define	MAXNAMLEN	255

#define	ROOTINO		EXT2_ROOT_INO

#define	NINDIR(fs)	EXT2_ADDR_PER_BLOCK(fs)

#define	IC_FASTLINK

#define	IFMT		00170000
#define	IFSOCK		0140000
#define	IFLNK		0120000
#define	IFREG		0100000
#define	IFBLK		0060000
#define	IFDIR		0040000
#define	IFCHR		0020000
#define	IFIFO		0010000
#define	ISUID		0004000
#define	ISGID		0002000
#define	ISVTX		0001000

#define f_fs		u.ext2.ext2_fs
#define f_gd		u.ext2.ext2_gd
#define f_gd_size	u.ext2.ext2_gd_size
#define i_ic		u.ext2.ext2_ic
#define f_nindir	u.ext2.ext2_nindir
#define f_blk		u.ext2.ext2_blk
#define f_blksize	u.ext2.ext2_blksize
#define f_blkno		u.ext2.ext2_blkno

