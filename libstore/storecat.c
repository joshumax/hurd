/* Test program for libstore -- outputs the concatenation of stores  */ 

#include <argp.h>
#include <error.h>
#include <unistd.h>

#include "store.h"

int
main (int argc, char **argv)
{
  error_t err;
  struct store *s;
  char *name;
  off_t addr;
  size_t left;
  const struct argp *parents[] = { &store_argp, 0 };
  struct argp argp =
    { 0, 0, 0, "Write the contents of a store to stdout", parents };
  store_argp_params p = { 0 };

  argp_parse (&argp, argc, argv, 0, 0, &p);
  err = store_parsed_name (p.result, &name);
  if (err)
    error (2, err, "store_parsed_name");

  err = store_parsed_open (p.result, STORE_READONLY, 0, &s);
  if (err)
    error (4, err, "%s", name);

  addr = 0;
  left = s->size;
  while (left > 0)
    {
      size_t read = left > 1024*1024 ? 1024*1024 : left;
      char buf[4096];
      void *data = buf;
      size_t data_len = sizeof (buf);
     
      err = store_read (s, addr, read, &data, &data_len);
      if (err)
	error (5, err, "%s", name);
      if (write (1, data, data_len) < 0)
	error (6, errno, "stdout");

      addr += data_len >> s->log2_block_size;
      left -= data_len;
    }

  exit (0);
}
