/* Test program for libstore -- outputs a portion of a store  */ 

#include <argp.h>
#include <error.h>
#include <unistd.h>
#include <hurd.h>
#include <sys/fcntl.h>

#include "store.h"

struct argp_option options[] = {
  {"file", 'f', 0, 0, "Use file IO instead of the raw device"},
  {"block-size", 'b', "BYTES", 0, "Set the file block size"},
  {0, 0}
};
char *arg_doc = "FILE [ADDR [LENGTH]]...";
char *doc = "ADDR is in blocks, and defaults to 0; LENGTH is in bytes, and defaults to the remainder of FILE.";

int
main (int argc, char **argv)
{
  struct store *store = 0;
  off_t addr = -1;
  int dumped = 0, use_file_io = 0, block_size = 0;

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
	case 'f': use_file_io = 1; break;
	case 'b': block_size = atoi (arg); break;

	case ARGP_KEY_ARG:
	  if (! store)
	    {
	      error_t err;
	      file_t source = file_name_lookup (arg, O_READ, 0);
	      if (errno)
		error (2, errno, "%s", arg);
	      if (use_file_io)
		if (block_size)
		  {
		    struct stat stat;
		    err = io_stat (source, &stat);
		    if (! err)
		      {
			struct store_run run = {0, stat.st_size / block_size};
			err = _store_file_create (source, block_size, &run, 1,
						  &store);
		      }
		  }
		else
		  err = store_file_create (source, &store);
	      else
		err = store_create (source, &store);
	      if (err)
		error (err, 3, "%s", arg);
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
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, arg_doc, doc};
  argp_parse (&argp, argc, argv, 0, 0, 0);
  exit (0);
}
