/*
 * Mach Operating System
 * Copyright (c) 1993-1989 Carnegie Mellon University
 * All Rights Reserved.
 *
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */

#ifndef __MACH_DEFPAGER_PRIV_H__
#define __MACH_DEFPAGER_PRIV_H__

#include <mach.h>
#include <queue.h>
#include <hurd/ihash.h>

/*
 * Bitmap allocation.
 */
typedef unsigned int	bm_entry_t;
#define	NB_BM		32
#define	BM_MASK		0xffffffff

#define	howmany(a,b)	(((a) + (b) - 1)/(b))

/*
 * Value to indicate no block assigned
 */
#define	NO_BLOCK	((vm_offset_t)-1)

/*
 * 'Partition' structure for each paging area.
 * Controls allocation of blocks within paging area.
 */
struct part {
	pthread_mutex_t	p_lock;		/* for bitmap/free */
	char		*name;		/* name */
	vm_size_t	total_size;	/* total number of blocks */
	vm_size_t	free;		/* number of blocks free */
	unsigned int	id;		/* named lookup */
	bm_entry_t	*bitmap;	/* allocation map */
	boolean_t	going_away;	/* destroy attempt in progress */
	struct file_direct *file;	/* file paged to */
};
typedef	struct part	*partition_t;

struct {
	pthread_mutex_t	lock;
	int		n_partitions;
	partition_t	*partition_list;/* array, for quick mapping */
} all_partitions;			/* list of all such */

typedef unsigned char	p_index_t;

#define	P_INDEX_INVALID	((p_index_t)-1)

#define	no_partition(x)	((x) == P_INDEX_INVALID)

/*
 * Allocation info for each paging object.
 *
 * Most operations, even pager_write_offset and pager_put_checksum,
 * just need a read lock.  Higher-level considerations prevent
 * conflicting operations on a single page.  The lock really protects
 * the underlying size and block map memory, so pager_extend needs a
 * write lock.
 *
 * An object can now span multiple paging partitions.  The allocation
 * info we keep is a pair (offset,p_index) where the index is in the
 * array of all partition ptrs, and the offset is partition-relative.
 * Size wise we are doing ok fitting the pair into a single integer:
 * the offset really is in pages so we have vm_page_size bits available
 * for the partition index.
 */
#define	DEBUG_READER_CONFLICTS	0

#if	DEBUG_READER_CONFLICTS
int	default_pager_read_conflicts = 0;
#endif

union dp_map {

	struct {
		unsigned int	p_offset : 24,
				p_index : 8;
	} block;

	union dp_map		*indirect;
};
typedef union dp_map	*dp_map_t;

/* quick check for part==block==invalid */
#define	no_block(e)		((e).indirect == (dp_map_t)NO_BLOCK)
#define	invalidate_block(e)	((e).indirect = (dp_map_t)NO_BLOCK)

struct dpager {
	pthread_mutex_t	lock;		/* lock for extending block map */
					/* XXX should be read-write lock */
#if	DEBUG_READER_CONFLICTS
	int		readers;
	boolean_t	writer;
#endif
	dp_map_t	map;		/* block map */
	vm_size_t	size;		/* size of paging object, in pages */
	vm_size_t	limit;	/* limit (bytes) allowed to grow to */
	vm_size_t	byte_limit; /* limit, which wasn't
				       rounded to page boundary */
	p_index_t	cur_partition;
#ifdef	CHECKSUM
	vm_offset_t	*checksum;	/* checksum - parallel to block map */
#define	NO_CHECKSUM	((vm_offset_t)-1)
#endif	 /* CHECKSUM */
};
typedef struct dpager	*dpager_t;

/*
 * A paging object uses either a one- or a two-level map of offsets
 * into a paging partition.
 */
#define	PAGEMAP_ENTRIES		64
				/* number of pages in a second-level map */
#define	PAGEMAP_SIZE(npgs)	((npgs)*sizeof(vm_offset_t))

#define	INDIRECT_PAGEMAP_ENTRIES(npgs) \
		((((npgs)-1)/PAGEMAP_ENTRIES) + 1)
#define	INDIRECT_PAGEMAP_SIZE(npgs) \
		(INDIRECT_PAGEMAP_ENTRIES(npgs) * sizeof(vm_offset_t *))
#define	INDIRECT_PAGEMAP(size)	\
		(size > PAGEMAP_ENTRIES)

#define	ROUNDUP_TO_PAGEMAP(npgs) \
		(((npgs) + PAGEMAP_ENTRIES - 1) & ~(PAGEMAP_ENTRIES - 1))

/*
 * Mapping between pager port and paging object.
 */
struct dstruct {
	hurd_ihash_locp_t htable_locp;	/* for the ihash table */
	queue_chain_t	links;		/* Link in pager-port list */

	pthread_mutex_t	lock;		/* Lock for the structure */
	pthread_cond_t
			waiting_seqno,	/* someone waiting on seqno */
			waiting_read,	/* someone waiting on readers */
			waiting_write,	/* someone waiting on writers */
			waiting_refs;	/* someone waiting on refs */

	memory_object_t	pager;		/* Pager port */
	mach_port_seqno_t seqno;	/* Pager port sequence number */
	mach_port_t	pager_request;	/* Request port */
	mach_port_urefs_t request_refs;	/* Request port user-refs */
	mach_port_t	pager_name;	/* Name port */
	mach_port_urefs_t name_refs;	/* Name port user-refs */
	boolean_t	external;	/* Is an external object? */

	unsigned int	readers;	/* Reads in progress */
	unsigned int	writers;	/* Writes in progress */

  	/* This is the reply port of an outstanding
           default_pager_object_set_size call.  */
        mach_port_t	lock_request;

	unsigned int	errors;		/* Pageout error count */
	struct dpager	dpager;		/* Actual pager */
};
typedef struct dstruct *	default_pager_t;
#define	DEFAULT_PAGER_NULL	((default_pager_t)0)

/*
 * List of all pagers.  A specific pager is
 * found directly via its port, this list is
 * only used for monitoring purposes by the
 * default_pager_object* calls
 */
struct pager_port {
	struct hurd_ihash	htable;
	pthread_mutex_t	lock;
	queue_head_t	leak_queue;
};

/* The list of pagers.  */
extern struct pager_port all_pagers;

#endif /* __MACH_DEFPAGER_PRIV_H__ */
