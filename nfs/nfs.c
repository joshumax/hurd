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


/* Round a number up to be a multiple of four. */
#define NFS_ROUNDUP(foo) ((foo + 3) & ~3)

/* Each of these functions copies its second arg to *P, converting it
   to XDR representation along the way.  They then the address after
   the copied value. */

static inline void *
xdr_encode_int (void *p, int foo)
{
  *(int *)p = htonl (foo);
  return p + sizeof (int);
}

static inline void *
xdr_encode_fhandle (void *p, void *fhandle)
{
  bcopy (fhandle, p, NFSV2_FHSIZE);
  return p + NFSV2_FHSIZE;
}

static inline void *
xdr_encode_string (void *p, char *string)
{
  int len = strlen (string);

  /* length */
  p = xdr_encode_int (p, len);

  /* Fill in string */
  bcopy (string, p, len);
  p += len;
  
  /* Zero extra */
  if (NFS_ROUNDUP (len) > len)
    {
      bzero (p, NFS_ROUNDUP (len) - len);
      p += NFS_ROUNDUP (len) - len;
    }
  
  return p;
}

      
