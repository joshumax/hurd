/* gzip decompression

   Copyright (C) 2014 Free Software Foundation, Inc.
   Written by Ignazio Sgalmuzzo <ignaker@gmail.com>

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <zlib.h>

/* I/O interface */
extern int (*unzip_read) (char *buf, size_t maxread);
extern void (*unzip_write) (const char *buf, size_t nwrite);
extern void (*unzip_read_error) (void);
extern void (*unzip_error) (const char *msg);

#define INBUFSIZ	0x1000
#define OUTBUFSIZ	0x1000

static unsigned char inbuf[INBUFSIZ];
static unsigned char outbuf[OUTBUFSIZ];

void
do_gunzip (void)
{
  int result;
  z_stream strm;

  strm.zalloc = NULL;
  strm.zfree = NULL;
  strm.opaque = NULL;

  strm.avail_in  = 0;
  strm.next_out = outbuf;
  strm.avail_out = OUTBUFSIZ;

  result = inflateInit2 (&strm, 32 + MAX_WBITS);

  while (result == Z_OK)
  {
    if (strm.avail_in == 0)
    {
      strm.next_in = inbuf;
      strm.avail_in  = (*unzip_read)((char*) strm.next_in, INBUFSIZ);

      if (strm.avail_in == 0)
        break;
    }

    result = inflate (&strm, Z_NO_FLUSH);

    if ((result != Z_OK) && (result != Z_STREAM_END))
      break;

    if ((strm.avail_out == 0) || (result == Z_STREAM_END))
    {
      (*unzip_write) ((char*) outbuf, OUTBUFSIZ - strm.avail_out);
      strm.next_out = outbuf;
      strm.avail_out = OUTBUFSIZ;
    }
  }

  inflateEnd (&strm);

  if (result != Z_STREAM_END)
    (*unzip_error) (NULL);
}
