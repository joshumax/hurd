/*
   Copyright (C) 2018 Free Software Foundation, Inc.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the GNU Hurd.  If not, see <<a rel="nofollow" href="http://www.gnu.org/licenses/">http://www.gnu.org/licenses/</a>>.
*/

/* ACPI Filesystem implementation */

#include <acpifs.h>

#include <error.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <hurd/netfs.h>

#include <ncache.h>
#include <func_files.h>

static error_t
create_dir_entry (char *name, struct acpi_table *t,
                 struct acpifs_dirent *parent, io_statbuf_t stat,
                 struct node *node, struct acpifs_dirent *entry)
{
  uint16_t parent_num_entries;

  strncpy (entry->name, name, NAME_SIZE);
  entry->acpitable = t;
  entry->parent = parent;
  entry->stat = stat;
  entry->dir = 0;
  entry->node = node;

  /* Update parent's child list */
  if (entry->parent)
    {
      if (!entry->parent->dir)
       {
         /* First child */
         entry->parent->dir = calloc (1, sizeof (struct acpifs_dir));
         if (!entry->parent->dir)
           return ENOMEM;
       }

      parent_num_entries = entry->parent->dir->num_entries++;
      entry->parent->dir->entries = realloc (entry->parent->dir->entries,
                                            entry->parent->dir->num_entries *
                                            sizeof (struct acpifs_dirent *));
      if (!entry->parent->dir->entries)
       return ENOMEM;
      entry->parent->dir->entries[parent_num_entries] = entry;
    }

  return 0;
}

error_t
alloc_file_system (struct acpifs **fs)
{
  *fs = calloc (1, sizeof (struct acpifs));
  if (!*fs)
    return ENOMEM;

  return 0;
}

error_t
init_file_system (file_t underlying_node, struct acpifs *fs)
{
  error_t err;
  struct node *np;
  io_statbuf_t underlying_node_stat;

  /* Initialize status from underlying node.  */
  err = io_stat (underlying_node, &underlying_node_stat);
  if (err)
    return err;

  np = netfs_make_node_alloc (sizeof (struct netnode));
  if (!np)
    return ENOMEM;
  np->nn_stat = underlying_node_stat;
  np->nn_stat.st_fsid = getpid ();
  np->nn_stat.st_mode =
    S_IFDIR | S_IROOT | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH |
    S_IXOTH;
  np->nn_translated = np->nn_stat.st_mode;

  /* Set times to now */
  fshelp_touch (&np->nn_stat, TOUCH_ATIME | TOUCH_MTIME | TOUCH_CTIME,
               acpifs_maptime);

  fs->entries = calloc (1, sizeof (struct acpifs_dirent));
  if (!fs->entries)
    {
      free (fs->entries);
      return ENOMEM;
    }

  /* Create the root entry */
  err = create_dir_entry ("", 0, 0, np->nn_stat, np, fs->entries);

  fs->num_entries = 1;
  fs->root = netfs_root_node = np;
  fs->root->nn->ln = fs->entries;
  pthread_mutex_init (&fs->node_cache_lock, 0);

  return 0;
}

error_t
create_fs_tree (struct acpifs *fs)
{
  error_t err = 0;
  int i;
  size_t nentries, ntables = 0;
  struct acpifs_dirent *e, *list, *parent;
  struct stat e_stat;
  char entry_name[NAME_SIZE];
  struct acpi_table *iter = NULL;

  /* Copy the root stat */
  e_stat = fs->entries->stat;

  err = acpi_get_num_tables(&ntables);
  if (err)
    return err;

  /* Allocate enough for / + "tables"/ + each table */
  nentries = ntables + 2;

  list = realloc (fs->entries, nentries * sizeof (struct acpifs_dirent));
  if (!list) {
    if (fs->entries)
      free(fs->entries);
    return ENOMEM;
  }

  e = list + 1;
  parent = list;
  e_stat.st_mode &= ~S_IROOT;   /* Remove the root mode */
  memset (entry_name, 0, NAME_SIZE);
  strncpy (entry_name, DIR_TABLES_NAME, NAME_SIZE);

  err = create_dir_entry (entry_name, 0, parent, e_stat, 0, e);
  if (err)
    return err;

  parent = e;

  /* Remove all permissions to others */
  e_stat.st_mode &= ~(S_IROTH | S_IWOTH | S_IXOTH);

  /* Change mode to a regular read-only file */
  e_stat.st_mode &= ~(S_IFDIR | S_IXUSR | S_IXGRP | S_IWUSR | S_IWGRP);
  e_stat.st_mode |= S_IFREG;

  /* Get all ACPI tables */
  err = acpi_get_tables(&iter);
  if (err)
    return err;

  for (i = 0; i < ntables; i++, iter++)
    {
      e_stat.st_size = iter->datalen;

      // Create ACPI table entry
      memset (entry_name, 0, NAME_SIZE);

      snprintf (entry_name, NAME_SIZE, "%c%c%c%c",
                iter->h.signature[0],
                iter->h.signature[1],
                iter->h.signature[2],
                iter->h.signature[3]);
      e++;
      err = create_dir_entry (entry_name, iter, parent, e_stat, 0, e);
      if (err)
       return err;
    }

  /* The root node points to the first element of the entry list */
  fs->entries = list;
  fs->num_entries = nentries;
  fs->root->nn->ln = fs->entries;

  return err;
}

error_t
entry_check_perms (struct iouser *user, struct acpifs_dirent *e, int flags)
{
  error_t err = 0;

  if (!err && (flags & O_READ))
    err = fshelp_access (&e->stat, S_IREAD, user);
  if (!err && (flags & O_WRITE))
    err = fshelp_access (&e->stat, S_IWRITE, user);
  if (!err && (flags & O_EXEC))
    err = fshelp_access (&e->stat, S_IEXEC, user);

  return err;
}

/* Set default permissions to the given entry */
static void
entry_default_perms (struct acpifs *fs, struct acpifs_dirent *e)
{
  /* Set default owner and group */
  UPDATE_OWNER (e, fs->root->nn->ln->stat.st_uid);
  UPDATE_GROUP (e, fs->root->nn->ln->stat.st_gid);

  /* Update ctime */
  UPDATE_TIMES (e, TOUCH_CTIME);

  return;
}

static void
entry_set_perms (struct acpifs *fs, struct acpifs_dirent *e)
{
  struct acpifs_perm *perm = &fs->perm;
  if (perm->uid >= 0)
    UPDATE_OWNER (e, perm->uid);
  if (perm->gid >= 0)
    UPDATE_GROUP (e, perm->gid);

  /* Update ctime */
  UPDATE_TIMES (e, TOUCH_CTIME);

  return;
}

/* Update all entries' permissions */
error_t
fs_set_permissions (struct acpifs *fs)
{
  int i;
  struct acpifs_dirent *e;

  for (i = 0, e = fs->entries; i < fs->num_entries; i++, e++)
    {
      /* Restore default perms, as this may be called from fsysopts */
      entry_default_perms (fs, e);

      /* Set new permissions, if any */
      entry_set_perms (fs, e);
    }

  return 0;
}
