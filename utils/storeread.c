/* Write portions of a store to stdout

   Copyright (C) 1996,97,99,2001,02,03,04 Free Software Foundation, Inc.
   Written by Miles Bader <miles@gnu.org>
   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <argp.h>
#include <error.h>
#include <unistd.h>
#include <stdlib.h>
#include <hurd.h>
#include <sys/fcntl.h>
#include <sys/mman.h>

#include <hurd/store.h>
#include <version.h>

const char *argp_program_version = STANDARD_HURD_VERSION (storeread);

struct argp_option options[] = {
  {"file", 'f', 0, 0, "Use file IO instead of the raw device"},
  {"block-size", 'b', "BYTES", 0, "Set the file block size"},
  {0, 0}
};
const char arg_doc[] = "FILE [ADDR [LENGTH]]...";
const char doc[] = "Write portions of the contents of a store to stdout"
"\vADDR is in blocks, and defaults to 0;"
" LENGTH is in bytes, and defaults to the remainder of FILE.";

int
main (int argc, char **argv)
{
  struct store *store = 0;
  store_offset_t addr = -1;
  int dumped = 0, use_file_io = 0, block_size = 0;

  void dump (store_offset_t addr,  ssize_t len)
    {
      char buf[4096];
      void *data = buf;
      size_t data_len = sizeof (buf);

      /* XXX: store->size can be too big for len.  */
      error_t err =
	store_read (store, addr, len < 0 ? store->size : len,
		    &data, &data_len);
      if (err)
	error (5, err, store->name ? "%s" : "<store>", store->name);
      if (write (1, data, data_len) < 0)
	error (6, errno, "stdout");
      if (data != buf)
	munmap (data, data_len);
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
			err = _store_file_create (source, 0, block_size, &run, 1,
						  &store);
		      }
		  }
		else
		  err = store_file_create (source, 0, &store);
	      else
		err = store_create (source, 0, 0, &store);
	      if (err)
		error (3, err, "%s", arg);
	    }
	  else if (addr < 0)
	    addr = atoll (arg);
	  else
	    {
	      dump (addr, atoi (arg));
	      dumped = 1;
	      addr = -1;
	    }
	  break;

	case ARGP_KEY_END:
	  if (!store)
	    argp_usage (state);

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
