#include <argp.h>
#include <error.h>
#include <unistd.h>
#include <hurd.h>
#include <sys/fcntl.h>

#include "store.h"

struct argp_option options[] = {
  {0}
};
char *arg_doc = "FILE [ADDR [LENGTH]]...";
char *doc = "ADDR is in blocks, and defaults to 0; LENGTH is in bytes, and defaults to the remainder of FILE.";

int
main (int argc, char **argv)
{
  struct store *store = 0;
  off_t addr = -1;
  int dumped = 0;

  void dump (off_t addr,  ssize_t len)
    {
      char buf[4096], *data = buf;
      size_t data_len = sizeof (buf);
      error_t err =
	store_read (store, addr, len < 0 ? store->size : len,
		    &data, &data_len);
      if (err)
	error (5, err, store->name ? "%s" : "<store>", store->name);
      if (write (1, data, data_len) < 0)
	error (6, errno, "stdout");
      if (data != buf)
	vm_deallocate (mach_task_self (), (vm_address_t)data, data_len);
    }

  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:
	  if (! store)
	    {
	      error_t err;
	      file_t source = file_name_lookup (arg, O_READ, 0);
	      if (errno)
		error (2, errno, "%s", arg);
	      err = store_create (source, &store);
	      if (err)
		error (err, 3, "%s", arg);
	      mach_port_deallocate (mach_task_self (), source);
	    }
	  else if (addr < 0)
	    addr = atoi (arg);
	  else
	    {
	      dump (addr, atoi (arg));
	      dumped = 1;
	      addr = -1;
	    }
	  break;

	case ARGP_KEY_END:
	  if (addr >= 0)
	    dump (addr, -1);
	  else if (! dumped)
	    dump (0, -1);
	  break;

	case ARGP_KEY_NO_ARGS:
	default:
	  return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, arg_doc, doc};
  argp_parse (&argp, argc, argv, 0, 0);
  exit (0);
}
