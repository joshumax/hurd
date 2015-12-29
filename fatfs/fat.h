/* fat.h - Support for FAT filesystems interfaces.
   Copyright (C) 2002, 2003 Free Software Foundation, Inc.
   Written by Marcus Brinkmann.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#ifndef FAT_H
#define FAT_H

/* Specification of the FAT12/16/32 filesystem format.  */

/* Overview
   --------

   Any FAT fs consists of several regions, which follow immediately
   after each other.

   Reserved

     The reserved region consists of the boot sector, and with it the
     BIOS Parameter Block, which contains all necessary data about the
     filesystem like sector size, number of clusters etc. It also
     holds the filesystem info block.

     The reserved region of FAT32 filesystems also hold a backup copy
     of the root sector at sector 6 (usually), followed by a backup
     copy of the filesystem info sector.

     The number of sectors occupied by the reserved region is stored
     in the reserved region as well, in the word at offset 14
     (reserved_sectors).

   FAT

     The FAT region contains the File Allocation Table, which is a
     linked list of clusters occupied by each file or directory.
     There might be multiple FAT tables in the FAT region, for
     redundancy.

     The number of FATs is stored in the reserved region, in the byte
     at offset 16 (nr_of_fat_tables). The number of sectors per FAT is
     stored in the word at offset 22 (sectors_per_fat_16) or, if this
     is zero (as it is for FAT32), in the doubleword at offset 36
     (sectors_per_fat_32).

   Root Directory

     In FAT12/16, the root directory entries allocate their own region
     and are not accessed through the FAT.

     The size of this region is determined by the word at offset 17
     (nr_of_root_dirents). You have to multiply this with the nr of
     bytes per entry, and divide through the number of bytes per
     sector, rounding up.  On FAT32 filesystems, this region does not
     exist, and nr_of_root_dirents is zero. The FAT32 root directory
     is accessed through the FAT as any other directory is.

   Data

     The data region occupies the rest of the filesystem and stores
     the actual file and directory data. It is separated in clusters,
     which are indexed in the FAT.

     The size of the data region is stored in the word at offset 19
     (total_sectors_16) or, if this is zero, in the doubleword at
     offset 32 (total_sectors_32).


  NOTE that all meta data in a FAT filesystem is stored in little endian
  format.

*/

/* The supported FAT types.  */

enum fat { FAT12, FAT16, FAT32 };
typedef enum fat fat_t;

/* The FAT type is determined by the number of clusters in the data
   region, and nothing else.  The maximal number of clusters for a
   FAT12 and FAT16 respectively is defined here.
*/

#define FAT12_MAX_NR_OF_CLUSTERS 4084
#define FAT16_MAX_NR_OF_CLUSTERS 65524
#define FAT32_MAX_NR_OF_CLUSTERS (FAT32_BAD_CLUSTER - 1)

struct boot_sector
{
  /* Unused.  */
  unsigned char jump_to_boot_code[3];      /*   0, typ. 0xeb 0x?? 0x90  */
  unsigned char oem_name[8];               /*   3, typ. "MSWIN4.1"  */

  /* Sector and Cluster size.
     bytes_per_sector is usually 512, but 1024, 2048, 4096 are also allowed.
     sectors_per_cluster is one of 1, 2, 4, 8, 16, 32, 64, 128.
     Note that bytes per cluster (product of the two) must be <= 32768.  */
  unsigned char bytes_per_sector[2];       /*  11 */
  unsigned char sectors_per_cluster;       /*  13 */
  
  /* Size of the various regions.
     reserved_sectors must not be zero and is typically 1 on FAT12/16
     filesystems and 32 on FAT32 filesystems.
     nr_of_fat_tables must not be zero and is typically 2.
     nr_of_root_dirents must be zero on FAT32 filesystems.
     For FAT12/16, the value multiplied with DIR_ENTRY_SIZE (32)
     should always be a multiple of bytes_per_sector to retain
     compatibility. For FAT16, 512 should be used.
     total_sectors_16 contains the complete number of sectors if not zero.
     If zero, the number of sectors is stored in total_sectors_32.  */
  unsigned char reserved_sectors[2];       /*  14 */
  unsigned char nr_of_fat_tables;          /*  16 */
  unsigned char nr_of_root_dirents[2];     /*  17 */
  unsigned char total_sectors_16[2];       /*  19 */

  /* Media descriptor.
     Allowed are values between 0xf0 and 0xff.
     0xf8 is a fixed hardware (disk), 0xf0 denotes a removable media.
     Must be the same as the first byte in the FAT (compatibility
     with DOS 1.x).  */
  unsigned char media_descriptor;          /*  21 */

  /* Size of one FAT.
     On FAT32 systems, this value must be zero and sectors_per_fat_32
     used instead.  */
  unsigned char sectors_per_fat_16[2];     /*  22 */

  /* Disk geometry. Unused.  */
  unsigned char sectors_per_track[2];      /*  24 */
  unsigned char nr_of_heads[2];            /*  26 */
  unsigned char nr_of_hidden_sectors[4];   /*  28 */

  /* See total_sectors_16.  */
  unsigned char total_sectors_32[4];       /*  32 */

  /* FAT specific information.
     Starting with offset 36, FAT12/16 filesystems differ from FAT32
     filesystems.  */
  union
  {
    struct
    {
      unsigned char drive;                 /*  36 */
      unsigned char reserved;              /*  37 */

      /* Boot signature.
	 Value is 0x29.
	 Indicates that the following three fields
	 are present.  */
      unsigned char boot_signature;        /*  38 */

      /* Identifier.
	 serial is an unique identifier for removable media.
	 label is the filesystem label, which must match the label
	 stored in the root directory entry which has DIR_ATTR_LABEL
	 set. If no name is specified, the content is "NO NAME    ".
	 fs_type: One of "FAT12      ", "FAT16      ", "FAT        ".
	 Don't use.  */
      unsigned char serial[4];             /*  39 */
      unsigned char label[11];             /*  43 */
      unsigned char fs_type[8];            /*  54 */
    } fat;
    struct
    {
      /* See sectors_per_fat_16.  */
      unsigned char sectors_per_fat_32[4]; /*  36 */

      /* Extension flags.
	 Bits 0-3: Zero based nr of active FAT.
	 Bit 7: If 0, all FATs are active and should be kept up to date.
	        If 1, only the active FAT (see bits 0-3) should be used.
	 The rest of the bits are reserved.  */
      unsigned char extension_flags[2];    /*  40 */

      /* Filesystem version.
	 The high byte is the major number, the low byte the minor version.
	 Don't mount if either version number is higher than known versions. */
      unsigned char fs_version[2];         /*  42 */

      /* Root cluster.
	 The cluster where the root directory starts.  */
      unsigned char root_cluster[4];       /*  44 */

      /* Filesystem Info sector.
	 The setor number of the filesystem info block in the
	 reserved area.  */
      unsigned char fs_info_sector[2];     /*  48 */

      /* Backup boot sector.
	 The sector of the backup copy of the boot sector.
	 Should be 6, so it can be used even if this field is
	 corrupted.  */
      unsigned char backup_boot_sector[2]; /*  50 */
      unsigned char reserved1[12];         /*  52 */

      /* See fat structure above, with the following exception:
	 fs_type is "FAT32      ".  */
      unsigned char drive_number;          /*  64 */
      unsigned char reserved2;             /*  65 */
      unsigned char boot_signature;        /*  66 */
      unsigned char serial[4];             /*  67 */
      unsigned char label[11];             /*  71 */
      unsigned char fs_type[8];            /*  82 */
    } fat32;
  } compat;
  unsigned char unused[420];               /*  90 */

  /* Expected ID at offset 510.
   */
#define BOOT_SECTOR_ID 0xaa55

  unsigned char id[2];                     /* 510 */
};

/* File System Info Block. */

#define FAT_FS_INFO_LEAD_SIGNATURE		0x41615252L
#define FAT_FS_INFO_STRUCT_SIGNATURE		0x61417272L
#define FAT_FS_INFO_TRAIL_SIGNAURE		0xaa550000L
#define FAT_FS_NR_OF_FREE_CLUSTERS_UNKNOWN	0xffffffffL
#define FAT_FS_NEXT_FREE_CLUSTER_UNKNOWN	0xffffffffL

struct fat_fs_info
{
  unsigned char lead_signature[4];
  unsigned char reserved1[480];
  unsigned char struct_signature[4];
  unsigned char nr_of_free_clusters[4];
  unsigned char next_free_cluster[4];
  unsigned char reserved2[12];
  unsigned char trail_signature[4];
};

/* File Allocation Table, special entries.  */

#define FAT_FREE_CLUSTER	0

#define FAT12_BAD_CLUSTER	0x0ff7
#define FAT16_BAD_CLUSTER	0xfff7
#define FAT32_BAD_CLUSTER	0x0ffffff7L
#define FAT_BAD_CLUSTER		FAT32_BAD_CLUSTER

#define FAT12_EOC	0x0ff8
#define FAT16_EOC	0xfff8
#define FAT32_EOC	0x0ffffff8
#define FAT_EOC		FAT32_EOC

/* Directories.  */

#define FAT_DIR_REC_LEN		32
#define FAT_DIR_RECORDS(x)	FAT_DIR_REC_LEN    /* Something else for vfat.  */

#define FAT_DIR_ATTR_RDONLY	0x01
#define FAT_DIR_ATTR_HIDDEN	0x02
#define FAT_DIR_ATTR_SYSTEM	0x04
#define FAT_DIR_ATTR_LABEL	0x08
#define FAT_DIR_ATTR_DIR	0x10
#define FAT_DIR_ATTR_ARCHIVE	0x20
#define FAT_DIR_ATTR_LONGNAME	(DIR_ATTR_RDONLY | DIR_ATTR_HIDDEN \
				| DIR_ATTR_SYSTEM | DIR_ATTR_LABEL)

#define FAT_DIR_NAME_LAST	'\x00'
#define FAT_DIR_NAME_DELETED	'\xe5'

/* If the first character is this, replace it with FAT_DIR_NAME_DELETED
   after checking for it.  */
#define FAT_DIR_NAME_REPLACE_DELETED '\x05'

#define FAT_DIR_NAME_DOT	".          "
#define FAT_DIR_NAME_DOTDOT	"..         "

struct dirrect
{
  unsigned char name[11];
  unsigned char attribute;
  unsigned char reserved;
  unsigned char creation_time_centiseconds;
  unsigned char creation_time[2];
  unsigned char creation_date[2];
  unsigned char last_access_date[2];
  unsigned char first_cluster_high[2];
  unsigned char write_time[2];
  unsigned char write_date[2];
  unsigned char first_cluster_low[2];
  unsigned char file_size[4];
};

#define FAT_NAME_MAX 12   /* VFAT: 255 */

extern vm_offset_t first_data_byte;
extern size_t bytes_per_cluster;

/* A cluster number.  */
typedef unsigned long cluster_t;

#define LOG2_CLUSTERS_PER_TABLE 10
#define CLUSTERS_PER_TABLE (1 << LOG2_CLUSTERS_PER_TABLE)

struct cluster_chain
{
  struct cluster_chain *next;
  cluster_t cluster[CLUSTERS_PER_TABLE];
};

/* Prototyping.  */
void fat_read_sblock (void);
void fat_to_epoch (unsigned char *, unsigned char *, struct timespec *);
void fat_from_epoch (unsigned char *, unsigned char *, time_t *);
error_t fat_getcluster (struct node *, cluster_t, int, cluster_t *);
void fat_truncate_node (struct node *, cluster_t);
error_t fat_extend_chain (struct node *, cluster_t, int);
int fat_get_freespace (void);

/* Unprocessed superblock.  */
extern struct boot_sector *sblock;

/* Processed sblock info.  */
extern fat_t fat_type;
extern size_t bytes_per_sector;
extern size_t log2_bytes_per_sector;
extern size_t sectors_per_cluster;
extern size_t bytes_per_cluster;
extern unsigned int log2_bytes_per_cluster;
extern size_t sectors_per_fat;
extern size_t total_sectors;
extern size_t nr_of_root_dir_sectors;
extern size_t first_root_dir_byte;
extern size_t first_data_sector;
extern vm_offset_t first_data_byte;
extern size_t first_fat_sector;
extern cluster_t nr_of_clusters;
        
/* Numeric conversions for these fields.  */
#include <endian.h>
#include <byteswap.h>

static inline unsigned int 
read_dword (unsigned char *addr)
{
#if BYTE_ORDER == LITTLE_ENDIAN
  return *(unsigned int *)addr;
#elif BYTE_ORDER == BIG_ENDIAN
  return bswap_32 (*(unsigned int *) addr);
#else
#error unknown byte order
#endif
}

static inline unsigned int
read_word (unsigned char *addr)
{
#if BYTE_ORDER == LITTLE_ENDIAN
  return *(unsigned short *)addr;
#elif BYTE_ORDER == BIG_ENDIAN
  return bswap_16 (*(unsigned int *) addr);
#else
#error unknown byte order
#endif
}

static inline void 
write_dword (unsigned char *addr, unsigned int value)
{
#if BYTE_ORDER == LITTLE_ENDIAN
  *(unsigned int *)addr = value;
#elif BYTE_ORDER == BIG_ENDIAN
  *(unsigned int *)addr = bswap_32 (value);
#else
#error unknown byte order
#endif
}

static inline void
write_word (unsigned char *addr, unsigned int value)
{
#if BYTE_ORDER == LITTLE_ENDIAN
  *(unsigned short *)addr = value;
#elif BYTE_ORDER == BIG_ENDIAN
  *(unsigned int *)addr = bswap_16 (value);
#else
#error unknown byte order
#endif
}

#endif /* FAT_H */
