/* Append the contents of multiple directories.  DIRS is a
   NULL-terminated array of directory nodes.  One reference is consumed
   for each of them, even on ENOMEM, in which case NULL is returned.
   DIRS has to be static data for now, or at list remain available and
   unchanged for the duration of the created node's life.  Strange
   things will happen if they have entries with the same name or if one
   of them is not a directory.  */
struct node *
dircat_make_node (struct node **dirs);
