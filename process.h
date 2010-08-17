#include <hurd.h>

/* Create a node for a directory representing information available at
   the proc server PROC for the given PID.  On success, returns the
   newly created node in *NP.  */
error_t
process_lookup_pid (process_t proc, pid_t pid, struct node **np);

