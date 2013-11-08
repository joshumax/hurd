/* 
   Copyright (C) 1997 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG.

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

/* Extensions to ISO 9660 we support */

#include "isofs.h"

struct susp_field susp_extension[] =
{
  { 'C', 'E', 1, process_su_ce },
  { 'P', 'D', 1, process_su_pd },
  { 'S', 'P', 1, process_su_sp },
  { 'E', 'R', 1, process_su_er },
  { 'S', 'T', 1, process_su_st },
  { 0, 0, 0, 0 },
};

struct susp_field rr_extension[] =
{
  { 'P', 'X', 1, process_rr_px },
  { 'P', 'N', 1, process_rr_pn },
  { 'S', 'L', 1, process_rr_sl },
  { 'N', 'M', 1, process_rr_nm },
  { 'C', 'L', 1, process_rr_cl },
  { 'P', 'L', 1, process_rr_pl },
  { 'R', 'E', 1, process_rr_re },
  { 'T', 'F', 1, process_rr_tf },
  { 'S', 'F', 1, process_rr_sf },
  { 0, 0, 0, 0 },
};

struct susp_ext extensions[] =
{
  { "RRIP_1991A", 1,
    "THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX FILE SYSTEM SEMANTICS",
    "ROCK RIDGE SPECIFICATION VERSION 1 REVISION 1.10 JULY 13 1993",
    rr_extensions
  },
  { 0, 0, 0, 0, susp_extensions },
  { 0, 0, 0, 0, 0 },
}
