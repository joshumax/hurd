/* Hurd version calculation
   Copyright (C) 1994 Free Software Foundation

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

/* A string identifying this release of the GNU Hurd. */
#define HURD_VERSION "0.0"

/* The versions of all the programs that go into making up this
   version.  These are *not* computed by including files local
   to those programs to ensure that they are permanently linked
   to the definition of HURD_VERNSION above. */

struct 
{
  char *name;
  char *version
} hurd_server_versions =
{
  "auth", "0.0",
  "exec", "0.0",
  "init", "0.0",
  "proc", "0.0",
  "ufs", "0.0",
};

  
