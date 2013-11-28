/* CPP definitions for MiG processing of exec.defs for exec server.  */

#define FILE_INTRAN trivfs_protid_t trivfs_begin_using_protid (file_t)
#define FILE_INTRAN_PAYLOAD trivfs_protid_t trivfs_begin_using_protid_payload
#define FILE_DESTRUCTOR trivfs_end_using_protid (trivfs_protid_t)

#define EXEC_IMPORTS					\
  import "priv.h";					\
  import "../libtrivfs/mig-decls.h";			\

#define EXEC_STARTUP_INTRAN                             \
  bootinfo_t begin_using_bootinfo_port (exec_startup_t)
#define EXEC_STARTUP_INTRAN_PAYLOAD                     \
  bootinfo_t begin_using_bootinfo_payload
#define EXEC_STARTUP_DESTRUCTOR                         \
  end_using_bootinfo (bootinfo_t)
#define EXEC_STARTUP_IMPORTS                            \
  import "priv.h";                                      \
  import "mig-decls.h";

#define SERVERCOPY 1
