/* Test program for libstore -- outputs the concatenation of stores  */ 

#include <argp.h>
#include <error.h>
#include <unistd.h>
#include <hurd.h>
#include <sys/fcntl.h>

#include "store.h"

struct argp_option options[] = {
  {"file", 'f', 0, 0, "Use file IO instead of the raw device"},
  {"block-size", 'b', "BYTES", 0, "Set the file block size"},
  {"interleave", 'i', "INTERVAL", 0, "Instead of concatenating the files,"
     " interleave them, every INTERVAL blocks (default 1)"},
  {0, 0}
};
char *arg_doc = "FILE...";
char *doc = 0;

int
main (int argc, char **argv)
{
  struct store *stores[4096];
  int num_stores = 0;
  int use_file_io = 0, block_size = 0, interleave = 0;

  struct store *make_store (char *file)
    {
      error_t err;
      struct store *store;
      file_t source = file_name_lookup (file, O_READ, 0);
      if (errno)
	error (2, errno, "%s", file);
      if (use_file_io)
	if (block_size)
	  {
	    struct stat stat;
	    err = io_stat (source, &stat);
	    if (! err)
	      {
		struct store_run run = {0, stat.st_size / block_size};
		err = _store_file_create (source, block_size, &run, 1, &store);
	      }
	  }
	else
	  err = store_file_create (source, &store);
      else
	err = store_create (source, &store);
      if (err)
	error (err, 3, "%s", file);
      return store;
    }
  void dump (struct store *store, off_t addr,  ssize_t len)
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
	case 'i': interleave = atoi (arg); break;

	case ARGP_KEY_ARG:
	  if (num_stores > sizeof stores / sizeof *stores)
	    error (72, 0, "%s: Too many files", arg);
	  stores[num_stores++] = make_store (arg);
	  break;

	case ARGP_KEY_END:
	  if (num_stores == 1)
	    dump (stores[0], 0, -1);
	  else if (num_stores > 1)
	    {
	      error_t err;
	      struct store *cat;
	      if (interleave)
		err =
		  store_ileave_create (stores, num_stores, interleave, &cat);
	      else
		err = store_concat_create (stores, num_stores, &cat);
	      if (err)
		error (99, err, "Can't concatenate");
	      dump (cat, 0, -1);
	    }
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  struct argp argp = {options, parse_opt, arg_doc, doc};
  argp_parse (&argp, argc, argv, 0, 0, 0);
  exit (0);
}
