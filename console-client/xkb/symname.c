#include <limits.h>
#include <stdlib.h>
#include <string.h>
#define NEEDKTABLE
#include "ks_tables.h"
#include "keysymdef.h"

#define NoSymbol 0

typedef unsigned long Signature;
typedef unsigned long KeySym;

KeySym XStringToKeysym(char *s)
{
  register int i, n;
  int h;
  register Signature sig = 0;
  register const char *p = s;
  register int c;
  register int idx;
  const unsigned char *entry;
  unsigned char sig1, sig2;
  KeySym val;

  while ((c = *p++))
    sig = (sig << 1) + c;
  i = sig % KTABLESIZE;
  h = i + 1;
  sig1 = (sig >> 8) & 0xff;
  sig2 = sig & 0xff;
  n = KMAXHASH;
  while ((idx = hashString[i]))
    {
      entry = &_XkeyTable[idx];
      if ((entry[0] == sig1) && (entry[1] == sig2) &&
	  !strcmp(s, (char *)entry + 6))
        {
	  val = (entry[2] << 24) | (entry[3] << 16) |
                (entry[4] << 8)  | entry[5];
	  if (!val)
	    val = XK_VoidSymbol;
	  return val;
        }
      if (!--n)
	break;
      i += h;
      if (i >= KTABLESIZE)

	i -= KTABLESIZE;
    }

  /*

  The KeysymDB is not yet supported.

  if (!initialized)
    (void)_XInitKeysymDB();
  if (keysymdb)
    {
      XrmValue result;
      XrmRepresentation from_type;
      char c;
      XrmQuark names[2];

      names[0] = _XrmInternalStringToQuark(s, p - s - 1, sig, False);
      names[1] = NULLQUARK;
      (void)XrmQGetResource(keysymdb, names, Qkeysym, &from_type, &result);
      if (result.addr && (result.size > 1))
        {
	  val = 0;
	  for (i = 0; i < result.size - 1; i++)
            {
	      c = ((char *)result.addr)[i];
	      if ('0' <= c && c <= '9') val = (val<<4)+c-'0';
	      else if ('a' <= c && c <= 'f') val = (val<<4)+c-'a'+10;
	      else if ('A' <= c && c <= 'F') val = (val<<4)+c-'A'+10;
	      else return NoSymbol;
            }
	  return val;
        }
    }
  */

  if (*s == 'U') {
    val = 0;
    for (p = &s[1]; *p; p++) {
      c = *p;
      if ('0' <= c && c <= '9') val = (val<<4)+c-'0';
      else if ('a' <= c && c <= 'f') val = (val<<4)+c-'a'+10;
      else if ('A' <= c && c <= 'F') val = (val<<4)+c-'A'+10;
      else return NoSymbol;
      if (val > 0x10ffff)
        return NoSymbol;

    }
    if (val < 0x20 || (val > 0x7e && val < 0xa0))
      return NoSymbol;
    if (val < 0x100)
      return val;
    return val | 0x01000000;
  }

  if (strlen(s) > 2 && s[0] == '0' && s[1] == 'x')
    {
      char *tmp = NULL;
      val = strtoul(s, &tmp, 16);
      if (val == ULONG_MAX || (tmp && *tmp != '\0'))
        return NoSymbol;
      else
        return val;
    }

  /* Stupid inconsistency between the headers and XKeysymDB: the former has
   * no separating underscore, while some XF86* syms in the latter did.
   * As a last ditch effort, try without. */
  if (strncmp(s, "XF86_", 5) == 0)
    {
      KeySym ret;
      char *tmp = strdup(s);
      if (!tmp)
        return NoSymbol;
      memmove(&tmp[4], &tmp[5], strlen(s) - 5 + 1);
      ret = XStringToKeysym(tmp);
      free(tmp);
      return ret;
    }

  return NoSymbol;
}
