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
  char buf[4096], *data = buf;
  size_t data_len = sizeof (buf);
  struct store_argp_params params = { 0, 0, 0 };

  argp_parse (&store_argp, argc, argv, 0, 0, &params);
  s = params.result;

  err = store_read (s, 0, s->size, &data, &data_len);
  if (err)
    error (5, err, s->name ? "%s" : "<store>", s->name);

  if (write (1, data, data_len) < 0)
    error (6, errno, "stdout");

  exit (0);
}
