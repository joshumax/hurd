/* Prime number generation
   Copyright (C) 1994, 1996, 1999 Free Software Foundation

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

#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <spin-lock.h>
#include "priv.h"

#define BITS_PER_UNSIGNED (8 * sizeof (unsigned))
#define SQRT_INT_MAX (1 << (BITS_PER_UNSIGNED / 2))

static spin_lock_t table_lock = SPIN_LOCK_INITIALIZER;

/* Return the next prime greater than or equal to N. */
int
_ihash_nextprime (unsigned n)
{
  /* Among other things, We guarantee that, for all i (0 <= i < primes_len),
     primes[i] is a prime,
     next_multiple[i] is a multiple of primes[i],
     next_multiple[i] > primes[primes_len - 1],
     next_multiple[i] is not a multiple of two unless primes[i] == 2, and
     next_multiple[i] is the smallest such value.  */
  static unsigned *primes, *next_multiple;
  static int primes_len;
  static int primes_size;
  static unsigned next_sieve;	/* always even */
  unsigned max_prime;

  spin_lock (&table_lock);

  if (! primes)
    {
      primes_size = 128;
      primes        = (unsigned *) malloc (primes_size * sizeof (*primes));
      next_multiple = (unsigned *) malloc (primes_size
					   * sizeof (*next_multiple));

      primes[0] = 2;		next_multiple[0] = 6;
      primes[1] = 3;		next_multiple[1] = 9;
      primes[2] = 5;		next_multiple[2] = 15;
      primes_len = 3;

      next_sieve = primes[primes_len - 1] + 1;
    }

  if (n <= primes[0])
    {
      spin_unlock (&table_lock);
      return primes[0];
    }

  while (n > (max_prime = primes[primes_len - 1]))
    {
      /* primes doesn't contain any prime large enough.  Sieve from
         max_prime + 1 to 2 * max_prime, looking for more primes.  */
      unsigned start = next_sieve;
      unsigned end   = start + max_prime + 1;
      char sieve[end - start];
      int i;

      bzero (sieve, (end - start) * sizeof (*sieve));

      /* Make the sieve indexed by prime number, rather than
	 distance-from-start-to-the-prime-number.  When we're done,
	 sieve[P] will be zero iff P is prime.  */
#define sieve (sieve - start)

      /* Set sieve[i] for all composites i, start <= i < end.
	 Ignore multiples of 2.  */
      for (i = 1; i < primes_len; i++)
	{
	  unsigned twice_prime = 2 * primes[i];
	  unsigned multiple;

	  for (multiple = next_multiple[i];
	       multiple < end;
	       multiple += twice_prime)
	    sieve[multiple] = 1;
	  next_multiple[i] = multiple;
	}

      for (i = start + 1; i < end; i += 2)
	if (! sieve[i])
	  {
	    if (primes_len >= primes_size)
	      {
		primes_size *= 2;
		primes = (int *) realloc (primes,
					  primes_size * sizeof (*primes));
		next_multiple
		  = (int *) realloc (next_multiple,
				     primes_size * sizeof (*next_multiple));
	      }
	    primes[primes_len] = i;
	    if (i >= SQRT_INT_MAX)
	      next_multiple[primes_len] = INT_MAX;
	    else
	      next_multiple[primes_len] = i * i;
	    primes_len++;
	  }

      next_sieve = end;
    }

  /* Now we have at least one prime >= n.  Find the smallest such.  */
  {
    int bottom = 0;
    int top = primes_len;

    while (bottom < top)
      {
	int mid = (bottom + top) / 2;

	if (primes[mid] < n)
	  bottom = mid + 1;
	else
	  top = mid;
      }

    spin_unlock (&table_lock);
    return primes[top];
  }
}
