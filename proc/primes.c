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

/* Return the next prime greater than or equal to n. */
int 
nextprime (int n)
{
  static int *q;
  static int k = 2;
  static int l = 2;
  int p;
  int *m;
  int i, j;
  
  if (!q)
    {
      q = malloc (sizeof (int) * 2);
      q[0] = 2;
      q[1] = 3;
    }

  if (n <= q[0])
    return q[0];

  while (n > q[l - 1])
    {
      p = q[l-1] * q[l-1];
      m = alloca (sizeof (int) * p);
      bzero (m, sizeof (int) * p);
      
      for (i = 0; i < l; i++)
	for (j = q[i] * 2; j < p; j += q[i])
	  m[j] = 1;

      for (i = q[l-1] + 1; i < p; i++)
	{
	  if (l == k)
	    {
	      q = realloc (q, k * sizeof (int) * 2);
	      k *= 2;
	    }
	  if (!m[i])
	    q[l++] = i;
	}
    }
  
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


main ()
{
  int i;

  initprimes();
  
  for (i = 0; i < 100; i++)
    printf ("%d\t%d\n", i, nextprime(i));
}

