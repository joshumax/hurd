 /* Ext2 support for extended attributes

   Copyright (C) 2006, 2016 Free Software Foundation, Inc.

   Written by Thadeu Lima de Souza Cascardo <cascardo@dcc.ufmg.br>
   and Shengyu Zhang <lastavengers@outlook.com>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef EXT2_XATTR_H
#define EXT2_XATTR_H

#include "ext2fs.h"

/* Identifies whether a block is a proper xattr block. */
#define EXT2_XATTR_BLOCK_MAGIC 0xEA020000

/* xattr block header. */
struct ext2_xattr_header
{
  __u32 h_magic;	/* h_magic number for identification */
  __u32 h_refcount;	/* reference count */
  __u32 h_blocks;	/* number of disk blocks used */
  __u32 h_hash;	/* hash value of all attributes */
  __u32 h_reserved[4];	/* zero right now */
};

/* xattr entry in xattr block. */
struct ext2_xattr_entry
{
  __u8 e_name_len;	/* length of name */
  __u8 e_name_index;	/* attribute name index */
  __u16 e_value_offs;	/* offset in disk block of value */
  __u32 e_value_block;	/* disk block attribute is stored on (n/i) */
  __u32 e_value_size;	/* size of attribute value */
  __u32 e_hash;		/* hash value of name and value */
  char e_name[0];	/* attribute name */
};

#define EXT2_XATTR_PAD_BITS 2
#define EXT2_XATTR_PAD (1 << EXT2_XATTR_PAD_BITS)
#define EXT2_XATTR_ROUND (EXT2_XATTR_PAD - 1)

/* Entry alignment in xattr block. */
#define EXT2_XATTR_ALIGN(x) (((unsigned long) (x) + \
			      EXT2_XATTR_ROUND) & \
			     (~EXT2_XATTR_ROUND))

/* Given a fs block, return the xattr header. */
#define EXT2_XATTR_HEADER(block) ((struct ext2_xattr_header *) block)

/* Aligned size of entry, including the name length. */
#define EXT2_XATTR_ENTRY_SIZE(len) EXT2_XATTR_ALIGN ((sizeof \
						      (struct ext2_xattr_entry) + \
						      len))

/* Offset of entry, given the block header. */
#define EXT2_XATTR_ENTRY_OFFSET(header, entry) ((off_t) ((char *) entry - \
							 (char *) header))

/* First entry of xattr block, given its header. */
#define EXT2_XATTR_ENTRY_FIRST(header) ((struct ext2_xattr_entry *) (header + 1))

/* Next entry, giving an entry. */
#define EXT2_XATTR_ENTRY_NEXT(entry) ((struct ext2_xattr_entry *) \
				      ((char *) entry + \
				       EXT2_XATTR_ENTRY_SIZE \
				       (entry->e_name_len)))

/* Checks if this entry is the last (not valid) one. */
#define EXT2_XATTR_ENTRY_LAST(entry) (*(unsigned long *) entry == 0UL)

#endif
