/* Some prime numbers for the ihash library.
   I cannot bring myself to assert copyright over a list of prime numbers.
   This file is in the public domain.  */

#include "priv.h"

/* The prime numbers greater than twice the last and less than 2^32.  */
const unsigned int _ihash_sizes[] =
{
  2,
  5,
  11,
  23,
  47,
  97,
  197,
  397,
  797,
  1597,
  3203,
  6421,
  12853,
  25717,
  51437,
  102877,
  205759,
  411527,
  823117,
  1646237,
  3292489,
  6584983,
  13169977,
  26339969,
  52679969,
  105359939,
  210719881,
  421439783,
};

const unsigned int _ihash_nsizes = (sizeof _ihash_sizes
				    / sizeof _ihash_sizes[0]);
