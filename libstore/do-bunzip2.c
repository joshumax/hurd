#include <bzlib.h>

/* I/O interface */
extern int (*unzip_read) (char *buf, size_t maxread);
extern void (*unzip_write) (const char *buf, size_t nwrite);
extern void (*unzip_read_error) (void);
extern void (*unzip_error) (const char *msg);

/* bzip2 doesn't require window sliding. Just for buffering. */
#define INBUFSIZ	0x1000
#define OUTBUFSIZ	0x1000

static unsigned char inbuf[INBUFSIZ];
static unsigned char outbuf[OUTBUFSIZ];

#ifdef SMALL_BZIP2
#define SMALL_MODE 1
#else
#define SMALL_MODE 0
#endif

void
do_bunzip2 (void)
{
  int result;
  bz_stream strm;

  strm.bzalloc = NULL;
  strm.bzfree = NULL;
  strm.opaque = NULL;

  strm.avail_in  = 0;
  strm.next_out = outbuf;
  strm.avail_out = OUTBUFSIZ;

  result = BZ2_bzDecompressInit (&strm, 0, SMALL_MODE);

  while (result == BZ_OK)
  {
    if (strm.avail_in == 0)
    {
      strm.next_in = inbuf;
      strm.avail_in  = (*unzip_read)(strm.next_in, INBUFSIZ);

      if (strm.avail_in == 0)
        break;
    }

    result = BZ2_bzDecompress (&strm);

    if ((result != BZ_OK) && (result != BZ_STREAM_END))
      break;

    if ((strm.avail_out == 0) || (result == BZ_STREAM_END))
    {
      (*unzip_write) (outbuf, OUTBUFSIZ - strm.avail_out);
      strm.next_out = outbuf;
      strm.avail_out = OUTBUFSIZ;
    }
  }

  BZ2_bzDecompressEnd (&strm);

  if (result != BZ_STREAM_END)
    (*unzip_error) (NULL);
}
