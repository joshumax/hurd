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

/* Array of prime numbers */
int *primes;

/* Allocated size of array `primes'. */
int primessize;

/* Number of primes recorded in array `primes'. */
int nprimes;

/* Initialize primes  */
void
initprimes (void)
{
  primessize = 1;
  nprimes = 1;
  primes = malloc (sizeof (int) * 1);
  *primes = 2;
}

/* Return the next prime greater than or equal to n. */
int 
nextprime (int n)
{
  if (n >= primes[nprimes])
