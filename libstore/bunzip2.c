#include <errno.h>

extern void do_bunzip2 (void);	/* Entry point to bunzip2 engine.  */

static error_t
DO_UNZIP (void)
{
  do_bunzip2 ();
  return 0;
}

#define UNZIP		bunzip2
#include "unzipstore.c"
