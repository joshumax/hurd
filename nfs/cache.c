/* 
   Copyright (C) 1995 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

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


/* We have fresh stat information for NP; the fattr structure is at 
   P.  Update our entry.  Return the address of the next int after
   the fattr structure.  */
int *
register_fresh_stat (struct node *np, int *p)
{
  int *ret;
  
  ret = xdr_decode_fattr (p, &np->nn_stat);
  np->nn->stat_updat = netfs_mtime.seconds;
  return ret;
}


  

  
  
