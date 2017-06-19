/* Some ps_filters to restrict proc_stat_lists in various ways.

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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

#include <hurd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert-backtrace.h>
#include <pwd.h>
#include <unistd.h>

#include "ps.h"
#include "common.h"

/* ---------------------------------------------------------------- */

static int 
ps_own_p (struct proc_stat *ps)
{
  static int own_uid = -2;	/* -1 means no uid at all.  */
  if (own_uid == -2)
    own_uid = getuid ();
  return own_uid >= 0 && own_uid == proc_stat_owner_uid (ps);
}
const struct ps_filter ps_own_filter =
{"own", PSTAT_OWNER_UID, ps_own_p};

static int 
ps_not_leader_p (struct proc_stat *ps)
{
  return
    !(proc_stat_state (ps) & (PSTAT_STATE_P_SESSLDR | PSTAT_STATE_P_LOGINLDR));
}
const struct ps_filter ps_not_leader_filter =
{"not-sess-leader", PSTAT_STATE, ps_not_leader_p};

static int 
ps_unorphaned_p (struct proc_stat *ps)
{
  int state = proc_stat_state (ps);
  return
    !(state & PSTAT_STATE_P_ORPHAN)
      || (state & (PSTAT_STATE_P_SESSLDR | PSTAT_STATE_P_LOGINLDR));
}
const struct ps_filter ps_unorphaned_filter =
{"unorphaned", PSTAT_STATE, ps_unorphaned_p};

static int 
ps_ctty_p (struct proc_stat *ps)
{
  return proc_stat_cttyid (ps) != MACH_PORT_NULL;
}
const struct ps_filter ps_ctty_filter =
{"ctty", PSTAT_CTTYID, ps_ctty_p};

static int 
ps_parent_p (struct proc_stat *ps)
{
  return !(proc_stat_state (ps) & PSTAT_STATE_P_NOPARENT);
}
const struct ps_filter ps_parent_filter =
{"parent", PSTAT_STATE, ps_parent_p};

static int
ps_alive_p (struct proc_stat *ps)
{
  ps_flags_t test_flag =
    proc_stat_is_thread (ps) ? PSTAT_THREAD_BASIC : PSTAT_PROC_INFO;
  if (proc_stat_has (ps, test_flag))
    return 1;
  proc_stat_set_flags (ps, test_flag);
  return proc_stat_has (ps, test_flag);
}
const struct ps_filter ps_alive_filter =
{"alive", 0, ps_alive_p};
