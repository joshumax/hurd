#ifndef _HACK_ASM_BITOPS_H
#define _HACK_ASM_BITOPS_H

/* We don't need atomicity in the Linux code because we serialize all
   entries to it.  */

#include <stdint.h>

#define BITOPS_WORD(nr, addr)	(((uint32_t *) (addr))[(nr) / 32])
#define BITOPS_MASK(nr)		(1 << ((nr) & 31))

static __inline__ void set_bit (int nr, void *addr)
{ BITOPS_WORD (nr, addr) |= BITOPS_MASK (nr); }

static __inline__ void clear_bit (int nr, void *addr)
{ BITOPS_WORD (nr, addr) &= ~BITOPS_MASK (nr); }

static __inline__ void change_bit (int nr, void *addr)
{ BITOPS_WORD (nr, addr) ^= BITOPS_MASK (nr); }

static __inline__ int test_bit (int nr, void *addr)
{ return BITOPS_WORD (nr, addr) & BITOPS_MASK (nr); }

static __inline__ int test_and_set_bit (int nr, void *addr)
{
  int res = BITOPS_WORD (nr, addr) & BITOPS_MASK (nr);
  BITOPS_WORD (nr, addr) |= BITOPS_MASK (nr);
  return res;
}

#define find_first_zero_bit #error loser
#define find_next_zero_bit #error loser

#define ffz(word)	(ffs (~(unsigned int) (word)) - 1)


#endif
