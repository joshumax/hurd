/* Prime number generation
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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Return the next prime greater than or equal to N. */
int 
nextprime (int n)
{
  static int *q;
  static int k = 2;
  static int l = 2;
  int p;
  int *m;
  int i, j;
  
  /* See the comment at the end for an explanation of the algorithm 
       used.   */

  if (!q)
    {
      /* Init */
      q = malloc (sizeof (int) * 2);
      q[0] = 2;
      q[1] = 3;
    }

  if (n <= q[0])
    return q[0];

  while (n > q[l - 1])
    {
      /* Grow */

      /* Alloc */
      p = q[l-1] * q[l-1];
      m = calloc (p, sizeof (int));
      assert (m);

      /* Sieve */
      for (i = 0; i < l; i++)
	for (j = q[i] * 2; j < p; j += q[i])
	  m[j] = 1;

      /* Copy */
      for (i = q[l-1] + 1; i < p; i++)
	{
	  if (l == k)
	    {
	      q = realloc (q, k * sizeof (int) * 2);
	      assert (q);
	      k *= 2;
	    }
	  if (!m[i])
	    q[l++] = i;
	}

      free (m);
    }
  
  /* Search */
  i = 0;
  j = l - 1;
  p = j / 2;

  while (q[p - 1] >= n || q[p] < n)
    {
      if (n > q[p])
	i = p + 1;
      else
	j = p - 1;
      p = ((j - i) / 2) + i;
    }
  
  return q[p];
}

/* [This code originally contained the comment "You are not expected
to understand this" (on the theory that every Unix-like system should
have such a comment somewhere, and now I have to find somewhere else
to put it).  I then offered this function as a challenge to Jim
Blandy (jimb@totoro.bio.indiana.edu).  At that time only the six
comments in the function and the description at the top were present.
Jim produced the following brilliant explanation.

 -mib]


The static variable q points to a sorted array of the first l natural
prime numbers.  k is the number of elements which have been allocated
to q, l <= k; we occasionally double k and realloc q accordingly to
maintain this invariant.

The table is initialized to contain a few primes (lines 26, 27,
34-40).  Subsequent code assumes the table isn't empty.

When passed a number n, we grow q until it contains a prime >= n
(lines 45--70), do a binary search in q to find the least prime >= n
(lines 72--84), and return that.

We grow q using a "sieve of Eratosthenes" procedure.  Let p be the
largest prime we've yet found, q[l-1].  We allocate a boolean array m
of p^2 elements, and initialize all its elements to false.  (Upon
completion, m[j] will be false iff j is a prime.)  For each number
q[i] in q, we set all m[j] to true, where j is a multiple of q[i], and
j is a valid index in m.  Once this is done, since every number j (p <
j < p^2) is either prime, or has a prime factor not greater than p,
m[j] will be false iff j is prime.  We scan m for false elements, and
add their indices to q.

As an optimization, we take advantage of the fact that 2 is the first
prime.  But essentially, the code works as described above.

Why is m's size chosen as it is?  Note that the sieve only guarantees
to mark multiples of the numbers in q.  Given that q contains all the
prime numbers from 2 to p, we can safely sieve for prime numbers
between 2 and p^2, because any composite number in that range must
have a prime factor not greater than its square root, and thus not
greater than p; q already contains all such primes.  I suppose we
could trust the sieve a bit farther ahead, but I'm not sure we could
go very far at all.


Possible bug: if there is no prime j such that p < j <= p^2, then the
growth loop will add no primes to q, and thus never exit.  Is there
any guarantee that there will be such a j?  I thought that people had
found proofs that the average density of primes was logarithmic in
their size, or something like that, but no guarantees.  Perhaps there
is no bug, and this is the part I am not expected to understand; oh,
well.


[I don't know if this is a bug or not.  The proofs of the density of
primes only deal with average long-term density, and there might be
stretches where prims thin out to the point that this algorithm fails.
However, it can only compute primes up to maxint (because ints are
used as indexes into the array M in the seive step).  It's certainly
the case that no such thinning out happens before 2^32, so we're safe.
And since this function is only used to size process tables, I don't
think it will ever get that far even.  -mib]

*/
