#define SHUTDOWN_INTRAN                                           \
  trivfs_protid_t trivfs_begin_using_protid (shutdown_t)
#define SHUTDOWN_INTRAN_PAYLOAD                                   \
  trivfs_protid_t trivfs_begin_using_protid_payload
#define SHUTDOWN_DESTRUCTOR                                       \
  trivfs_end_using_protid (trivfs_protid_t)
#define SHUTDOWN_IMPORTS                                          \
  import "libtrivfs/mig-decls.h";
