/*
 * BSD FFS like declarations used to ease porting bootstrap to MINIX fs
 * Copyright (C) 1994 Csizmazia Balazs, University ELTE, Hungary
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

#define	MINIX_SBSIZE		MINIX_BLOCK_SIZE	/* Size of superblock */
#define	MINIX_SBLOCK		((minix_daddr_t) 2)		/* Location of superblock */

#define	MINIX_NDADDR		7
#define	MINIX_NIADDR		2

#define	MINIX_MAXNAMLEN	14

#define	MINIX_ROOTINO		1 /* MINIX ROOT INODE */

#define	MINIX_NINDIR(fs)	512 /* DISK_ADDRESSES_PER_BLOCKS */

#define	IFMT		00170000
#define	IFREG		0100000
#define	IFDIR		0040000
#define	ISVTX		0001000

#define f_fs		u.minix.minix_fs
#define i_ic		u.minix.minix_ic
#define f_nindir	u.minix.minix_nindir
#define f_blk		u.minix.minix_blk
#define f_blksize	u.minix.minix_blksize
#define f_blkno		u.minix.minix_blkno

