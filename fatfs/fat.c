/* fat.c - Support for FAT filesystems.
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

#include <string.h>
#include <error.h>
#include <limits.h>
#include <errno.h>
#include <assert-backtrace.h>
#include <ctype.h>
#include <time.h>

#include <hurd/store.h>
#include <hurd/diskfs.h>

#include "fatfs.h"

/* Unprocessed superblock.  */
struct boot_sector *sblock;

/* Processed sblock info.  */
fat_t fat_type;
size_t bytes_per_sector;
size_t log2_bytes_per_sector;
size_t sectors_per_cluster;
size_t bytes_per_cluster;
unsigned int log2_bytes_per_cluster;
size_t sectors_per_fat;
size_t total_sectors;
size_t nr_of_root_dir_sectors;
size_t first_root_dir_byte;
size_t first_data_sector;
vm_offset_t first_data_byte;
size_t first_fat_sector;
cluster_t nr_of_clusters;

/* Hold this lock while converting times using gmtime.  */
pthread_spinlock_t epoch_to_time_lock = PTHREAD_SPINLOCK_INITIALIZER;

/* Hold this lock while allocating a new cluster in the FAT.  */
pthread_spinlock_t allocate_free_cluster_lock = PTHREAD_SPINLOCK_INITIALIZER;

/* Where to look for the next free cluster. This is meant to avoid
   searching through a nearly full file system from the beginning at
   every request.  It would be better to use the field of the same
   name in the fs_info block. 2 is the first data cluster in any
   FAT.  */
cluster_t next_free_cluster = 2;


/* Read the superblock.  */
void
fat_read_sblock (void)
{
  error_t err;
  size_t read;

  sblock = malloc (sizeof (struct boot_sector));
  err = store_read (store, 0, sizeof (struct boot_sector),
		    (void **) &sblock, &read);
  if (err)
    error (1, err, "Could not read superblock");

  if (read_word(sblock->id) != BOOT_SECTOR_ID)
    error (1, 0, "Could not find valid superblock");

  /* Parse some important bits of the superblock.  */

  bytes_per_sector = read_word (sblock->bytes_per_sector);
  switch (bytes_per_sector)
    {
    case 512:
      log2_bytes_per_sector = 9;
      break;
      
    case 1024:
      log2_bytes_per_sector = 10;
      break;
	
    case 2048:
      log2_bytes_per_sector = 11;
      break;
      
    case 4096:
      log2_bytes_per_sector = 12;
      break;
      
    default:
      error (1, 0, "Invalid number of bytes per sector");
    };

  sectors_per_cluster = sblock->sectors_per_cluster;
  if (sectors_per_cluster != 1 && sectors_per_cluster != 2
      && sectors_per_cluster != 4 && sectors_per_cluster != 8
      && sectors_per_cluster != 16 && sectors_per_cluster != 32
      && sectors_per_cluster != 64 && sectors_per_cluster != 128)
    error (1, 0, "Invalid number of sectors per cluster");

  bytes_per_cluster = sectors_per_cluster << log2_bytes_per_sector;
  switch (bytes_per_cluster)
    {
    case 512:
      log2_bytes_per_cluster = 9;
      break;
      
    case 1024:
      log2_bytes_per_cluster = 10;
      break;
      
    case 2048:
      log2_bytes_per_cluster = 11;
      break;
      
    case 4096:
      log2_bytes_per_cluster = 12;
      break;
      
    case 8192:
      log2_bytes_per_cluster = 13;
      break;
      
    case 16384:
      log2_bytes_per_cluster = 14;
      break;

    case 32768:
      log2_bytes_per_cluster = 15;
      break;
      
    default:
      error (1, 0, "Invalid number of bytes per cluster");
    };
  
  total_sectors = read_word (sblock->total_sectors_16)
    ?: read_word (sblock->total_sectors_32);
  if (total_sectors * bytes_per_sector > store->size)
    error (1, 0, "Store is smaller then implied by metadata");
  if (total_sectors == 0)
    error (1, 0, "Number of total sectors is zero");

  if (bytes_per_sector & (store->block_size - 1))
    error (1, 0, "Block size of filesystem is not"
          " a multiple of the block size of the store");

  if (read_word (sblock->reserved_sectors) == 0)
    error (1, 0, "Number of reserved sectors is zero");
  if (sblock->nr_of_fat_tables == 0)
    error (1, 0, "Number of FATs is zero");

  sectors_per_fat = read_word (sblock->sectors_per_fat_16)
    ?: read_word (sblock->compat.fat32.sectors_per_fat_32);
  if (sectors_per_fat == 0)
    error (1, 0, "Number of sectors per fat is zero");

  nr_of_root_dir_sectors = ((read_word (sblock->nr_of_root_dirents) *
			    FAT_DIR_REC_LEN) - 1) / bytes_per_sector + 1;

  first_root_dir_byte = (read_word (sblock->reserved_sectors)
    + (sblock->nr_of_fat_tables * sectors_per_fat)) << log2_bytes_per_sector;
  first_data_sector = (first_root_dir_byte >> log2_bytes_per_sector)
    + nr_of_root_dir_sectors;
  first_data_byte = first_data_sector << log2_bytes_per_sector;

  nr_of_clusters = (total_sectors - first_data_sector) / sectors_per_cluster;

  if (nr_of_clusters < FAT12_MAX_NR_OF_CLUSTERS)
    fat_type = FAT12;
  else
    {
      if (nr_of_clusters < FAT16_MAX_NR_OF_CLUSTERS)
	fat_type = FAT16;
      else
	fat_type = FAT32;
    }
  
  if (fat_type == FAT32 && read_word (sblock->compat.fat32.fs_version) != 0)
    error (1, 0, "Incompatible file system version");

  first_fat_sector = 0;
  if (fat_type == FAT32 && read_word (sblock->compat.fat32.extension_flags) & 1<<7)
    {
      first_fat_sector = (read_word (sblock->compat.fat32.extension_flags) & 0x0f);
      if (first_fat_sector > sblock->nr_of_fat_tables)
	error (1, 0, "Active FAT table does not exist");
      first_fat_sector *= sectors_per_fat;
    }
  first_fat_sector += read_word (sblock->reserved_sectors);
}


/* Write NEXT_CLUSTER in the FAT at position CLUSTER.
   You must call this from inside diskfs_catch_exception.
   Returns 0 (always succeeds).  */
error_t
fat_write_next_cluster(cluster_t cluster, cluster_t next_cluster)
{
  loff_t fat_entry_offset;
  cluster_t data;

  /* First data cluster is cluster 2.  */
  assert_backtrace (cluster >= 2 && cluster < nr_of_clusters + 2); 

  switch (fat_type)
    {
    case FAT12:
      if (next_cluster == FAT_BAD_CLUSTER)
	next_cluster = FAT12_BAD_CLUSTER;
      else if (next_cluster == FAT_EOC)
	next_cluster = FAT12_EOC;

      fat_entry_offset = (cluster * 3) / 2;
      data = read_word (fat_image + fat_entry_offset);
      if (cluster & 1)
	data = (data & 0xf) | ((next_cluster & 0xfff) << 4);
      else
	data = (data & 0xf000) | (next_cluster & 0xfff);

      write_word (fat_image + fat_entry_offset, data);
      break;

    case FAT16:
      if (next_cluster == FAT_BAD_CLUSTER)
	next_cluster = FAT16_BAD_CLUSTER;
      else if (next_cluster == FAT_EOC)
	next_cluster = FAT16_EOC;

      fat_entry_offset = cluster * 2;
      write_word (fat_image + fat_entry_offset, next_cluster);
      break;

    case FAT32:
    default:                             /* To silence gcc warning.  */
      if (next_cluster == FAT_BAD_CLUSTER)
	next_cluster = FAT32_BAD_CLUSTER;
      else if (next_cluster == FAT_EOC)
	next_cluster = FAT32_EOC;

      fat_entry_offset = cluster * 4;
      write_dword (fat_image + fat_entry_offset, next_cluster & 0x0fffffff);
    }

  return 0;
}

/* Read the FAT entry at position CLUSTER into NEXT_CLUSTER.
   You must call this from inside diskfs_catch_exception.
   Returns 0 (always succeeds).  */
error_t
fat_get_next_cluster(cluster_t cluster, cluster_t *next_cluster)
{
  loff_t fat_entry_offset;

  /* First data cluster is cluster 2.  */
  assert_backtrace (cluster >= 2 && cluster < nr_of_clusters + 2); 

  switch (fat_type)
    {
    case FAT12:
      fat_entry_offset = (cluster * 3) / 2;
      *next_cluster = read_word (fat_image + fat_entry_offset);
      if (cluster & 1)
	*next_cluster = *next_cluster >> 4;
      else
	*next_cluster &= 0xfff;

      if (*next_cluster == FAT12_BAD_CLUSTER)
	*next_cluster = FAT_BAD_CLUSTER;
      else if (*next_cluster >= FAT12_EOC)
	*next_cluster = FAT_EOC;
      break;

    case FAT16:
      fat_entry_offset = cluster * 2;
      *next_cluster = read_word (fat_image + fat_entry_offset);
      if (*next_cluster == FAT16_BAD_CLUSTER)
	*next_cluster = FAT_BAD_CLUSTER;
      else if (*next_cluster >= FAT16_EOC)
	*next_cluster = FAT_EOC;
      break;

    case FAT32:
    default:                             /* To silence gcc warning.  */
      fat_entry_offset = cluster * 4;
      *next_cluster = read_dword (fat_image + fat_entry_offset);
      *next_cluster &= 0x0fffffff;
      if (*next_cluster == FAT32_BAD_CLUSTER)
	*next_cluster = FAT_BAD_CLUSTER;
      else if (*next_cluster >= FAT32_EOC)
	*next_cluster = FAT_EOC;
    }

  return 0;
}

/* Allocate a new cluster, write CONTENT into the FAT at this new
   clusters position.  At success, 0 is returned and CLUSTER contains
   the cluster number allocated.  Otherwise, ENOSPC is returned if the
   filesystem is full.
   You must call this from inside diskfs_catch_exception.  */
error_t
fat_allocate_cluster (cluster_t content, cluster_t *cluster)
{
  error_t err = 0;
  cluster_t old_next_free_cluster;
  int wrapped = 0;
  cluster_t found_cluster = FAT_FREE_CLUSTER;

  assert_backtrace (content != FAT_FREE_CLUSTER);

  pthread_spin_lock (&allocate_free_cluster_lock);
  old_next_free_cluster = next_free_cluster;

  /* Loop over all clusters, starting from next_free_cluster and
     wrapping if reaching the end of the FAT, until we either find an
     unallocated cluster, or we have to give up because all clusters
     are allocated.  */
  do
    {
      cluster_t next_free_content;

      fat_get_next_cluster (next_free_cluster, &next_free_content);

      if (next_free_content == FAT_FREE_CLUSTER)
	found_cluster = next_free_cluster;

      if (++next_free_cluster == nr_of_clusters + 2)
	{
	  next_free_cluster = 2;
	  wrapped = 1;
	}
    }
  while (found_cluster == FAT_FREE_CLUSTER
	 && !(wrapped && next_free_cluster == old_next_free_cluster));

  if (found_cluster != FAT_FREE_CLUSTER)
    {
      *cluster = found_cluster;
      fat_write_next_cluster(found_cluster, content);
    }
  else 
    err = ENOSPC;

  pthread_spin_unlock (&allocate_free_cluster_lock);
  return err;
}

/* Extend the cluster chain to maximum size or new_last_cluster,
   whatever is less. If we reach the end of the file, and CREATE is
   true, allocate new blocks until there is either no space on the
   device or new_last_cluster are allocated.  (new_last_cluster: 0 is
   the first cluster of the file).  */
error_t
fat_extend_chain (struct node *node, cluster_t new_last_cluster, int create)
{
  error_t err = 0;
  struct disknode *dn = node->dn;
  struct cluster_chain *table;
  int offs;
  cluster_t left, prev_cluster, cluster;

  error_t allocate_new_table(struct cluster_chain **table)
    {
      struct cluster_chain *t;

      t = *table;
      *table = malloc (sizeof (struct cluster_chain));
      if (!*table)
	return ENOMEM;
      (*table)->next = 0;
      if (t)
	dn->last = t->next = *table;
      else
	dn->last = dn->first = *table;
      return 0;
    }
  
  pthread_spin_lock (&dn->chain_extension_lock);

  /* If we already have what we need, or we have all clusters that are
     available without allocating new ones, go out.  */
  if (new_last_cluster < dn->length_of_chain
      || (!create && dn->chain_complete))
    {
      pthread_spin_unlock (&dn->chain_extension_lock);
      return 0;
    }

  left = new_last_cluster + 1 - dn->length_of_chain;

  table = dn->last;
  if (table)
    {
      offs = (dn->length_of_chain - 1) & (CLUSTERS_PER_TABLE - 1);
      prev_cluster = table->cluster[offs];
    }
  else
    {
      offs = CLUSTERS_PER_TABLE - 1;
      prev_cluster = FAT_FREE_CLUSTER;
    }

   while (left)
     {
       if (dn->chain_complete)
	 {
	   err = fat_allocate_cluster(FAT_EOC, &cluster);
	   if (err)
	     break;
	   if (prev_cluster)
	     fat_write_next_cluster(prev_cluster, cluster);
	   else
	     /* XXX: Also write this to dirent structure!  */
	     dn->start_cluster = cluster;
	 }
       else
	 {
	   if (prev_cluster != FAT_FREE_CLUSTER)
	     err = fat_get_next_cluster(prev_cluster, &cluster);
	   else
	     cluster = dn->start_cluster;
	   if (cluster == FAT_EOC || cluster == FAT_FREE_CLUSTER)
	     {
	       dn->chain_complete = 1;
	       if (create)
		 continue;
	       else
		 break;
	     }
	 }
       prev_cluster = cluster;
       offs++;
       if (offs == CLUSTERS_PER_TABLE)
	 {
	   offs = 0;
	   err = allocate_new_table(&table);
	   if (err)
	     break;
	 }
       table->cluster[offs] = cluster;
       dn->length_of_chain++;
       left--;
     }

   if (dn->length_of_chain << log2_bytes_per_cluster > node->allocsize)
     node->allocsize = dn->length_of_chain << log2_bytes_per_cluster;

   pthread_spin_unlock (&dn->chain_extension_lock);
   return err;
}
   
/* Returns in DISK_CLUSTER the disk cluster corresponding to cluster
   CLUSTER in NODE.  If there is no such cluster yet, but CREATE is
   true, then it is created, otherwise EINVAL is returned.  */
error_t
fat_getcluster (struct node *node, cluster_t cluster, int create,
		cluster_t *disk_cluster)
{
  error_t err = 0;
  cluster_t chains_to_go = cluster >> LOG2_CLUSTERS_PER_TABLE;
  cluster_t offs = cluster & (CLUSTERS_PER_TABLE - 1);
  struct cluster_chain *chain;

  if (cluster >= node->dn->length_of_chain)
    {
      err = fat_extend_chain (node, cluster, create);
      if (err)
	return err;
      if (cluster >= node->dn->length_of_chain)
	{
	  assert_backtrace (!create);
	  return EINVAL;
	}
    }
  chain = node->dn->first;
  while (chains_to_go--)
    {
      assert_backtrace (chain);
      chain = chain->next;
    }
  assert_backtrace (chain);
  *disk_cluster = chain->cluster[offs];
  return 0;
}

void
fat_truncate_node (struct node *node, cluster_t clusters_to_keep)
{
  struct cluster_chain *next;
  cluster_t count;
  cluster_t offs;
  cluster_t pos;

  /* The root dir of a FAT12/16 fs is of fixed size, while the root
     dir of a FAT32 fs must never decease to exist.  */
  assert_backtrace (! (((fat_type == FAT12 || fat_type == FAT16) && node == diskfs_root_node)
	     || (fat_type == FAT32 && node == diskfs_root_node && clusters_to_keep == 0)));

  /* Expand the cluster chain, because we have to know the complete tail.  */
  fat_extend_chain (node, FAT_EOC, 0);
  if (clusters_to_keep == node->dn->length_of_chain)
    return;
  assert_backtrace (clusters_to_keep < node->dn->length_of_chain);

  /* Truncation happens here.  */
  next = node->dn->first;
  if (clusters_to_keep == 0)
    {
      /* Deallocate the complete file.  */
      node->dn->start_cluster = 0;
      pos = count = offs = 0;
      node->dn->last = 0;
    }
  else
    {
      count = (clusters_to_keep - 1) >> LOG2_CLUSTERS_PER_TABLE;
      offs = (clusters_to_keep - 1) & (CLUSTERS_PER_TABLE - 1);
      while (count-- > 0)
	{
	  assert_backtrace (next);

	  /* This cluster is now the last cluster in the chain.  */
	  if (count == 0)
	    node->dn->last = next;

	  next = next->next;
	}
      assert_backtrace (next);
      fat_write_next_cluster (next->cluster[offs++], FAT_EOC);
      pos = clusters_to_keep;
    }

  /* Purge dangling clusters. If we die here, scandisk will have to
     clean up the remains.  */
  while (pos < node->dn->length_of_chain)
    {
      if (offs == CLUSTERS_PER_TABLE)
	{
	  offs = 0;
	  next = next->next;
	  assert_backtrace (next);
	}
      fat_write_next_cluster(next->cluster[offs++], 0);
      pos++;
    }
 
  /* Free now unused tables.  (Could be done in one run with the above.)  */
  next = node->dn->first;
  if (clusters_to_keep != 0)
    {
      count = (clusters_to_keep - 1) >> LOG2_CLUSTERS_PER_TABLE;
      offs = (clusters_to_keep - 1) & (CLUSTERS_PER_TABLE - 1);
      while (count-- > 0)
	{
	  assert_backtrace (next);
	  next = next->next;
	}
      assert_backtrace (next);
      next = next->next;
    }
  while (next)
    {
      struct cluster_chain *next_next = next->next;
      free (next);
      next = next_next;
    }

  if (clusters_to_keep == 0)
    node->dn->first = 0;
  
  node->dn->length_of_chain = clusters_to_keep; 
}


/* Count the number of free clusters in the FAT.  */
int
fat_get_freespace (void)
{
  int free_clusters = 0;
  cluster_t curr_cluster;
  cluster_t next_cluster;
  error_t err;

  err = diskfs_catch_exception ();
  if (!err)
    {
      /* First cluster is the 3rd entry in the FAT table.  */
      for (curr_cluster = 2; curr_cluster < nr_of_clusters + 2;
	   curr_cluster++)
	{
	  fat_get_next_cluster (curr_cluster, &next_cluster);
	  if (next_cluster == FAT_FREE_CLUSTER)
	    free_clusters++;
	}
    }
  diskfs_end_catch_exception ();

  return free_clusters;
}


/* FILE must be a buffer with 13 characters.  */
void fat_to_unix_filename(const char *name, char *file)
{
  int npos;
  int fpos = 0;
  int ext = 0;

  for (npos = 0; npos < 11; npos++)
    {
      if (name[npos] == ' ')
	{
	  if (ext)
	    {
	      break;
	    }
	  else
	    {
	      file[fpos] = '.';
	      fpos++;
	      ext = 1;
	      while (npos < 7 && name[npos+1] == ' ') npos++;
	    }
	}
      else
	{
	  file[fpos] = name[npos];
	  fpos++;
	  if (npos == 7)
	    {
	      file[fpos] = '.';
	      fpos++;
	      ext = 1;
	    }
	}
    }
  if (ext && file[fpos-1] == '.')
    file[fpos-1] = '\0';
  else
    file[fpos] = '\0';
}

void
fat_from_unix_filename(char *fn, const char *un, int ul)
{
  int fp = 0;
  int up = 0;
  int ext = 0;

  while (fp < 11)
    {
      if (up == ul)
	{
	  /* We parsed the complete unix filename.  */
	  while (fp < 11)
	    fn[fp++] = ' ';
	}
      else
	{
	  if (!ext)
	    {
	      if (un[up] == '.')
		{
		  while (fp < 8)
		    fn[fp++] = ' ';
		  ext = 1;
		  un++;
		}
	      else if (fp == 8)
		{
		  while (un[up++] != '.' && up < ul);
		  ext = 1;
		}
	      else
		  fn[fp++] = toupper(un[ul++]);
	    }
	  else
	    {
	      if (un[up] == '.')
		{
		  while (fp < 11)
		    fn[fp++] = ' ';
		}
	      else
		fn[fp++] = toupper(un[up++]);
	    }
	}
    }
}


/* Return Epoch-based time from a MSDOS time/date pair.  */
void
fat_to_epoch (unsigned char *date, unsigned char *time, struct timespec *ts)
{
  struct tm tm;

  /* Date format:
     Bits 0-4: Day of month (1-31).
     Bits 5-8: Month of year (1-12).
     Bits 9-15: Count of years from 1980 (0-127).

     Time format:
     Bits 0-4: 2-second count (0-29).
     Bits 5-10: Minutes (0-59).
     Bits 11-15: Hours (0-23).
  */

  tm.tm_year = (read_word (date) >> 9) + 80;
  tm.tm_mon = ((read_word (date) & 0x1ff) >> 5) - 1;
  tm.tm_mday = read_word (date) & 0x1f;
  tm.tm_hour = (read_word (time) >> 11);
  tm.tm_min = (read_word (time) & 0x7ff) >> 5;
  tm.tm_sec = read_word (time) & 0x1f;
  tm.tm_isdst = 0;

  ts->tv_sec = timegm (&tm);
  ts->tv_nsec = 0;
}

/* Return MSDOS time/date pair from Epoch-based time.  */
void
fat_from_epoch (unsigned char *date, unsigned char *time, time_t *tp)
{
  struct tm *tm;

  pthread_spin_lock (&epoch_to_time_lock);
  tm = gmtime (tp);

  /* Date format:
     Bits 0-4: Day of month (1-31).
     Bits 5-8: Month of year (1-12).
     Bits 9-15: Count of years from 1980 (0-127).

     Time format:
     Bits 0-4: 2-second count (0-29).
     Bits 5-10: Minutes (0-59).
     Bits 11-15: Hours (0-23).
  */

  write_word(date, tm->tm_mday | ((tm->tm_mon + 1) << 5)
	     | ((tm->tm_year - 80) << 9));
  write_word(time, (tm->tm_hour << 11) | (tm->tm_min << 5)
	     | (tm->tm_sec >> 1));
  pthread_spin_unlock (&epoch_to_time_lock);
}
