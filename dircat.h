/* Append the contents of NUM_DIRS directories.  DIRS is an array of
   directory nodes.  One reference is consumed for each of them. If a
   memory allocation error occurs, or if one of the directories is a
   NULL pointer, the references are dropped immediately and NULL is
   returned.  The given DIRS array is duplicated and can therefore be
   allocated on the caller's stack.  Strange things will happen if some
   elements of DIRS have entries with the same name or if one of them is
   not a directory.  */
struct node *
dircat_make_node (struct node *const *dirs, int num_dirs);
