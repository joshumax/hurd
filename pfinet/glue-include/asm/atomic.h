#ifndef _HACK_ASM_ATOMIC_H
#define _HACK_ASM_ATOMIC_H

/* We don't need atomicity in the Linux code because we serialize all
   entries to it.  */

typedef struct { int counter; } atomic_t;

#define ATOMIC_INIT(i)	{ (i) }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v,i)		(((v)->counter) = (i))

static __inline__ void atomic_add(int i, atomic_t *v)	{ v->counter += i; }
static __inline__ void atomic_sub(int i, atomic_t *v)	{ v->counter -= i; }
static __inline__ void atomic_inc(atomic_t *v)		{ ++v->counter; }
static __inline__ void atomic_dec(atomic_t *v)		{ --v->counter; }
static __inline__ int atomic_dec_and_test(atomic_t *v)
{ return --v->counter == 0; }
static __inline__ int atomic_inc_and_test_greater_zero(atomic_t *v)
{ return ++v->counter > 0; }

#define atomic_clear_mask(mask, addr)			(*(addr) &= ~(mask))
#define atomic_set_mask(mask, addr)			(*(addr) |= (mask))


#endif
