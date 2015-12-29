/* Bitmap perusing routines

   Copyright (C) 1995 Free Software Foundation, Inc.

   Converted to work under the hurd by Miles Bader <miles@gnu.ai.mit.edu>

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

#define ffz(word)	(ffs (~(unsigned int) (word)) - 1)

/*
 *  linux/fs/ext2/bitmap.c (&c)
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

static int nibblemap[] = {4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0};

static inline
unsigned long count_free (unsigned char *map, unsigned int numchars)
{
	unsigned int i;
	unsigned long sum = 0;

	if (!map)
		return (0);
	for (i = 0; i < numchars; i++)
		sum += nibblemap[map[i] & 0xf] +
			nibblemap[(map[i] >> 4) & 0xf];
	return (sum);
}

/* ---------------------------------------------------------------- */

/*
 * Copyright 1994, David S. Miller (davem@caip.rutgers.edu).
 */

/* find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */

static inline unsigned long
find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
  unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
  unsigned long result = offset & ~31UL;
  unsigned long tmp;

  if (offset >= size)
    return size;
  size -= result;
  offset &= 31UL;
  if (offset)
    {
      tmp = *(p++);
      tmp |= ~0UL >> (32-offset);
      if (size < 32)
	goto found_first;
      if (~tmp)
	goto found_middle;
      size -= 32;
      result += 32;
    }
  while (size & ~31UL)
    {
      if (~(tmp = *(p++)))
	goto found_middle;
      result += 32;
      size -= 32;
    }
  if (!size)
    return result;
  tmp = *p;

found_first:
  tmp |= ~0UL << size;
  if (!~tmp)
    return result + size;
found_middle:
  return result + ffz(tmp);
}

/* Linus sez that gcc can optimize the following correctly, we'll see if this
 * holds on the Sparc as it does for the ALPHA.
 */

static inline int
find_first_zero_bit(void *buf, unsigned len)
{
  return find_next_zero_bit(buf, len, 0);
}
