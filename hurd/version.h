/* Hurd version calculation
   Copyright (C) 1994, 1996 Free Software Foundation

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

#ifndef _HURD_VERSION_H
#define _HURD_VERSION_H

/* Each version of the Hurd defines a set of versions for the servers
   that make it up.  Each server is identified by one of these
   structures.  */
struct hurd_server_version
{
  char *name;			/* The name of the server.  */
  char *version;		/* The server's version.  */
};

/* A version of the Hurd is therefore defined by its version number
   (which is incremented for each distribution) and its release number
   (which refers to the interfaces in the hurd directory, and is found
   in <hurd/hurd_types.h> as HURD_RELEASE) and by a list of hurd_vers
   structures defining servers that were distributed with that
   version. */
struct hurd_version
{
  char *hurdrelease;
  char *hurdversion;
  int nservers;
  struct hurd_server_version vers[0];
};

#endif	/* hurd/version.h */


/* And these are the standard lists referred to above. */
#ifdef HURD_VERSION_DEFINE

struct hurd_version hurd_versions[] = 
{
  {
    /* Hurd version 0.0 pre-alpha; frozen May 3, 1996. */
    "0.0 pre-alpha",
    "0.0 pre-alpha",
    5,
    {
      {"auth", "0.0 pre-alpha"},
      {"proc", "0.0 pre-alpha"},
      {"ufs", "0.0 pre-alpha"},
      {"init", "0.0 pre-alpha"},
      {"exec", "0.0 pre-alpha"},
    }
  },
  {
    /* Hurd version 0.0.  Not frozen yet. */
    "0.0",
    "0.0",
    5,
    {
      {"auth", "0.0"},
      {"proc", "0.0"},
      {"ufs", "0.0"},
      {"init", "0.0"},
      {"exec", "0.0"},
    }
  }
};

int nhurd_versions = sizeof hurd_versions / sizeof *hurd_versions;

#endif

      
