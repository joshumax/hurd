/*
 *  linux/fs/ext2/bitmap.c (etc
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

static int nibblemap[] = {4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0};

unsigned long count_free (char * map, unsigned int numchars)
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

static int ffz_nibble_map[] = {0, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};

inline unsigned long ffz(unsigned long word)
{
  int offset = 0;
  if ((word & 0xFFFF) == 0xFFFF)
    {
      word >>= 16;
      offset += 16;
    }
  if ((word & 0xFF) == 0xFF)
    {
      word >>= 8;
      offset += 8;
    }
  if ((word & 0xF) == 0xF)
    {
      word >>= 4;
      offset += 4;
    }
  return ffz_nibble_map[word] + offset;
}

/* ---------------------------------------------------------------- */

/*
 * Copyright 1994, David S. Miller (davem@caip.rutgers.edu).
 */

/* find_next_zero_bit() finds the first zero bit in a bit string of length
 * 'size' bits, starting the search at bit 'offset'. This is largely based
 * on Linus's ALPHA routines, which are pretty portable BTW.
 */

inline unsigned long
find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
  unsigned long *p = ((unsigned long *) addr) + (offset >> 6);
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
  while (size & ~32UL) 
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
found_middle:
  return result + ffz(tmp);
}

/* Linus sez that gcc can optimize the following correctly, we'll see if this
 * holds on the Sparc as it does for the ALPHA.
 */

inline int
find_first_zero_bit(void *buf, unsigned len)
{
  return find_next_zero_bit(buf, len, 0);
}

/* ---------------------------------------------------------------- */

/* Returns a pointer to the first occurence of CH in the buffer BUF of len
   LEN, or BUF + LEN if CH doesn't occur.  */
void *memscan(void *buf, unsigned char ch, unsigned len)
{
  unsigned char *p = (unsigned char *)buf;
  while (len-- > 0)
    if (*p == ch)
      break;
    else
      p++;
  return (void *)p;
}
