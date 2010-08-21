#include <ps.h>

/* Create a node for a directory representing the given PID, as published by
   the proc server refrenced by the libps context PC.  On success, returns the
   newly created node in *NP.  */
error_t
process_lookup_pid (struct ps_context *pc, pid_t pid, struct node **np);

