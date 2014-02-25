/* CPP definitions for MiG processing of exec.defs for exec server.  */

#define FILE_INTRAN trivfs_protid_t trivfs_begin_using_protid (file_t)
#define FILE_DESTRUCTOR trivfs_end_using_protid (trivfs_protid_t)

#define EXEC_IMPORTS					\
  import "priv.h";					\
  import "../libtrivfs/mig-decls.h";			\

#define SERVERCOPY 1
