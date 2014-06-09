#ifndef __GNUPG_GLUE_H__
#define __GNUPG_GLUE_H__

#include <sys/types.h>
#include <random.h>

#define SIZEOF_UNSIGNED_LONG 4
typedef unsigned int u32;
typedef unsigned char byte;

/* GnuPG's config.h  */
#define HAVE_GETTIMEOFDAY 1
#define HAVE_GETRUSAGE 1
#define HAVE_RAND 1

/* GnuPG's memory.h  */
#define m_alloc malloc
#define m_alloc_secure malloc
#define m_alloc_clear(x) calloc(x, 1)
#define m_alloc_secure_clear(x) calloc(x, 1)
#define m_free free
#define m_strdup strdup

/* GnuPG's dynaload.h  */
#define dynload_getfnc_fast_random_poll() (0)
#define dynload_getfnc_gather_random() &gather_random
int
gather_random( void (*add)(const void*, size_t, int), int requester,
               size_t length, int level );

/* GnuPG's miscellaneous stuff.  */
#define BUG() assert(0)
#define _(x) x
#define make_timestamp() time(0)
#define tty_printf printf
#define log_info(format, args...) printf(format , ## args)
#define log_fatal(format, args...) { printf(format , ## args) ; exit(2); }
#define DIM(v) (sizeof(v)/sizeof((v)[0]))

#endif /* __GNUPG_GLUE_H__ */
