/* Host name canonicalization

   Copyright (C) 1995, 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   [This file is from sh-utils/lib; maybe something can be done to share them.]

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Returns the canonical hostname associated with HOST (allocated in a static
   buffer), or 0 if it can't be determined.  */
char *
canon_host (char *host)
{
  struct hostent *he = gethostbyname (host);

  if (he)
    {
      char *addr = 0;

      /* Try and get an ascii version of the numeric host address.  */
      switch (he->h_addrtype)
	{
	case AF_INET:
	  addr = inet_ntoa (*(struct in_addr *)he->h_addr);
	  break;
	}

      if (addr && strcmp (he->h_name, addr) == 0)
	/* gethostbyname() cheated!  Lookup the host name via the address
	   this time to get the actual host name.  */
	he = gethostbyaddr (he->h_addr, he->h_length, he->h_addrtype);

      if (he)
	return he->h_name;
    }

  return 0;
}
