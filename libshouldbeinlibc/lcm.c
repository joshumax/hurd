/* Lcm (least common multiple), and gcd (greatest common divisor)

   Copyright (C) 1996 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* There are probably more efficient ways to do these...  */

/* Return the greatest common divisor of p & q.  */
inline long
gcd (long p, long q)
{
  if (p == 0)
    return q;
  else if (q == 0)
    return p;
  else if (p == q)
    return p;
  else if (q > p)
    return gcd (q, p);
  else
    return gcd (q, p % q);
}

/* Return the least common multiple of p & q.  */
long
lcm (long p, long q)
{
  return (p / gcd (p, q)) * q;
}
