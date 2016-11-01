#define TREEFS_DEFINE_EI
#include "treefs.h"
#include "mig-decls.h"

pthread_spinlock_t treefs_node_refcnt_lock = PTHREAD_SPINLOCK_INITIALIZER;
