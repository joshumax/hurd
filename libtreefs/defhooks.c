/* Default functions for the hook vector

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "treefs.h"
#include "treefs-s-hooks.h"

typedef void (*vf)();

static error_t unsupp () { return EOPNOTSUPP; }
static error_t nop () { return 0; }
static int true () { return 1; }

/* Most hooks return an error_t, so the default for anything not mentioned in
   this array is to return EOPNOTSUPP.  Any hooks returning different types,
   or with some different default behavior should be mentioned here.  */
treefs_hook_vector_init_t treefs_default_hooks =
{
  /* directory rpcs */
  [TREEFS_HOOK_S_DIR_LOOKUP] = (vf)_treefs_s_dir_lookup,

  [TREEFS_HOOK_S_FSYS_GETROOT] = (vf)_treefs_s_fsys_getroot,
  [TREEFS_HOOK_S_FSYS_SYNCFS] = (vf)nop,

  /* Non-rpc fsys hooks */
  [TREEFS_HOOK_FSYS_CREATE_NODE] = (vf)_treefs_fsys_create_node,
  [TREEFS_HOOK_FSYS_DESTROY_NODE] = (vf)_treefs_fsys_destroy_node,
  [TREEFS_HOOK_FSYS_GET_ROOT] = (vf)_treefs_fsys_get_root,

  /* Node hooks */
  [TREEFS_HOOK_NODE_TYPE] = (vf)_treefs_node_type,
  [TREEFS_HOOK_NODE_UNLINKED] = (vf)true,
  [TREEFS_HOOK_NODE_MOD_LINK_COUNT] = (vf)_treefs_node_mod_link_count,
  [TREEFS_HOOK_DIR_LOOKUP] = (vf)_treefs_dir_lookup,
  [TREEFS_HOOK_DIR_NOENT] = (vf)_treefs_dir_noent,
  [TREEFS_HOOK_DIR_CREATE_CHILD] = (vf)_treefs_dir_create_child,
  [TREEFS_HOOK_DIR_LINK] = (vf)_treefs_dir_link,
  [TREEFS_HOOK_DIR_UNLINK] = (vf)_treefs_dir_unlink,
  [TREEFS_HOOK_NODE_OWNED] = (vf)_treefs_node_owned,
  [TREEFS_HOOK_NODE_ACCESS] = (vf)_treefs_node_access,
  [TREEFS_HOOK_NODE_START_TRANSLATOR] = (vf)_treefs_node_start_translator,
  [TREEFS_HOOK_NODE_INIT] = (vf)nop,
  [TREEFS_HOOK_DIR_INIT] = (vf)nop,
  [TREEFS_HOOK_NODE_INIT_PEROPEN] = (vf)nop,
  [TREEFS_HOOK_NODE_INIT_HANDLE] = (vf)nop,
  [TREEFS_HOOK_NODE_FINALIZE] = (vf)nop,
  [TREEFS_HOOK_NODE_FINALIZE_PEROPEN] = (vf)nop,
  [TREEFS_HOOK_NODE_FINALIZE_HANDLE] = (vf)nop,

  /* Reference counting support */
  [TREEFS_HOOK_NODE_NEW_REFS] = (vf)nop,
  [TREEFS_HOOK_NODE_LOST_REFS] = (vf)nop,
  [TREEFS_HOOK_NODE_TRY_DROPPING_WEAK_REFS] = (vf)nop,
};

void _treefs_init_defhooks()
{
  int i;
  for (i = 0; i < TREEFS_NUM_HOOKS; i++)
    if (!treefs_default_hooks[i])
      treefs_default_hooks[i] = (vf)unsupp;
}
