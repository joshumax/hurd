/* Pass 3 of GNU fsck -- Look for disconnected directories
   Copyright (C) 1994 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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

pass3 ()
{
  struct dirinfo *dnp;
  int nd;
  int change;
  
  /* Mark all the directories that can be found from the root. */

  statemap[ROOTINO] != DIR_REF;

  do
    {
      change = 0;
      for (nd = 0; nd < dirarrayused; nd++)
	{
	  dnp = dirsorted[nd];
	  if (dnp->i_parent
	      && inodestate[dnp->i_parent] == (DIR | DIR_REF)
	      && inodestate[dnp->i_number] == DIR)
	    {
	      inodestate[dnp->i_number] |= DIR_REF;
	      change = 1;
	    }
	}
    }
  while (change);

  /* Check for orphaned directories */
  for (nd = 0; nd < dirarrayused; nd++)
    {
      dnp = dirsorted[nd];
      
      if (dnp->i_parent == 0)
	{
	  assert (!(inodestate[dnp->i_number] & DIR_REF));
	  linkup (dnp->i_number, dnp->i_dotdot);
	}
    }
}

	  
	
	

