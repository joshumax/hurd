/* Some ps_filter_t's to restrict proc_stat_list's in various ways.

   Copyright (C) 1995 Free Software Foundation, Inc.

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
#include <assert.h>
#include <pwd.h>
#include <unistd.h>

#include "ps.h"
#include "common.h"

/* ---------------------------------------------------------------- */

static bool 
ps_own_p(proc_stat_t ps)
{
  static int own_uid = -1;
  if (own_uid < 0)
    own_uid = getuid();
  return own_uid == proc_stat_proc_info(ps)->owner;
}
struct ps_filter ps_own_filter =
{"own", PSTAT_PROC_INFO, ps_own_p};

static bool 
ps_not_sess_leader_p(proc_stat_t ps)
{
  return !(proc_stat_state(ps) & PSTAT_STATE_P_SESSLDR);
}
struct ps_filter ps_not_sess_leader_filter =
{"not-sess-leader", PSTAT_STATE, ps_not_sess_leader_p};

static bool 
ps_unorphaned_p(proc_stat_t ps)
{
  int state = proc_stat_state(ps);
  return !(state & PSTAT_STATE_P_ORPHAN) || (state & PSTAT_STATE_P_SESSLDR);
}
struct ps_filter ps_unorphaned_filter =
{"unorphaned", PSTAT_STATE, ps_unorphaned_p};

static bool 
ps_ctty_p(proc_stat_t ps)
{
  return proc_stat_cttyid(ps) != MACH_PORT_NULL;
}
struct ps_filter ps_ctty_filter =
{"ctty", PSTAT_CTTYID, ps_ctty_p};

static bool 
ps_parent_p(proc_stat_t ps)
{
  return !(proc_stat_state(ps) & PSTAT_STATE_P_NOPARENT);
}
struct ps_filter ps_parent_filter =
{"parent", PSTAT_STATE, ps_parent_p};
