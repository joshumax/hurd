/* JBD2 Standard On-Disk Layout

   Copyright (C) 2026 Free Software Foundation, Inc.
   Written by Milos Nikic.

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

#ifndef _JBD2_FORMAT_H
#define _JBD2_FORMAT_H

#include <stdint.h>

/**
 * JBD2 Magic Numbers
 * If a block starts with this, it's a metadata block.
 */
#define JBD2_MAGIC_NUMBER 0xC03B3998U

/* Block Types */
#define JBD2_DESCRIPTOR_BLOCK 1
#define JBD2_COMMIT_BLOCK     2
#define JBD2_SUPERBLOCK_V1    3
#define JBD2_SUPERBLOCK_V2    4
#define JBD2_REVOKE_BLOCK     5

#define JBD2_PACKED __attribute__((packed))

/**
 * The Journal Superblock (Version 2).
 * Lives at the very start of the journal partition (typically Inode 8).
 */
typedef struct JBD2_PACKED journal_superblock_s
{
  uint32_t s_header[3];		/* Standard header (magic, type, etc) */
  uint32_t s_blocksize;		/* Journal device blocksize */
  uint32_t s_maxlen;		/* Total blocks in journal file */
  uint32_t s_first;		/* First block of log information */

  uint32_t s_sequence;		/* First commit ID expected in log */
  uint32_t s_start;		/* Block number of start of log */

  uint32_t s_errno;		/* Error value, if any */

  uint32_t s_feature_compat;
  uint32_t s_feature_incompat;
  uint32_t s_feature_ro_compat;

  uint8_t s_uuid[16];		/* 128-bit uuid for journal */
  uint32_t s_nr_users;		/* Nr of filesystems sharing log */
  uint32_t s_dynsuper;		/* Blocknr of dynamic superblock copy */
  uint32_t s_max_transaction;	/* Limit of handle size */
  uint32_t s_max_trans_data;	/* Limit of data blocks per trans */
  uint32_t s_checksum_type;	/* checksum type */
  uint8_t s_padding2[42 * 4];
  uint32_t s_checksum;		/* crc32c(superblock) */
  uint8_t s_users[16 * 48];	/* ids of all filesystems sharing log */
} journal_superblock_t;

_Static_assert (sizeof (journal_superblock_t) == 1024,
		"JBD2 Superblock size mismatch! Check padding.");

/**
 * The Standard Header
 * Every metadata block (Descriptor, Commit) starts with this.
 */
typedef struct JBD2_PACKED journal_header_s
{
  uint32_t h_magic;		/* 0xC03B3998 */
  uint32_t h_blocktype;		/* Descriptor, Commit, etc. */
  uint32_t h_sequence;		/* The Transaction ID */
} journal_header_t;

/**
 * The Block Tag
 * Describes a data block that follows.
 * Structure: [Descriptor Block] [Tag 1] [Tag 2] ... [Data 1] [Data 2] ...
 */
typedef struct JBD2_PACKED journal_block_tag_s
{
  uint32_t t_blocknr;		/* The 32-bit physical block number */
  uint32_t t_flags;		/* See flags below */
} journal_block_tag_t;

/* Flags for the Block Tag */
#define JBD2_FLAG_ESCAPE    1	/* The data block starts with magic number (escaped) */
#define JBD2_FLAG_SAME_UUID 2	/* (Not needed for us usually) */
#define JBD2_FLAG_DELETED   4	/* Block was deleted */
#define JBD2_FLAG_LAST_TAG  8	/* This is the last tag in this descriptor block */

#endif
