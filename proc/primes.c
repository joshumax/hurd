/* Seive of Eratosthenes
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

/* Array of prime numbers */
int *primes;

/* Allocated size of array `primes'. */
int primessize;

/* Number of primes recorded in array `primes'. */
int nprimes;

/* Initialize primes  */
void
initprimes ()
{
  primessize = 2;
  nprimes = 2;
  primes = malloc (sizeof (int) * 2);
  primes[0] = 2;
  primes[1] = 3;
}

/* Make the array of primes larger than it is right now.  */
void
growprimes ()
{
  int *iscomp;
  int nints;
  int lastprime = primes[nprimes - 1];
  int i, j;
  
  nints = lastprime * lastprime;
  iscomp = alloca (sizeof (int) * nints);
  bzero (iscomp, sizeof (int) * nints);

  

  for (i = 0; i < nprimes; i++)
    for (j = primes[i] * 2; j < nints; j += primes[i])
      iscomp[j] = 1;

  for (i = lastprime; i < nints; i++)
    {
      if (nprimes == primessize)
	{
	  primes = realloc (primes, primessize * sizeof (int) * 2);
	  primessize *= 2;
	}
      if (!iscomp[i])
	primes[nprimes++] = i;
    }
}  

/* Return the next prime greater than or equal to n. */
int 
nextprime (int n)
{
  int p;
  int low, high;
  
  if (n < primes[0])
    return primes[0];

  while (n > primes[nprimes - 1])
    growprimes ();

  /* Binary search */
  low = 0;
  high = nprimes - 1;
  p = high / 2;

  /* This works because nprimes is always at least 2. */
  while (primes[p - 1] >= n || primes[p] < n)
    {
      if (n > primes[p])
	low = p;
      else
	high = p;
      p = ((high - low) / 2) + low;
    }
  
  return primes[p];
}

  


  
    
  
