/* Pass 4 of GNU fsck -- Check reference counts
   Copyright (C) 1994, 1996 Free Software Foundation, Inc.
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

#include "fsck.h"

void
pass4()
{
  ino_t number;
  /* True if any reconnect attempt failed, in which case we don't try again. */
  int reconn_failed = 0;
  
  for (number = ROOTINO; number < maxino; number++)
    {
      if (linkfound[number] && inodestate[number] != UNALLOC)
	{
	  if (linkcount[number] != linkfound[number])
	    {
	      pinode (0, number,
		      "LINK COUNT %d SHOULD BE %d IN",
		      linkcount[number], linkfound[number]);
	      if (preen || reply ("ADJUST"))
		{
		  struct dinode dino;
		  getinode (number, &dino);
		  dino.di_nlink = linkfound[number];
		  write_inode (number, &dino);
		  pfix ("ADJUSTED");
		}
	    }
	}
      else if (linkfound[number] && inodestate[number] == UNALLOC)
	{
	  /* This can't happen because we never count links to unallocated
	     nodes. */
	  errexit ("LINK RECORDED FOR UNALLOCATED NODE");
	}
      else if (!linkfound[number] && inodestate[number] != UNALLOC)
	{
	  /* No links to allocated node.  If the size is zero, then
	     we want to clear it; if the size is positive, then we
	     want to reattach in. */
	  struct dinode dino;

	  pinode (0, number, "UNREF");
	  
	  getinode (number, &dino);
	  if (dino.di_size && !reconn_failed)
	    {
	      /* This can't happen for dirctories because pass 3 should
		 already have reset them up.  */
	      if ((DI_MODE (&dino) & IFMT) == IFDIR)
		errexit ("NO LINKS TO NONZERO DIRECTORY");
	      
	      if (preen || reply ("RECONNECT"))
		reconn_failed = !linkup (number, -1);
	      if (! reconn_failed)
		pfix ("RECONNECTED");
	    }
	  if (dino.di_size == 0 || reconn_failed)
	    {
	      if (reconn_failed && !preen)
		/* If preening, the previous call to problem is still active
		   (more likely the failure was too severe, and exited).  */
		problem (0, "RECONNECT FAILED");
	      if (preen || reply ("CLEAR"))
		{
		  inodestate[number] = UNALLOC;
		  clear_inode (number, &dino);
		  pfix ("CLEARED");
		}
	    }
	}
    }
}      
