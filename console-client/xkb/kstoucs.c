#include <assert-backtrace.h>

struct ksmap {
  int keysym;
  unsigned int ucs;
};

#include "kstoucs_map.c"

/* Binary search through `kstoucs_map'.  */
static unsigned int
find_ucs (int keysym, struct ksmap *first, struct ksmap *last)
{
  struct ksmap *middle = first + (last - first) / 2;

  assert_backtrace (first <= last);

  if (middle->keysym == keysym)
    return middle->ucs; /* base case: needle found. */
  else if (first == last) /* empty search space */
    return 0;
  /* recursive cases: halve search space. */
  else if (middle->keysym < keysym)
    return find_ucs (keysym, middle+1, last);
  else if (middle->keysym > keysym)
    /* don't remove middle from the range to compensate
       for rounding down in it's calculation */
    return find_ucs (keysym, first, middle);
  return 0;
}

unsigned int
KeySymToUcs4 (int keysym)
{
#ifdef XKB_DEBUG
  char *XKeysymToString(int keysym);
  printf ("KeySymToUcs4: %s (%d) -> ", XKeysymToString (keysym), keysym);
unsigned int doit (int keysym)
{
#endif

  /* Control characters not covered by keysym map. */
  if (keysym > 0 && keysym < 32)
    return keysym;

  /* 'Unicode keysym' */
  if ((keysym & 0xff000000) == 0x01000000)
    return (keysym & 0x00ffffff);

  #define NUM_KEYSYMS (sizeof kstoucs_map / sizeof(struct ksmap))
  return find_ucs(keysym, &kstoucs_map[0], &kstoucs_map[NUM_KEYSYMS - 1]);
#ifdef XKB_DEBUG
}
  unsigned int ret = doit (keysym);
  printf ("%d\n", ret);
  return ret;
#endif
}
