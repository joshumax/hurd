/*
 * BSD FFS like functions used to ease porting bootstrap to Linux ext2 fs
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

#include <device/device_types.h>
#include <device/device.h>

#include <mach/mach_traps.h>
#include <mach/mach_interface.h>

#include <file_io.h>

int ino2blk (struct ext2_super_block *fs, struct ext2_group_desc *gd, int ino)
{
        int group;
        int blk;

        group = (ino - 1) / EXT2_INODES_PER_GROUP(fs);
        blk = gd[group].bg_inode_table +
	      (((ino - 1) % EXT2_INODES_PER_GROUP(fs)) /
               EXT2_INODES_PER_BLOCK(fs));
        return blk;
}

int fsbtodb (struct ext2_super_block *fs, int b)
{
        return (b * EXT2_BLOCK_SIZE(fs)) / DEV_BSIZE;
}

int itoo (struct ext2_super_block *fs, int ino)
{
	return (ino - 1) % EXT2_INODES_PER_BLOCK(fs);
}

int blkoff (struct ext2_super_block * fs, vm_offset_t offset)
{
	return offset % EXT2_BLOCK_SIZE(fs);
}

int lblkno (struct ext2_super_block * fs, vm_offset_t offset)
{
	return offset / EXT2_BLOCK_SIZE(fs);
}

int blksize (struct ext2_super_block *fs, struct file *fp, daddr_t file_block)
{
	return EXT2_BLOCK_SIZE(fs);	/* XXX - fix for fragments */
}
