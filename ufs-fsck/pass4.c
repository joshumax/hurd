/* Pass 4 of GNU fsck -- Check reference counts
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

pass4()
{
  ino_t number;
  
  for (number = ROOTINO; number < lastino; number++)
    {
      /* If it's correct, then there's nothing to do */
      if (linkcount[number] == linkfound[number]
	  && (!!(inodestate[number] == UNALLOC) == !linkfound[number]))

      if (linkfound[number] && inodestate[number] != UNALLOC)
	{
	  if (linkcount[number] != linkfound[number])
	    {
	      struct dinode dino;
	      getinode (number, &dino);
	      pwarn ("LINK COUNT %s", 
		     (DI_MODE (dp) & IFMT) == IFDIR ? "DIR" : "FILE");
	      pinode (dino);
	      printf (" COUNT %d SHOULD BE %d", linkcount[number],
		      linkfound[number]);
	      if (preen)
		printf (" (ADJUSTED)");
	      if (preen || reply ("ADJUST"))
		{
		  dino.di_nlink = linkfound[number];
		  write_inode (number, &dino);
		}
	    }
	}
      else if (inodestate[number
	{
	  
		  
		  
