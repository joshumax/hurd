/*
 * BSD FFS like functions used to ease porting bootstrap to MINIX fs
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

#include <device/device_types.h>
#include <device/device.h>

#include <mach/mach_traps.h>
#include <mach/mach_interface.h>

#include <file_io.h>

#define	MINIX_BLOCK_SIZE	1024

int minix_ino2blk (struct minix_super_block *fs, int ino)
{
        int blk;

	blk=0 /* it's Mach */+2 /* boot+superblock */ + fs->s_imap_blocks +
		fs->s_zmap_blocks + (ino-1)/MINIX_INODES_PER_BLOCK;
        return blk;
}

int minix_fsbtodb (struct minix_super_block *fs, int b)
{
        return (b * MINIX_BLOCK_SIZE) / DEV_BSIZE;
}

int minix_itoo (struct minix_super_block *fs, int ino)
{
	return (ino - 1) % MINIX_INODES_PER_BLOCK;
}

int minix_blkoff (struct minix_super_block * fs, vm_offset_t offset)
{
	return offset % MINIX_BLOCK_SIZE;
}

int minix_lblkno (struct minix_super_block * fs, vm_offset_t offset)
{
	return offset / MINIX_BLOCK_SIZE;
}

int minix_blksize (struct minix_super_block *fs, struct file *fp, minix_daddr_t file_block)
{
	return MINIX_BLOCK_SIZE;
}
