/* Default version of Journal in libdiskfs.
   It implements default implementations of 5 functions:
     - diskfs_journal_start_transaction
     - diskfs_journal_stop_transaction
     - diskfs_journal_commit_transaction
     - diskfs_journal_needs_sync
     - diskfs_journal_set_sync

   diskfs_journal_start_transaction returns NULL,
   diskfs_journal_needs_sync returns 0.
   All others are empty and do nothing.

   Written by Milos Nikic.
   Copyright (C) 2026 Free Software Foundation, Inc.

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "diskfs.h"

/* Weak definitions.
   If the specific filesystem (ext2fs) provides these, the linker uses those.
   If not, it uses these empty ones. */

diskfs_transaction_t * __attribute__((weak))
diskfs_journal_start_transaction (void)
{
  /* Do nothing */
  return NULL;
}

void __attribute__((weak))
diskfs_journal_stop_transaction (diskfs_transaction_t *tx)
{
  /* Do nothing */
}

void __attribute__((weak))
diskfs_journal_commit_transaction (diskfs_transaction_t *tx)
{
  /* Do nothing */
}

void __attribute__((weak))
diskfs_journal_set_sync (diskfs_transaction_t *txn)
{
  /* Do nothing */
}

int __attribute__((weak))
diskfs_journal_needs_sync (diskfs_transaction_t *txn)
{
  /* Do nothing */
  return 0;
}
