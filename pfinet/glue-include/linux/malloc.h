#ifndef _HACK_MALLOC_H_
#define _HACK_MALLOC_H_

#include <linux/mm.h>

#include <stdlib.h>

static inline void *kmalloc (size_t sz, int ignored) { return malloc (sz); }
static inline void kfree (void *ptr) { free (ptr); }
static inline void kfree_s (void *ptr, size_t sz) { free (ptr); }
#define free(x) kfree(x)	/* just don't ask */


typedef struct kmem_cache_s kmem_cache_t;

#define	SLAB_HWCACHE_ALIGN 0	/* flag everybody uses */
#define SLAB_ATOMIC 0


extern kmem_cache_t *kmem_cache_create(const char *, size_t, size_t, unsigned long,
				       void (*)(void *, kmem_cache_t *, unsigned long),
				       void (*)(void *, kmem_cache_t *, unsigned long));
extern void *kmem_cache_alloc(kmem_cache_t *, int);
extern void kmem_cache_free(kmem_cache_t *, void *);


#endif
