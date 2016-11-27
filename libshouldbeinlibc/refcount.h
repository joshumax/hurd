/* Lock-less reference counting primitives

   Copyright (C) 2014 Free Software Foundation, Inc.

   Written by Justus Winter <4winter@informatik.uni-hamburg.de>

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
   along with the GNU Hurd.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef _HURD_REFCOUNT_H_
#define _HURD_REFCOUNT_H_

#ifdef REFCOUNT_DEFINE_EI
#define REFCOUNT_EI
#else
#define REFCOUNT_EI __extern_inline
#endif

#include <assert-backtrace.h>
#include <limits.h>
#include <stdint.h>

/* Simple reference counting.  */

/* An opaque type.  You must not access these values directly.  */
typedef unsigned int refcount_t;

/* Initialize REF with REFERENCES.  REFERENCES must not be zero.  */
REFCOUNT_EI void
refcount_init (refcount_t *ref, unsigned int references)
{
  assert_backtrace (references > 0 || !"references must not be zero!");
  *ref = references;
}

/* Increment REF.  Return the result of the operation.  This function
   uses atomic operations.  It is not required to serialize calls to
   this function.

   This is the unsafe version of refcount_ref.  refcount_ref also
   checks for use-after-free errors.  When in doubt, use that one
   instead.  */
REFCOUNT_EI unsigned int
refcount_unsafe_ref (refcount_t *ref)
{
  unsigned int r;
  r = __atomic_add_fetch (ref, 1, __ATOMIC_RELAXED);
  assert_backtrace (r != UINT_MAX || !"refcount overflowed!");
  return r;
}

/* Increment REF.  Return the result of the operation.  This function
   uses atomic operations.  It is not required to serialize calls to
   this function.  */
REFCOUNT_EI unsigned int
refcount_ref (refcount_t *ref)
{
  unsigned int r;
  r = refcount_unsafe_ref (ref);
  assert_backtrace (r != 1 || !"refcount detected use-after-free!");
  return r;
}

/* Decrement REF.  Return the result of the operation.  This function
   uses atomic operations.  It is not required to serialize calls to
   this function.  */
REFCOUNT_EI unsigned int
refcount_deref (refcount_t *ref)
{
  unsigned int r;
  r = __atomic_sub_fetch (ref, 1, __ATOMIC_RELAXED);
  assert_backtrace (r != UINT_MAX || !"refcount underflowed!");
  return r;
}

/* Return REF.  This function uses atomic operations.  It is not
   required to serialize calls to this function.  */
REFCOUNT_EI unsigned int
refcount_references (refcount_t *ref)
{
  return __atomic_load_n (ref, __ATOMIC_RELAXED);
}

/* Reference counting with weak references.  */

/* An opaque type.  You must not access these values directly.  */
typedef union _references refcounts_t;

/* Instead, the functions manipulating refcounts_t values write the
   results into this kind of objects.  */
struct references {
  /* We chose the layout of this struct so that when it is used in the
     union _references, the hard reference counts occupy the least
     significant bits.  We rely on this layout for atomic promotion
     and demotion of references.  See refcounts_promote and
     refcounts_demote for details.  */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define REFCOUNT_REFERENCES(_hard, _weak) \
    (struct references) { .hard = (_hard), .weak = (_weak) }
  uint32_t hard;
  uint32_t weak;
#else
#define REFCOUNT_REFERENCES(_hard, _weak) \
    (struct references) { .weak = (_weak), .hard = (_hard) }
  uint32_t weak;
  uint32_t hard;
#endif
};

/* We use a union to convert struct reference values to uint64_t which
   we can manipulate atomically.  While this behavior is not
   guaranteed by the C standard, it is supported by all major
   compilers.  */
union _references {
  struct references references;
  uint64_t value;
};

/* Initialize REF with HARD and WEAK references.  HARD and WEAK must
   not both be zero.  */
REFCOUNT_EI void
refcounts_init (refcounts_t *ref, uint32_t hard, uint32_t weak)
{
  assert_backtrace ((hard != 0 || weak != 0)
                    || !"references must not both be zero!");
  ref->references = REFCOUNT_REFERENCES (hard, weak);
}

/* Increment the hard reference count of REF.  If RESULT is not NULL,
   the result of the operation is written there.  This function uses
   atomic operations.  It is not required to serialize calls to this
   function.

   This is the unsafe version of refcounts_ref.  refcounts_ref also
   checks for use-after-free errors.  When in doubt, use that one
   instead.  */
REFCOUNT_EI void
refcounts_unsafe_ref (refcounts_t *ref, struct references *result)
{
  const union _references op = { .references = REFCOUNT_REFERENCES (1, 0) };
  union _references r;
  r.value = __atomic_add_fetch (&ref->value, op.value, __ATOMIC_RELAXED);
  assert_backtrace (r.references.hard != UINT32_MAX
                    || !"refcount overflowed!");
  if (result)
    *result = r.references;
}

/* Increment the hard reference count of REF.  If RESULT is not NULL,
   the result of the operation is written there.  This function uses
   atomic operations.  It is not required to serialize calls to this
   function.  */
REFCOUNT_EI void
refcounts_ref (refcounts_t *ref, struct references *result)
{
  struct references r;
  refcounts_unsafe_ref (ref, &r);
  assert_backtrace (! (r.hard == 1 && r.weak == 0)
          || !"refcount detected use-after-free!");
  if (result)
    *result = r;
}

/* Decrement the hard reference count of REF.  If RESULT is not NULL,
   the result of the operation is written there.  This function uses
   atomic operations.  It is not required to serialize calls to this
   function.  */
REFCOUNT_EI void
refcounts_deref (refcounts_t *ref, struct references *result)
{
  const union _references op = { .references = REFCOUNT_REFERENCES (1, 0) };
  union _references r;
  r.value = __atomic_sub_fetch (&ref->value, op.value, __ATOMIC_RELAXED);
  assert_backtrace (r.references.hard != UINT32_MAX
                    || !"refcount underflowed!");
  if (result)
    *result = r.references;
}

/* Promote a weak reference to a hard reference.  If RESULT is not
   NULL, the result of the operation is written there.  This function
   uses atomic operations.  It is not required to serialize calls to
   this function.  */
REFCOUNT_EI void
refcounts_promote (refcounts_t *ref, struct references *result)
{
  /* To promote a weak reference, we need to atomically subtract 1
     from the weak reference count, and add 1 to the hard reference
     count.

     We can subtract by 1 by adding the two's complement of 1 = ~0 to
     a fixed-width value, discarding the overflow.

     We do the same in our uint64_t value, but we have chosen the
     layout of struct references so that when it is used in the union
     _references, the weak reference counts occupy the most
     significant bits.  When we add ~0 to the weak references, the
     overflow will be discarded as unsigned arithmetic is modulo 2^n.
     So we just add a hard reference.  In combination, this is the
     desired operation.  */
  const union _references op =
    { .references = REFCOUNT_REFERENCES (1, ~0U) };
  union _references r;
  r.value = __atomic_add_fetch (&ref->value, op.value, __ATOMIC_RELAXED);
  assert_backtrace (r.references.hard != UINT32_MAX
                    || !"refcount overflowed!");
  assert_backtrace (r.references.weak != UINT32_MAX
                    || !"refcount underflowed!");
  if (result)
    *result = r.references;
}

/* Demote a hard reference to a weak reference.  If RESULT is not
   NULL, the result of the operation is written there.  This function
   uses atomic operations.  It is not required to serialize calls to
   this function.  */
REFCOUNT_EI void
refcounts_demote (refcounts_t *ref, struct references *result)
{
  /* To demote a hard reference, we need to atomically subtract 1 from
     the hard reference count, and add 1 to the weak reference count.

     We can subtract by 1 by adding the two's complement of 1 = ~0 to
     a fixed-width value, discarding the overflow.

     We do the same in our uint64_t value, but we have chosen the
     layout of struct references so that when it is used in the union
     _references, the hard reference counts occupy the least
     significant bits.  When we add ~0 to the hard references, it will
     overflow into the weak references.  This is the desired
     operation.  */
  const union _references op = { .references = REFCOUNT_REFERENCES (~0U, 0) };
  union _references r;
  r.value = __atomic_add_fetch (&ref->value, op.value, __ATOMIC_RELAXED);
  assert_backtrace (r.references.hard != UINT32_MAX
                    || !"refcount underflowed!");
  assert_backtrace (r.references.weak != UINT32_MAX
                    || !"refcount overflowed!");
  if (result)
    *result = r.references;
}

/* Increment the weak reference count of REF.  If RESULT is not NULL,
   the result of the operation is written there.  This function uses
   atomic operations.  It is not required to serialize calls to this
   function.

   This is the unsafe version of refcounts_ref_weak.
   refcounts_ref_weak also checks for use-after-free errors.  When in
   doubt, use that one instead.  */
REFCOUNT_EI void
refcounts_unsafe_ref_weak (refcounts_t *ref, struct references *result)
{
  const union _references op = { .references = REFCOUNT_REFERENCES (0, 1) };
  union _references r;
  r.value = __atomic_add_fetch (&ref->value, op.value, __ATOMIC_RELAXED);
  assert_backtrace (r.references.weak != UINT32_MAX
                    || !"refcount overflowed!");
  if (result)
    *result = r.references;
}

/* Increment the weak reference count of REF.  If RESULT is not NULL,
   the result of the operation is written there.  This function uses
   atomic operations.  It is not required to serialize calls to this
   function.  */
REFCOUNT_EI void
refcounts_ref_weak (refcounts_t *ref, struct references *result)
{
  struct references r;
  refcounts_unsafe_ref_weak (ref, &r);
  assert_backtrace (! (r.hard == 0 && r.weak == 1)
          || !"refcount detected use-after-free!");
  if (result)
    *result = r;
}

/* Decrement the weak reference count of REF.  If RESULT is not NULL,
   the result of the operation is written there.  This function uses
   atomic operations.  It is not required to serialize calls to this
   function.  */
REFCOUNT_EI void
refcounts_deref_weak (refcounts_t *ref, struct references *result)
{
  const union _references op = { .references = REFCOUNT_REFERENCES (0, 1) };
  union _references r;
  r.value = __atomic_sub_fetch (&ref->value, op.value, __ATOMIC_RELAXED);
  assert_backtrace (r.references.weak != UINT32_MAX
                    || !"refcount underflowed!");
  if (result)
    *result = r.references;
}

/* Store the current reference counts of REF in RESULT.  This function
   uses atomic operations.  It is not required to serialize calls to
   this function.  */
REFCOUNT_EI void
refcounts_references (refcounts_t *ref, struct references *result)
{
  union _references r;
  r.value =__atomic_load_n (&ref->value, __ATOMIC_RELAXED);
  *result = r.references;
}

/* Return the hard reference count of REF.  This function uses atomic
   operations.  It is not required to serialize calls to this
   function.  */
REFCOUNT_EI uint32_t
refcounts_hard_references (refcounts_t *ref)
{
  struct references result;
  refcounts_references (ref, &result);
  return result.hard;
}

/* Return the weak reference count of REF.  This function uses atomic
   operations.  It is not required to serialize calls to this
   function.  */
REFCOUNT_EI uint32_t
refcounts_weak_references (refcounts_t *ref)
{
  struct references result;
  refcounts_references (ref, &result);
  return result.weak;
}

#endif /* _HURD_REFCOUNT_H_ */
