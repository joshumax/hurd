/* Directory management subroutines
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



/* This routine is used in pass 1 to initialize DIRARRAY and DIRSORTED.
   Copy information from DP (for number NUMBER) into a newly allocated
   dirinfo structure and add it to the arrays. */
record_directory (struct dinode *dp, ino_t number)
{
  u_int blks;
  struct dirinfo *dnp;
  
  blks = howmany (dp->di_size, sblock.fs_bsize);
  if (blks > NDADDR)
    blks = NDADDR * NIADDR;
  blks *= sizeof (daddr_t);
  dnp = malloc (sizeof (struct dirinfo) + blks);
  
  dnp->i_number = number;
  dnp->i_parent = dnp->i_dotdot = 0;
  dnp->i_isize = dnp->di_size;
  dnp->i_numblks = blks * sizeof (daddr_t);
  bcopy (dp->di_db, dnp->i_blks, blks);
  
  if (dirarrayused ==  dirarraysize)
    {
      if (dirarraysize == 0)
	{
	  dirarraysize = 100;
	  dirarray = malloc (dirarraysize * sizeof (struct dirinfo *));
	  dirsorted = malloc (dirarraysize * sizeof (struct dirinfo *));
	}
      else
	{
	  dirarraysize *= 2;
	  dirarray = realloc (dirarray,
			      dirarraysize * sizeof (struct dirinfo *));
	  dirsorted = realloc (dirsorted,
			       dirarraysize * sizeof (struct dirinfo *));
	}
    }
  dirarray[dirarrayused] = dnp;
  dirsorted[dirarrayused] = dnp;
  dirarrayused++;
}


