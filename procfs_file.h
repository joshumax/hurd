/* Create a new regular file with the given CONTENTS. If LEN is negative,
   CONTENTS is considered as a string and the file stops at the first
   nul char.  If CLEANUP is non-NULL, it is passed CONTENTS when the
   node is destroyed.  */
struct node *
procfs_file_make_node (void *contents, ssize_t len, void (*cleanup)(void *));
