#include <argp.h>

#include "store.h"

struct argp_options options[] = {
  {0}
};
char *arg_doc = "FILE [OFFSET [LENGTH]]...";
char *doc = "OFFSET defaults to 0, LENGTH to the remainder of FILE";

int
main (int argc, char **argv)
{
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case ARGP_KEY_ARG:
	  if (! store)
	    {
	      file_t source = file_name_lookup (arg, O_READ, 0);
	      if (err)
		error (err, 2, "%s", arg);
	      err = store_create (source, &store);
	      if (err)
		error (err, 3, "%s", arg);
	      mach_port_deallocate (mach_task_self (), source);
	    }
	  else if (offset < 0)
	    offset = atoi (arg);
	  else
	    dump (store, offset, 
	    {
	      
	    }
	  break;
	case ARGP_KEY_END:
	  if (offset < 0)
	    offset = 0;
	  if (length < 0)
	    dump (	    
	case ARGP_KEY_NO_ARGS:
	default:
	  return EINVAL;
	}
      return 0;
    }
  struct argp argp = {options, arg_doc, doc};
  argp_parse (&argp, argc, argv, 0, 0);
  exit (0);
}
