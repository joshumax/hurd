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
/*
 * Default pager.  Pages to paging partition.
 *
 * MUST BE ABLE TO ALLOCATE WIRED-DOWN MEMORY!!!
 */

#include <mach.h>
#include <mach/message.h>
#include <mach/notify.h>
#include <mach/mig_errors.h>
#include <mach/thread_switch.h>
#include <mach/task_info.h>
#include <mach/default_pager_types.h>

#include <cthreads.h>

#include <device/device_types.h>
#include <device/device.h>

#include <queue.h>
#include <wiring.h>

#include <assert.h>
#include <stdio.h>

#include <file_io.h>

#include "default_pager_S.h"

#define debug 0

extern void *kalloc();

static char my_name[] = "(default pager):";

static struct mutex printf_lock = MUTEX_INITIALIZER;

#define dprintf(f, x...) \
  ({ mutex_lock (&printf_lock); printf (f , ##x); fflush (stdout); mutex_unlock (&printf_lock); })
#define ddprintf(f, x...) ((void)0)

/*
 * parallel vs serial switch
 */
#define	PARALLEL 1

#if	0
#define	CHECKSUM	1
#endif

#define	USE_PRECIOUS	1

#define	ptoa(p)	((p)*vm_page_size)
#define	atop(a)	((a)/vm_page_size)

/*

 */
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
	struct mutex	p_lock;		/* for bitmap/free */
	vm_size_t	total_size;	/* total number of blocks */
	vm_size_t	free;		/* number of blocks free */
	unsigned int	id;		/* named lookup */
	bm_entry_t	*bitmap;	/* allocation map */
	boolean_t	going_away;	/* destroy attempt in progress */
	struct file_direct *file;	/* file paged to */
};
typedef	struct part	*partition_t;

struct {
	struct mutex	lock;
	int		n_partitions;
	partition_t	*partition_list;/* array, for quick mapping */
} all_partitions;			/* list of all such */

typedef unsigned char	p_index_t;

#define	P_INDEX_INVALID	((p_index_t)-1)

#define	no_partition(x)	((x) == P_INDEX_INVALID)

partition_t partition_of(x)
      int x;
{
      if (x >= all_partitions.n_partitions || x < 0)
	  panic("partition_of x%x", x);
      return all_partitions.partition_list[x];
}

void set_partition_of(x, p)
      int x;
      partition_t p;
{
      if (x >= all_partitions.n_partitions || x < 0)
	  panic("set_partition_of x%x", x);
      all_partitions.partition_list[x] = p;
}

/*
 * Simple mapping from (file)NAME to id
 * Saves space, filenames can be long.
 */
unsigned int
part_id(const unsigned char *name)
{
	register unsigned int len, id, xorid;

	len = strlen(name);
	id = xorid = 0;
	while (len--) {
		xorid ^= *name;
		id += *name++;
	}
	return (id << 8) | xorid;
}

partition_init()
{
	mutex_init(&all_partitions.lock);
	all_partitions.n_partitions = 0;
}

static partition_t
new_partition (const char *name, struct file_direct *fdp,
	       int check_linux_signature)
{
	register partition_t	part;
	register vm_size_t	size, bmsize;
	vm_offset_t raddr;
	mach_msg_type_number_t rsize;
	int rc;
	unsigned int id = part_id(name);

	mutex_lock(&all_partitions.lock);
	{
	  unsigned int i;
	  for (i = 0; i < all_partitions.n_partitions; i++)
	    {
	      part = partition_of(i);
	      if (part && part->id == id)
		{
		  printf ("(default pager): Already paging to partition %s!\n",
			  name);
		  mutex_unlock(&all_partitions.lock);
		  return 0;
		}
	    }
	}
	mutex_unlock(&all_partitions.lock);

	size = atop(fdp->fd_size * fdp->fd_bsize);
	bmsize = howmany(size, NB_BM) * sizeof(bm_entry_t);

	part = (partition_t) kalloc(sizeof(struct part));
	mutex_init(&part->p_lock);
	part->total_size = size;
	part->free	= size;
	part->id	= id;
	part->bitmap	= (bm_entry_t *)kalloc(bmsize);
	part->going_away= FALSE;
	part->file = fdp;

	bzero((char *)part->bitmap, bmsize);

	if (check_linux_signature < 0)
	  {
	    if (check_linux_signature != -3)
	      printf("(default pager): "
		     "Paging to raw partition %s (%uk paging space)\n",
		     name, part->total_size * (vm_page_size / 1024));
	    return part;
	  }

#define LINUX_PAGE_SIZE 4096	/* size of pages in Linux swap partitions */
	rc = page_read_file_direct(part->file,
				   0, LINUX_PAGE_SIZE,
				   &raddr,
				   &rsize);
	if (rc)
	  panic("(default pager): cannot read first page of %s! rc=%#x\n",
		name, rc);
	while (rsize < LINUX_PAGE_SIZE)
	  {
	    /* Filesystem block size is smaller than page size,
	       so we must do several reads to get the whole page.  */
	    vm_address_t baddr, bsize;
	    rc = page_read_file_direct(part->file,
				       rsize, LINUX_PAGE_SIZE-rsize,
				       &baddr,
				       &bsize);
	    if (rc)
	      panic("(default pager): "
		    "cannot read first page of %s! rc=%#x at %#x\n",
		    name, rc, rsize);

	    memcpy ((char *) raddr + rsize, (void *) baddr, bsize);
	    rsize += bsize;
	    vm_deallocate (mach_task_self (), baddr, bsize);
	  }

	if (!memcmp("SWAP-SPACE", (char *) raddr + LINUX_PAGE_SIZE-10, 10))
	  {
	    /* The partition's first page has a Linux swap signature.
	       This means the beginning of the page contains a bitmap
	       of good pages, and all others are bad.  */
	    unsigned int i, j, bad, max;
	    int waste;

	    printf("(default pager): Found Linux 2.0 swap signature in %s\n",
		   name);

	    /* The first page, and the pages corresponding to the bits
	       occupied by the signature in the final 10 bytes of the page,
	       are always unavailable ("bad").  */
	    *(u_int32_t *)raddr &= ~(u_int32_t) 1;
	    memset((char *) raddr + LINUX_PAGE_SIZE-10, 0, 10);

	    max = LINUX_PAGE_SIZE / sizeof(u_int32_t);
	    if (max > (part->total_size + 31) / 32)
	      max = (part->total_size + 31) / 32;

	    bad = 0;
	    for (i = 0; i < max; ++i)
	      {
		u_int32_t bm = ((u_int32_t *) raddr)[i];
		if (bm == ~(u_int32_t) 0)
		  continue;
		/* There are some zero bits in this word.  */
		for (j = 0; j < 32; ++j)
		  if ((bm & (1 << j)) == 0)
		    {
		      unsigned int p = i*32 + j;
		      if (p >= part->total_size)
			break;
		      ++bad;
		      part->bitmap[p / NB_BM] |= 1 << (p % NB_BM);
		    }
	      }
	    part->free -= bad;

	    --bad;		/* Don't complain about first page.  */
	    waste = part->total_size - (8 * (LINUX_PAGE_SIZE-10));
	    if (waste > 0)
	      {
		/* The wasted pages were already marked "bad".  */
		bad -= waste;
		if (bad > 0)
		  printf("\
(default pager): Paging to %s, %dk swap-space (%dk bad, %dk wasted at end)\n",
			 name,
			 part->free * (LINUX_PAGE_SIZE / 1024),
			 bad * (LINUX_PAGE_SIZE / 1024),
			 waste * (LINUX_PAGE_SIZE / 1024));
		else
		  printf("\
(default pager): Paging to %s, %dk swap-space (%dk wasted at end)\n",
			 name,
			 part->free * (LINUX_PAGE_SIZE / 1024),
			 waste * (LINUX_PAGE_SIZE / 1024));
	      }
	    else if (bad > 0)
	      printf("\
(default pager): Paging to %s, %dk swap-space (excludes %dk marked bad)\n",
		     name,
		     part->free * (LINUX_PAGE_SIZE / 1024),
		     bad * (LINUX_PAGE_SIZE / 1024));
	    else
	      printf("\
(default pager): Paging to %s, %dk swap-space\n",
		     name,
		     part->free * (LINUX_PAGE_SIZE / 1024));
	  }
	else if (!memcmp("SWAPSPACE2",
			 (char *) raddr + LINUX_PAGE_SIZE-10, 10))
	  {
	    struct
	    {
	      u_int8_t bootbits[1024];
	      u_int32_t version;
	      u_int32_t last_page;
	      u_int32_t nr_badpages;
	      u_int32_t padding[125];
	      u_int32_t badpages[1];
	    } *hdr = (void *) raddr;

	    printf("\
(default pager): Found Linux 2.2 swap signature (v%u) in %s...",
		   hdr->version, name);

	    part->bitmap[0] |= 1; /* first page unusable */
	    part->free--;

	    switch (hdr->version)
	      {
	      default:
		if (check_linux_signature)
		  {
		    printf ("version %u unknown!  SKIPPING %s!\n",
			    hdr->version,
			    name);
		    vm_deallocate(mach_task_self(), raddr, rsize);
		    kfree(part->bitmap, bmsize);
		    kfree(part, sizeof *part);
		    return 0;
		  }
		else
		  printf ("version %u unknown! IGNORING SIGNATURE PAGE!"
			  " %dk swap-space\n",
			  hdr->version,
			  part->free * (LINUX_PAGE_SIZE / 1024));
		break;

	      case 1:
		{
		  unsigned int waste, i;
		  if (hdr->last_page > part->total_size)
		    {
		      printf ("signature says %uk, partition has only %uk! ",
			    hdr->last_page * (LINUX_PAGE_SIZE / 1024),
			    part->total_size * (LINUX_PAGE_SIZE / 1024));
		      waste = 0;
		    }
		  else
		    {
		      waste = part->total_size - hdr->last_page;
		      part->total_size = hdr->last_page;
		      part->free = part->total_size - 1;
		    }
		  for (i = 0; i < hdr->nr_badpages; ++i)
		    {
		      const u_int32_t bad = hdr->badpages[i];
		      part->bitmap[bad / NB_BM] |= 1 << (bad % NB_BM);
		      part->free--;
		    }
		  printf ("%uk swap-space",
			  part->free * (LINUX_PAGE_SIZE / 1024));
		  if (hdr->nr_badpages != 0)
		    printf (" (excludes %uk marked bad)",
			    hdr->nr_badpages * (LINUX_PAGE_SIZE / 1024));
		  if (waste != 0)
		    printf (" (excludes %uk at end of partition)",
			    waste * (LINUX_PAGE_SIZE / 1024));
		  printf ("\n");
		}
	      }
	  }
	else if (check_linux_signature)
	  {
	    printf ("(default pager): "
		    "Cannot find Linux swap signature page!  "
		    "SKIPPING %s (%uk partition)!",
		    name, part->total_size * (vm_page_size / 1024));
	    kfree(part->bitmap, bmsize);
	    kfree(part, sizeof *part);
	    part = 0;
	  }
	else
	  printf("(default pager): "
		 "Paging to raw partition %s (%uk paging space)\n",
		 name, part->total_size * (vm_page_size / 1024));

	vm_deallocate(mach_task_self(), raddr, rsize);

	return part;
}

/*
 * Create a partition descriptor,
 * add it to the list of all such.
 * size is in BYTES.
 */
void
create_paging_partition(const char *name,
			struct file_direct *fdp, int isa_file,
			int linux_signature)
{
	register partition_t	part;

	part = new_partition (name, fdp, linux_signature);
	if (!part)
	  return;

	mutex_lock(&all_partitions.lock);
	{
		register int i;

		for (i = 0; i < all_partitions.n_partitions; i++)
			if (partition_of(i) == 0) break;

		if (i == all_partitions.n_partitions) {
			register partition_t	*new_list, *old_list;
			register int		n;

			n = i ? (i<<1) : 2;
			new_list = (partition_t *)
				kalloc( n * sizeof(partition_t) );
			if (new_list == 0) no_paging_space(TRUE);
			bzero(new_list, n*sizeof(partition_t));
			if (i) {
			    old_list = all_partitions.partition_list;
			    bcopy(old_list, new_list, i*sizeof(partition_t));
			}
			all_partitions.partition_list = new_list;
			all_partitions.n_partitions = n;
			if (i) kfree(old_list, i*sizeof(partition_t));
		}
		set_partition_of(i, part);
	}
	mutex_unlock(&all_partitions.lock);

#if 0
	dprintf("%s Added paging %s %s\n", my_name,
		(isa_file) ? "file" : "device",  name);
#endif
	overcommitted(TRUE, part->free);
}

/*
 * Choose the most appropriate default partition
 * for an object of SIZE bytes.
 * Return the partition locked, unless
 * the object has no CUR_PARTition.
 */
p_index_t
choose_partition(size, cur_part)
	unsigned int		size;
	register p_index_t	cur_part;
{
	register partition_t	part;
	register boolean_t	found = FALSE;
	register int		i;

	mutex_lock(&all_partitions.lock);
	for (i = 0; i < all_partitions.n_partitions; i++) {

		/* the undesireable one ? */
		if (i == cur_part)
			continue;

ddprintf ("choose_partition(%x,%d,%d)\n",size,cur_part,i);
		/* one that was removed ? */
		if ((part = partition_of(i)) == 0)
			continue;

		/* one that is being removed ? */
		if (part->going_away)
			continue;

		/* is it big enough ? */
		mutex_lock(&part->p_lock);
		if (ptoa(part->free) >= size) {
			if (cur_part != P_INDEX_INVALID) {
				mutex_unlock(&all_partitions.lock);
				return (p_index_t)i;
			} else
				found = TRUE;
		}
		mutex_unlock(&part->p_lock);

		if (found) break;
	}
	mutex_unlock(&all_partitions.lock);
	return (found) ? (p_index_t)i : P_INDEX_INVALID;
}

/*
 * Allocate a page in a paging partition
 * The partition is returned unlocked.
 */
vm_offset_t
pager_alloc_page(pindex, lock_it)
	p_index_t	pindex;
{
	register int	bm_e;
	register int	bit;
	register int	limit;
	register bm_entry_t	*bm;
	partition_t	part;
	static char	here[] = "%spager_alloc_page";

	if (no_partition(pindex))
	    return (NO_BLOCK);
ddprintf ("pager_alloc_page(%d,%d)\n",pindex,lock_it);
	part = partition_of(pindex);

	/* unlikely, but possible deadlock against destroy_partition */
	if (!part || part->going_away)
	    return (NO_BLOCK);

	if (lock_it)
	    mutex_lock(&part->p_lock);

	if (part->free == 0) {
	    /* out of paging space */
	    mutex_unlock(&part->p_lock);
	    return (NO_BLOCK);
	}

	limit = howmany(part->total_size, NB_BM);
	bm = part->bitmap;
	for (bm_e = 0; bm_e < limit; bm_e++, bm++)
	    if (*bm != BM_MASK)
		break;

	if (bm_e == limit)
	    panic(here,my_name);

	/*
	 * Find and set the proper bit
	 */
	{
	    register bm_entry_t	b = *bm;

	    for (bit = 0; bit < NB_BM; bit++)
		if ((b & (1<<bit)) == 0)
		    break;
	    if (bit == NB_BM)
		panic(here,my_name);

	    *bm = b | (1<<bit);
	    part->free--;

	}

	mutex_unlock(&part->p_lock);

	return (bm_e*NB_BM+bit);
}

/*
 * Deallocate a page in a paging partition
 */
void
pager_dealloc_page(pindex, page, lock_it)
	p_index_t		pindex;
	register vm_offset_t	page;
{
	register partition_t	part;
	register int	bit, bm_e;

	/* be paranoid */
	if (no_partition(pindex))
	    panic("%sdealloc_page",my_name);
ddprintf ("pager_dealloc_page(%d,%x,%d)\n",pindex,page,lock_it);
	part = partition_of(pindex);

	if (page >= part->total_size)
	    panic("%sdealloc_page",my_name);

	bm_e = page / NB_BM;
	bit  = page % NB_BM;

	if (lock_it)
	    mutex_lock(&part->p_lock);

	part->bitmap[bm_e] &= ~(1<<bit);
	part->free++;

	if (lock_it)
	    mutex_unlock(&part->p_lock);
}

/*

 */
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
	struct mutex	lock;		/* lock for extending block map */
					/* XXX should be read-write lock */
#if	DEBUG_READER_CONFLICTS
	int		readers;
	boolean_t	writer;
#endif
	dp_map_t	map;		/* block map */
	vm_size_t	size;		/* size of paging object, in pages */
	vm_size_t	limit;	/* limit (bytes) allowed to grow to */
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
 * Object sizes are rounded up to the next power of 2,
 * unless they are bigger than a given maximum size.
 */
vm_size_t	max_doubled_size = 4 * 1024 * 1024;	/* 4 meg */

/*
 * Attach a new paging object to a paging partition
 */
void
pager_alloc(pager, part, size)
	register dpager_t	pager;
	p_index_t		part;
	register vm_size_t	size;	/* in BYTES */
{
	register int    i;
	register dp_map_t mapptr, emapptr;

	mutex_init(&pager->lock);
#if	DEBUG_READER_CONFLICTS
	pager->readers = 0;
	pager->writer = FALSE;
#endif
	pager->cur_partition = part;

	/*
	 * Convert byte size to number of pages, then increase to the nearest
	 * power of 2.
	 */
	size = atop(size);
	if (size <= atop(max_doubled_size)) {
	    i = 1;
	    while (i < size)
		i <<= 1;
	    size = i;
	} else
	    size = ROUNDUP_TO_PAGEMAP(size);

	/*
	 * Allocate and initialize the block map
	 */
	{
		register vm_size_t	alloc_size;
		dp_map_t		init_value;

		if (INDIRECT_PAGEMAP(size)) {
			alloc_size = INDIRECT_PAGEMAP_SIZE(size);
			init_value = (dp_map_t)0;
		} else {
			alloc_size = PAGEMAP_SIZE(size);
			init_value = (dp_map_t)NO_BLOCK;
		}

		mapptr = (dp_map_t) kalloc(alloc_size);
		for (emapptr = &mapptr[(alloc_size-1) / sizeof(vm_offset_t)];
		     emapptr >= mapptr;
		     emapptr--)
			emapptr->indirect = init_value;

	}
	pager->map = mapptr;
	pager->size = size;
	pager->limit = (vm_size_t)-1;

#ifdef	CHECKSUM
	if (INDIRECT_PAGEMAP(size)) {
		mapptr = (vm_offset_t *)
			kalloc(INDIRECT_PAGEMAP_SIZE(size));
		for (i = INDIRECT_PAGEMAP_ENTRIES(size); --i >= 0;)
			mapptr[i] = 0;
	} else {
		mapptr = (vm_offset_t *) kalloc(PAGEMAP_SIZE(size));
		for (i = 0; i < size; i++)
			mapptr[i] = NO_CHECKSUM;
	}
	pager->checksum = mapptr;
#endif	 /* CHECKSUM */
}

/*
 * Return size (in bytes) of space actually allocated to this pager.
 * The pager is read-locked.
 */

vm_size_t
pager_allocated(pager)
	register dpager_t	pager;
{
	vm_size_t       size;
	register dp_map_t map, emap;
	vm_size_t       asize;

	size = pager->size;	/* in pages */
	asize = 0;		/* allocated, in pages */
	map = pager->map;

	if (INDIRECT_PAGEMAP(size)) {
		for (emap = &map[INDIRECT_PAGEMAP_ENTRIES(size)];
		     map < emap; map++) {

			register dp_map_t	map2, emap2;

			if ((map2 = map->indirect) == 0)
				continue;

			for (emap2 = &map2[PAGEMAP_ENTRIES];
			     map2 < emap2; map2++)
				if ( ! no_block(*map2) )
					asize++;

		}
	} else {
		for (emap = &map[size]; map < emap; map++)
			if ( ! no_block(*map) )
				asize++;
	}

	return ptoa(asize);
}

/*
 * Find offsets (in the object) of pages actually allocated to this pager.
 * Returns the number of allocated pages, whether or not they all fit.
 * The pager is read-locked.
 */

unsigned int
pager_pages(pager, pages, numpages)
	dpager_t			pager;
	register default_pager_page_t	*pages;
	unsigned int			numpages;
{
	vm_size_t       size;
	dp_map_t        map, emap;
	unsigned int    actual;
	vm_offset_t     offset;

	size = pager->size;	/* in pages */
	map = pager->map;
	actual = 0;
	offset = 0;

	if (INDIRECT_PAGEMAP(size)) {
		for (emap = &map[INDIRECT_PAGEMAP_ENTRIES(size)];
		     map < emap; map++) {

			register dp_map_t	map2, emap2;

			if ((map2 = map->indirect) == 0) {
				offset += vm_page_size * PAGEMAP_ENTRIES;
				continue;
			}
			for (emap2 = &map2[PAGEMAP_ENTRIES];
			     map2 < emap2; map2++)
				if ( ! no_block(*map2) ) {
					if (actual++ < numpages)
						pages++->dpp_offset = offset;
				}
			offset += vm_page_size;
		}
	} else {
		for (emap = &map[size]; map < emap; map++)
			if ( ! no_block(*map) ) {
				if (actual++ < numpages)
					pages++->dpp_offset = offset;
			}
		offset += vm_page_size;
	}
	return actual;
}

/*
 * Extend the map for a paging object.
 *
 * XXX This implementation can allocate an arbitrary large amount
 * of wired memory when extending a big block map.  Because vm-privileged
 * threads call pager_extend, this can crash the system by exhausting
 * system memory.
 */
void
pager_extend(pager, new_size)
	register dpager_t	pager;
	register vm_size_t	new_size;	/* in pages */
{
	register dp_map_t	new_mapptr;
	register dp_map_t	old_mapptr;
	register int		i;
	register vm_size_t	old_size;

	mutex_lock(&pager->lock);		/* XXX lock_write */
#if	DEBUG_READER_CONFLICTS
	pager->writer = TRUE;
#endif
	/*
	 * Double current size until we cover new size.
	 * If object is 'too big' just use new size.
	 */
	old_size = pager->size;

	if (new_size <= atop(max_doubled_size)) {
	    i = old_size;
	    while (i < new_size)
		i <<= 1;
	    new_size = i;
	} else
	    new_size = ROUNDUP_TO_PAGEMAP(new_size);

	if (INDIRECT_PAGEMAP(old_size)) {
	    /*
	     * Pager already uses two levels.  Allocate
	     * a larger indirect block.
	     */
	    new_mapptr = (dp_map_t)
			kalloc(INDIRECT_PAGEMAP_SIZE(new_size));
	    old_mapptr = pager->map;
	    for (i = 0; i < INDIRECT_PAGEMAP_ENTRIES(old_size); i++)
		new_mapptr[i] = old_mapptr[i];
	    for (; i < INDIRECT_PAGEMAP_ENTRIES(new_size); i++)
		new_mapptr[i].indirect = (dp_map_t)0;
	    kfree((char *)old_mapptr, INDIRECT_PAGEMAP_SIZE(old_size));
	    pager->map = new_mapptr;
	    pager->size = new_size;
#ifdef	CHECKSUM
	    new_mapptr = (vm_offset_t *)
			kalloc(INDIRECT_PAGEMAP_SIZE(new_size));
	    old_mapptr = pager->checksum;
	    for (i = 0; i < INDIRECT_PAGEMAP_ENTRIES(old_size); i++)
		new_mapptr[i] = old_mapptr[i];
	    for (; i < INDIRECT_PAGEMAP_ENTRIES(new_size); i++)
		new_mapptr[i] = 0;
	    kfree((char *)old_mapptr, INDIRECT_PAGEMAP_SIZE(old_size));
	    pager->checksum = new_mapptr;
#endif	 /* CHECKSUM */
#if	DEBUG_READER_CONFLICTS
	    pager->writer = FALSE;
#endif
	    mutex_unlock(&pager->lock);
#if 0
	    ddprintf ("pager_extend 1 mapptr %x [3b] = %x\n", new_mapptr,
		     new_mapptr[0x3b]);
	    if (new_mapptr[0x3b].indirect > 0x10000
		&& new_mapptr[0x3b].indirect != NO_BLOCK)
	      panic ("debug panic");
#endif
	    return;
	}

	if (INDIRECT_PAGEMAP(new_size)) {
	    /*
	     * Changing from direct map to indirect map.
	     * Allocate both indirect and direct map blocks,
	     * since second-level (direct) block must be
	     * full size (PAGEMAP_SIZE(PAGEMAP_ENTRIES)).
	     */

	    /*
	     * Allocate new second-level map first.
	     */
	    new_mapptr = (dp_map_t) kalloc(PAGEMAP_SIZE(PAGEMAP_ENTRIES));
	    old_mapptr = pager->map;
	    for (i = 0; i < old_size; i++)
		new_mapptr[i] = old_mapptr[i];
	    for (; i < PAGEMAP_ENTRIES; i++)
		invalidate_block(new_mapptr[i]);
	    kfree((char *)old_mapptr, PAGEMAP_SIZE(old_size));
	    old_mapptr = new_mapptr;

#if 0
	    ddprintf ("pager_extend 2 mapptr %x [3b] = %x\n", new_mapptr,
		     new_mapptr[0x3b]);
	    if (new_mapptr[0x3b].indirect > 0x10000
		&& new_mapptr[0x3b].indirect != NO_BLOCK)
	      panic ("debug panic");
#endif

	    /*
	     * Now allocate indirect map.
	     */
	    new_mapptr = (dp_map_t)
			kalloc(INDIRECT_PAGEMAP_SIZE(new_size));
	    new_mapptr[0].indirect = old_mapptr;
	    for (i = 1; i < INDIRECT_PAGEMAP_ENTRIES(new_size); i++)
		new_mapptr[i].indirect = 0;
	    pager->map = new_mapptr;
	    pager->size = new_size;
#ifdef	CHECKSUM
	    /*
	     * Allocate new second-level map first.
	     */
	    new_mapptr = (vm_offset_t *)kalloc(PAGEMAP_SIZE(PAGEMAP_ENTRIES));
	    old_mapptr = pager->checksum;
	    for (i = 0; i < old_size; i++)
		new_mapptr[i] = old_mapptr[i];
	    for (; i < PAGEMAP_ENTRIES; i++)
		new_mapptr[i] = NO_CHECKSUM;
	    kfree((char *)old_mapptr, PAGEMAP_SIZE(old_size));
	    old_mapptr = new_mapptr;

	    /*
	     * Now allocate indirect map.
	     */
	    new_mapptr = (vm_offset_t *)
			kalloc(INDIRECT_PAGEMAP_SIZE(new_size));
	    new_mapptr[0] = (vm_offset_t) old_mapptr;
	    for (i = 1; i < INDIRECT_PAGEMAP_ENTRIES(new_size); i++)
		new_mapptr[i] = 0;
	    pager->checksum = new_mapptr;
#endif	 /* CHECKSUM */
#if	DEBUG_READER_CONFLICTS
	    pager->writer = FALSE;
#endif
	    mutex_unlock(&pager->lock);
	    return;
	}
	/*
	 * Enlarging a direct block.
	 */
	new_mapptr = (dp_map_t)	kalloc(PAGEMAP_SIZE(new_size));
	old_mapptr = pager->map;
	for (i = 0; i < old_size; i++)
	    new_mapptr[i] = old_mapptr[i];
	for (; i < new_size; i++)
	    invalidate_block(new_mapptr[i]);
	kfree((char *)old_mapptr, PAGEMAP_SIZE(old_size));
	pager->map = new_mapptr;
	pager->size = new_size;
#ifdef	CHECKSUM
	new_mapptr = (vm_offset_t *)
		kalloc(PAGEMAP_SIZE(new_size));
	old_mapptr = pager->checksum;
	for (i = 0; i < old_size; i++)
	    new_mapptr[i] = old_mapptr[i];
	for (; i < new_size; i++)
	    new_mapptr[i] = NO_CHECKSUM;
	kfree((char *)old_mapptr, PAGEMAP_SIZE(old_size));
	pager->checksum = new_mapptr;
#endif	 /* CHECKSUM */
#if	DEBUG_READER_CONFLICTS
	pager->writer = FALSE;
#endif
	mutex_unlock(&pager->lock);
}

/* Truncate a memory object.  First, any pages between the new size
   and the (larger) old size are deallocated.  Then, the size of
   the pagemap may be reduced, an indirect map may be turned into
   a direct map.

   The pager must be locked by the caller.  */
static void
pager_truncate(dpager_t pager, vm_size_t new_size)	/* in pages */
{
  dp_map_t new_mapptr;
  dp_map_t old_mapptr;
  int i;
  vm_size_t old_size;

  /* This deallocates the pages necessary to truncate a direct map
     previously of size NEW_SIZE to the smaller size OLD_SIZE.  */
  inline void dealloc_direct (dp_map_t mapptr,
			      vm_size_t old_size, vm_size_t new_size)
    {
      vm_size_t i;
      for (i = new_size; i < old_size; ++i)
	{
	  const union dp_map entry = mapptr[i];
	  pager_dealloc_page(entry.block.p_index, entry.block.p_offset,
			     TRUE);
	  invalidate_block(mapptr[i]);
	}
    }

  old_size = pager->size;

  if (INDIRECT_PAGEMAP(old_size))
    {
      /* First handle the entire second-levels blocks that are being freed.  */
      for (i = INDIRECT_PAGEMAP_ENTRIES(new_size);
	   i < INDIRECT_PAGEMAP_ENTRIES(old_size);
	   ++i)
	{
	  const dp_map_t mapptr = pager->map[i].indirect;
	  pager->map[i].indirect = (dp_map_t)0;
	  dealloc_direct (mapptr, PAGEMAP_ENTRIES, 0);
	  kfree ((char *)mapptr, PAGEMAP_SIZE(PAGEMAP_ENTRIES));
	}

      /* Now truncate what's now the final nonempty direct block.  */
      dealloc_direct (pager->map[(new_size - 1) / PAGEMAP_ENTRIES].indirect,
		      old_size & (PAGEMAP_ENTRIES - 1),
		      new_size & (PAGEMAP_ENTRIES - 1));

      if (INDIRECT_PAGEMAP (new_size))
	{
	  if (INDIRECT_PAGEMAP_SIZE (new_size) >= vm_page_size)
	    /* XXX we know how kalloc.c works; avoid copying.  */
	    kfree ((char *) round_page ((vm_address_t) pager->map
					+ INDIRECT_PAGEMAP_SIZE (new_size)),
		   round_page (INDIRECT_PAGEMAP_SIZE (old_size))
		   - round_page (INDIRECT_PAGEMAP_SIZE (new_size)));
	  else
	    {
	      const dp_map_t old_mapptr = pager->map;
	      pager->map = (dp_map_t) kalloc (INDIRECT_PAGEMAP_SIZE(new_size));
	      memcpy (pager->map, old_mapptr, INDIRECT_PAGEMAP_SIZE(old_size));
	      kfree ((char *) old_mapptr, INDIRECT_PAGEMAP_SIZE (old_size));
	    }
	}
      else
	{
	  /* We are truncating to a size small enough that it goes to using
	     a one-level map.  We already have that map, as the first and only
	     nonempty element in our indirect map.  */
	  const dp_map_t mapptr = pager->map[0].indirect;
	  kfree((char *)pager->map, INDIRECT_PAGEMAP_SIZE(old_size));
	  pager->map = mapptr;
	  old_size = PAGEMAP_ENTRIES;
	}
    }

  if (new_size == 0)
    new_size = 1;

  if (! INDIRECT_PAGEMAP(old_size))
    {
      /* First deallocate pages in the truncated region.  */
      dealloc_direct (pager->map, old_size, new_size);
      /* Now reduce the size of the direct map itself.  We don't bother
	 with kalloc/kfree if it's not shrinking enough that kalloc.c
	 would actually use less.  */
      if (PAGEMAP_SIZE (new_size) <= PAGEMAP_SIZE (old_size) / 2)
	{
	  const dp_map_t old_mapptr = pager->map;
	  pager->map = (dp_map_t) kalloc (PAGEMAP_SIZE (new_size));
	  memcpy (pager->map, old_mapptr, PAGEMAP_SIZE (old_size));
	  kfree ((char *) old_mapptr, PAGEMAP_SIZE (old_size));
	}
    }

  pager->size = new_size;

#ifdef	CHECKSUM
#error write me
#endif	 /* CHECKSUM */
}


/*
 * Given an offset within a paging object, find the
 * corresponding block within the paging partition.
 * Return NO_BLOCK if none allocated.
 */
union dp_map
pager_read_offset(pager, offset)
	register dpager_t	pager;
	vm_offset_t		offset;
{
	register vm_offset_t	f_page;
	union dp_map		pager_offset;

	f_page = atop(offset);

#if	DEBUG_READER_CONFLICTS
	if (pager->readers > 0)
	    default_pager_read_conflicts++;	/* would have proceeded with
						   read/write lock */
#endif
	mutex_lock(&pager->lock);		/* XXX lock_read */
#if	DEBUG_READER_CONFLICTS
	pager->readers++;
#endif
	if (f_page >= pager->size)
	  {
	    ddprintf ("%spager_read_offset pager %x: bad page %d >= size %d",
		    my_name, pager, f_page, pager->size);
	    mutex_unlock(&pager->lock);
	    return (union dp_map) (union dp_map *) NO_BLOCK;
#if 0
	    panic("%spager_read_offset",my_name);
#endif
	  }

	if (INDIRECT_PAGEMAP(pager->size)) {
	    register dp_map_t	mapptr;

	    mapptr = pager->map[f_page/PAGEMAP_ENTRIES].indirect;
	    if (mapptr == 0)
		invalidate_block(pager_offset);
	    else
		pager_offset = mapptr[f_page%PAGEMAP_ENTRIES];
	}
	else {
	    pager_offset = pager->map[f_page];
	}

#if	DEBUG_READER_CONFLICTS
	pager->readers--;
#endif
	mutex_unlock(&pager->lock);
	return (pager_offset);
}

#if	USE_PRECIOUS
/*
 * Release a single disk block.
 */
pager_release_offset(pager, offset)
	register dpager_t	pager;
	vm_offset_t		offset;
{
	register union dp_map	entry;

	offset = atop(offset);

	mutex_lock(&pager->lock);	/* XXX lock_read */

	if (INDIRECT_PAGEMAP(pager->size)) {
		register dp_map_t	mapptr;

		mapptr = pager->map[offset / PAGEMAP_ENTRIES].indirect;
		entry = mapptr[offset % PAGEMAP_ENTRIES];
		invalidate_block(mapptr[offset % PAGEMAP_ENTRIES]);
	} else {
		entry = pager->map[offset];
		invalidate_block(pager->map[offset]);
	}

	mutex_unlock(&pager->lock);

	pager_dealloc_page(entry.block.p_index, entry.block.p_offset, TRUE);
}
#endif	/*USE_PRECIOUS*/


/*
 * Move a page from one partition to another
 * New partition is locked, old partition is
 * locked unless LOCK_OLD sez otherwise.
 */
union dp_map
pager_move_page(block)
	union dp_map	block;
{
	partition_t	old_part, new_part;
	p_index_t	old_pindex, new_pindex;
	union dp_map	ret;
	vm_size_t	size;
	vm_offset_t	raddr, offset, new_offset;
	kern_return_t	rc;
	static char	here[] = "%spager_move_page";

	old_pindex = block.block.p_index;
	invalidate_block(ret);

	/* See if we have room to put it anywhere else */
	new_pindex = choose_partition( ptoa(1), old_pindex);
	if (no_partition(new_pindex))
		return ret;

	/* this unlocks the new partition */
	new_offset = pager_alloc_page(new_pindex, FALSE);
	if (new_offset == NO_BLOCK)
		panic(here,my_name);

	/*
	 * Got the resources, now move the data
	 */
ddprintf ("pager_move_page(%x,%d,%d)\n",block.block.p_offset,old_pindex,new_pindex);
	old_part = partition_of(old_pindex);
	offset = ptoa(block.block.p_offset);
	rc = page_read_file_direct (old_part->file,
				    offset,
				    vm_page_size,
				    &raddr,
				    &size);
	if (rc != 0)
		panic(here,my_name);

	/* release old */
	pager_dealloc_page(old_pindex, block.block.p_offset, FALSE);

	new_part = partition_of(new_pindex);
	offset = ptoa(new_offset);
	rc = page_write_file_direct (new_part->file,
				     offset,
				     raddr,
				     size,
				     &size);
	if (rc != 0)
		panic(here,my_name);

	(void) vm_deallocate( mach_task_self(), raddr, size);

	ret.block.p_offset = new_offset;
	ret.block.p_index  = new_pindex;

	return ret;
}

#ifdef	CHECKSUM
/*
 * Return the checksum for a block.
 */
int
pager_get_checksum(pager, offset)
	register dpager_t	pager;
	vm_offset_t		offset;
{
	register vm_offset_t	f_page;
	int checksum;

	f_page = atop(offset);

	mutex_lock(&pager->lock);		/* XXX lock_read */
	if (f_page >= pager->size)
	    panic("%spager_get_checksum",my_name);

	if (INDIRECT_PAGEMAP(pager->size)) {
	    register vm_offset_t *mapptr;

	    mapptr = (vm_offset_t *)pager->checksum[f_page/PAGEMAP_ENTRIES];
	    if (mapptr == 0)
		checksum = NO_CHECKSUM;
	    else
		checksum = mapptr[f_page%PAGEMAP_ENTRIES];
	}
	else {
	    checksum = pager->checksum[f_page];
	}

	mutex_unlock(&pager->lock);
	return (checksum);
}

/*
 * Remember the checksum for a block.
 */
int
pager_put_checksum(pager, offset, checksum)
	register dpager_t	pager;
	vm_offset_t		offset;
	int			checksum;
{
	register vm_offset_t	f_page;
	static char		here[] = "%spager_put_checksum";

	f_page = atop(offset);

	mutex_lock(&pager->lock);		/* XXX lock_read */
	if (f_page >= pager->size)
	    panic(here,my_name);

	if (INDIRECT_PAGEMAP(pager->size)) {
	    register vm_offset_t *mapptr;

	    mapptr = (vm_offset_t *)pager->checksum[f_page/PAGEMAP_ENTRIES];
	    if (mapptr == 0)
		panic(here,my_name);

	    mapptr[f_page%PAGEMAP_ENTRIES] = checksum;
	}
	else {
	    pager->checksum[f_page] = checksum;
	}
	mutex_unlock(&pager->lock);
}

/*
 * Compute a checksum - XOR each 32-bit word.
 */
int
compute_checksum(addr, size)
	vm_offset_t	addr;
	vm_size_t	size;
{
	register int	checksum = NO_CHECKSUM;
	register int	*ptr;
	register int	count;

	ptr = (int *)addr;
	count = size / sizeof(int);

	while (--count >= 0)
	    checksum ^= *ptr++;

	return (checksum);
}
#endif	 /* CHECKSUM */

/*
 * Given an offset within a paging object, find the
 * corresponding block within the paging partition.
 * Allocate a new block if necessary.
 *
 * WARNING: paging objects apparently may be extended
 * without notice!
 */
union dp_map
pager_write_offset(pager, offset)
	register dpager_t	pager;
	vm_offset_t		offset;
{
	register vm_offset_t	f_page;
	register dp_map_t	mapptr;
	register union dp_map	block;

	invalidate_block(block);

	f_page = atop(offset);

#if	DEBUG_READER_CONFLICTS
	if (pager->readers > 0)
	    default_pager_read_conflicts++;	/* would have proceeded with
						   read/write lock */
#endif
	mutex_lock(&pager->lock);		/* XXX lock_read */
#if	DEBUG_READER_CONFLICTS
	pager->readers++;
#endif

	/* Catch the case where we had no initial fit partition
	   for this object, but one was added later on */
	if (no_partition(pager->cur_partition)) {
		p_index_t	new_part;
		vm_size_t	size;

		size = (f_page > pager->size) ? f_page : pager->size;
		new_part = choose_partition(ptoa(size), P_INDEX_INVALID);
		if (no_partition(new_part))
			new_part = choose_partition(ptoa(1), P_INDEX_INVALID);
		if (no_partition(new_part))
			/* give up right now to avoid confusion */
			goto out;
		else
			pager->cur_partition = new_part;
	}

	while (f_page >= pager->size) {
	  ddprintf ("pager_write_offset: extending: %x %x\n", f_page, pager->size);

	    /*
	     * Paging object must be extended.
	     * Remember that offset is 0-based, but size is 1-based.
	     */
#if	DEBUG_READER_CONFLICTS
	    pager->readers--;
#endif
	    mutex_unlock(&pager->lock);
	    pager_extend(pager, f_page + 1);
#if	DEBUG_READER_CONFLICTS
	    if (pager->readers > 0)
		default_pager_read_conflicts++;	/* would have proceeded with
						   read/write lock */
#endif
	    mutex_lock(&pager->lock);		/* XXX lock_read */
#if	DEBUG_READER_CONFLICTS
	    pager->readers++;
#endif
	    ddprintf ("pager_write_offset: done extending: %x %x\n", f_page, pager->size);
	}

	if (INDIRECT_PAGEMAP(pager->size)) {
	  ddprintf ("pager_write_offset: indirect\n");
	    mapptr = pager->map[f_page/PAGEMAP_ENTRIES].indirect;
	    if (mapptr == 0) {
		/*
		 * Allocate the indirect block
		 */
		register int i;
		ddprintf ("pager_write_offset: allocating indirect\n");

		mapptr = (dp_map_t) kalloc(PAGEMAP_SIZE(PAGEMAP_ENTRIES));
		if (mapptr == 0) {
		    /* out of space! */
		    no_paging_space(TRUE);
		    goto out;
		}
		pager->map[f_page/PAGEMAP_ENTRIES].indirect = mapptr;
		for (i = 0; i < PAGEMAP_ENTRIES; i++)
		    invalidate_block(mapptr[i]);
#ifdef	CHECKSUM
		{
		    register vm_offset_t *cksumptr;
		    register int j;

		    cksumptr = (vm_offset_t *)
				kalloc(PAGEMAP_SIZE(PAGEMAP_ENTRIES));
		    if (cksumptr == 0) {
			/* out of space! */
			no_paging_space(TRUE);
			goto out;
		    }
		    pager->checksum[f_page/PAGEMAP_ENTRIES]
			= (vm_offset_t)cksumptr;
		    for (j = 0; j < PAGEMAP_ENTRIES; j++)
			cksumptr[j] = NO_CHECKSUM;
		}
#endif	 /* CHECKSUM */
	    }
	    f_page %= PAGEMAP_ENTRIES;
	}
	else {
	    mapptr = pager->map;
	}

	block = mapptr[f_page];
	ddprintf ("pager_write_offset: block starts as %x[%x] %x\n", mapptr, f_page, block);
	if (no_block(block)) {
	    vm_offset_t	off;

	    /* get room now */
	    off = pager_alloc_page(pager->cur_partition, TRUE);
	    if (off == NO_BLOCK) {
		/*
		 * Before giving up, try all other partitions.
		 */
		p_index_t	new_part;

		ddprintf ("pager_write_offset: could not allocate block\n");
		/* returns it locked (if any one is non-full) */
		new_part = choose_partition( ptoa(1), pager->cur_partition);
		if ( ! no_partition(new_part) ) {

#if debug
dprintf("%s partition %x filled,", my_name, pager->cur_partition);
dprintf("extending object %x (size %x) to %x.\n",
	pager, pager->size, new_part);
#endif

		    /* this one tastes better */
		    pager->cur_partition = new_part;

		    /* this unlocks the partition too */
		    off = pager_alloc_page(pager->cur_partition, FALSE);

		}

		if (off == NO_BLOCK) {
		    /*
		     * Oh well.
		     */
		    overcommitted(FALSE, 1);
		    goto out;
		}
		ddprintf ("pager_write_offset: decided to allocate block\n");
	    }
	    block.block.p_offset = off;
	    block.block.p_index  = pager->cur_partition;
	    mapptr[f_page] = block;
	    ddprintf ("pager_write_offset: mapptr %x [3b] = %x\n", mapptr,
		     mapptr[0x3b]);
	    ddprintf ("pager_write_offset: block is finally %x\n", block);
	}

out:

#if	DEBUG_READER_CONFLICTS
	pager->readers--;
#endif
	mutex_unlock(&pager->lock);
	return (block);
}

/*
 * Deallocate all of the blocks belonging to a paging object.
 * No locking needed because no other operations can be in progress.
 */
void
pager_dealloc(pager)
	register dpager_t	pager;
{
	register int i, j;
	register dp_map_t	mapptr;
	register union dp_map	block;

	if (INDIRECT_PAGEMAP(pager->size)) {
	    for (i = INDIRECT_PAGEMAP_ENTRIES(pager->size); --i >= 0; ) {
		mapptr = pager->map[i].indirect;
		if (mapptr != 0) {
		    for (j = 0; j < PAGEMAP_ENTRIES; j++) {
			block = mapptr[j];
			if ( ! no_block(block) )
			    pager_dealloc_page(block.block.p_index,
			    			block.block.p_offset, TRUE);
		    }
		    kfree((char *)mapptr, PAGEMAP_SIZE(PAGEMAP_ENTRIES));
		}
	    }
	    kfree((char *)pager->map, INDIRECT_PAGEMAP_SIZE(pager->size));
#ifdef	CHECKSUM
	    for (i = INDIRECT_PAGEMAP_ENTRIES(pager->size); --i >= 0; ) {
		mapptr = (vm_offset_t *)pager->checksum[i];
		if (mapptr) {
		    kfree((char *)mapptr, PAGEMAP_SIZE(PAGEMAP_ENTRIES));
		}
	    }
	    kfree((char *)pager->checksum,
		  INDIRECT_PAGEMAP_SIZE(pager->size));
#endif	 /* CHECKSUM */
	}
	else {
	    mapptr = pager->map;
	    for (i = 0; i < pager->size; i++ ) {
		block = mapptr[i];
		if ( ! no_block(block) )
		    pager_dealloc_page(block.block.p_index,
		    			block.block.p_offset, TRUE);
	    }
	    kfree((char *)pager->map, PAGEMAP_SIZE(pager->size));
#ifdef	CHECKSUM
	    kfree((char *)pager->checksum, PAGEMAP_SIZE(pager->size));
#endif	 /* CHECKSUM */
	}
}

/*
 * Move all the pages of a PAGER that live in a
 * partition PINDEX somewhere else.
 * Pager should be write-locked, partition too.
 * Returns FALSE if it could not do it, but
 * some pages might have been moved nonetheless.
 */
boolean_t
pager_realloc(pager, pindex)
	register dpager_t	pager;
	p_index_t		pindex;
{
	register dp_map_t	map, emap;
	vm_size_t		size;
	union dp_map		block;

	size = pager->size;	/* in pages */
	map = pager->map;

	if (INDIRECT_PAGEMAP(size)) {
		for (emap = &map[INDIRECT_PAGEMAP_ENTRIES(size)];
		     map < emap; map++) {

			register dp_map_t	map2, emap2;

			if ((map2 = map->indirect) == 0)
				continue;

			for (emap2 = &map2[PAGEMAP_ENTRIES];
			     map2 < emap2; map2++)
				if ( map2->block.p_index == pindex) {

					block = pager_move_page(*map2);
					if (!no_block(block))
						*map2 = block;
					else
						return FALSE;
				}

		}
		goto ok;
	}

	/* A small one */
	for (emap = &map[size]; map < emap; map++)
		if (map->block.p_index == pindex) {
			block = pager_move_page(*map);
			if (!no_block(block))
				*map = block;
			else
				return FALSE;
		}
ok:
	pager->cur_partition = choose_partition(0, P_INDEX_INVALID);
	return TRUE;
}

/*

 */

/*
 * Read/write routines.
 */
#define	PAGER_SUCCESS	0
#define	PAGER_ABSENT	1
#define	PAGER_ERROR	2

/*
 * Read data from a default pager.  Addr is the address of a buffer
 * to fill.  Out_addr returns the buffer that contains the data;
 * if it is different from <addr>, it must be deallocated after use.
 */
int
default_read(ds, addr, size, offset, out_addr, deallocate, external)
	register dpager_t	ds;
	vm_offset_t		addr;	/* pointer to block to fill */
	register vm_size_t	size;
	register vm_offset_t	offset;
	vm_offset_t		*out_addr;
				/* returns pointer to data */
	boolean_t		deallocate;
	boolean_t		external;
{
	register union dp_map	block;
	vm_offset_t	raddr;
	vm_size_t	rsize;
	register int	rc;
	boolean_t	first_time;
	register partition_t	part;
#ifdef	CHECKSUM
	vm_size_t	original_size = size;
#endif	 /* CHECKSUM */
	vm_offset_t	original_offset = offset;

	/*
	 * Find the block in the paging partition
	 */
	block = pager_read_offset(ds, offset);
	if ( no_block(block) ) {
	    if (external) {
		/* 
		 * An external object is requesting unswapped data,
		 * zero fill the page and return.
		 */ 
		bzero((char *) addr, vm_page_size);
		*out_addr = addr;
		return (PAGER_SUCCESS);
	    }
	    return (PAGER_ABSENT);
	}

	/*
	 * Read it, trying for the entire page.
	 */
	offset = ptoa(block.block.p_offset);
ddprintf ("default_read(%x,%x,%x,%d)\n",addr,size,offset,block.block.p_index);
	part   = partition_of(block.block.p_index);
	first_time = TRUE;
	*out_addr = addr;

	do {
	    rc = page_read_file_direct(part->file,
				       offset,
				       size,
				       &raddr,
				       &rsize);
	    if (rc != 0)
		return (PAGER_ERROR);

	    /*
	     * If we got the entire page on the first read, return it.
	     */
	    if (first_time && rsize == size) {
		*out_addr = raddr;
		break;
	    }
	    /*
	     * Otherwise, copy the data into the
	     * buffer we were passed, and try for
	     * the next piece.
	     */
	    first_time = FALSE;
	    bcopy((char *)raddr, (char *)addr, rsize);
	    addr += rsize;
	    offset += rsize;
	    size -= rsize;
	} while (size != 0);

#if	USE_PRECIOUS
	if (deallocate)
		pager_release_offset(ds, original_offset);
#endif	/*USE_PRECIOUS*/

#ifdef	CHECKSUM
	{
	    int	write_checksum,
		read_checksum;

	    write_checksum = pager_get_checksum(ds, original_offset);
	    read_checksum = compute_checksum(*out_addr, original_size);
	    if (write_checksum != read_checksum) {
		panic(
  "PAGER CHECKSUM ERROR: offset 0x%x, written 0x%x, read 0x%x",
		    original_offset, write_checksum, read_checksum);
	    }
	}
#endif	 /* CHECKSUM */
	return (PAGER_SUCCESS);
}

int
default_write(ds, addr, size, offset)
	register dpager_t	ds;
	register vm_offset_t	addr;
	register vm_size_t	size;
	register vm_offset_t	offset;
{
	register union dp_map	block;
	partition_t		part;
	vm_size_t		wsize;
	register int		rc;

	ddprintf ("default_write: pager offset %x\n", offset);

	/*
	 * Find block in paging partition
	 */
	block = pager_write_offset(ds, offset);
	if ( no_block(block) )
	    return (PAGER_ERROR);

#ifdef	CHECKSUM
	/*
	 * Save checksum
	 */
	{
	    int	checksum;

	    checksum = compute_checksum(addr, size);
	    pager_put_checksum(ds, offset, checksum);
	}
#endif	 /* CHECKSUM */
	offset = ptoa(block.block.p_offset);
ddprintf ("default_write(%x,%x,%x,%d)\n",addr,size,offset,block.block.p_index);
	part   = partition_of(block.block.p_index);

	/*
	 * There are various assumptions made here,we
	 * will not get into the next disk 'block' by
	 * accident. It might well be non-contiguous.
	 */
	do {
	    rc = page_write_file_direct(part->file,
					offset,
					addr,
					size,
					&wsize);
	    if (rc != 0) {
		dprintf("*** PAGER ERROR: default_write: ");
		dprintf("ds=0x%x addr=0x%x size=0x%x offset=0x%x resid=0x%x\n",
			ds, addr, size, offset, wsize);
		return (PAGER_ERROR);
	    }
	    addr += wsize;
	    offset += wsize;
	    size -= wsize;
	} while (size != 0);
	return (PAGER_SUCCESS);
}

boolean_t
default_has_page(ds, offset)
	dpager_t	ds;
	vm_offset_t	offset;
{
	return ( ! no_block(pager_read_offset(ds, offset)) );
}
/*

 */

/*
 * Mapping between pager port and paging object.
 */
struct dstruct {
	queue_chain_t	links;		/* Link in pager-port list */

	struct mutex	lock;		/* Lock for the structure */
	struct condition
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

#if	PARALLEL
#define	dstruct_lock_init(ds)	mutex_init(&ds->lock)
#define	dstruct_lock(ds)	mutex_lock(&ds->lock)
#define	dstruct_unlock(ds)	mutex_unlock(&ds->lock)
#else	/* PARALLEL */
#define	dstruct_lock_init(ds)
#define	dstruct_lock(ds)
#define	dstruct_unlock(ds)
#endif	/* PARALLEL */

/*
 * List of all pagers.  A specific pager is
 * found directly via its port, this list is
 * only used for monitoring purposes by the
 * default_pager_object* calls
 */
struct pager_port {
	queue_head_t	queue;
	struct mutex	lock;
	int		count;	/* saves code */
	queue_head_t	leak_queue;
} all_pagers;

#define pager_port_list_init()					\
{								\
	mutex_init(&all_pagers.lock);				\
	queue_init(&all_pagers.queue);				\
	queue_init(&all_pagers.leak_queue);			\
	all_pagers.count = 0;					\
}

void pager_port_list_insert(port, ds)
	mach_port_t port;
	default_pager_t	ds;
{
	mutex_lock(&all_pagers.lock);
	queue_enter(&all_pagers.queue, ds, default_pager_t, links);
	all_pagers.count++;
	mutex_unlock(&all_pagers.lock);
}

/* given a data structure return a good port-name to associate it to */
#define	pnameof(_x_)	(((vm_offset_t)(_x_))+1)
/* reverse, assumes no-odd-pointers */
#define	dnameof(_x_)	(((vm_offset_t)(_x_))&~1)

/* The magic typecast */
#define pager_port_lookup(_port_)					\
	((! MACH_PORT_VALID(_port_) ||					\
	 ((default_pager_t)dnameof(_port_))->pager != (_port_)) ?	\
		DEFAULT_PAGER_NULL : (default_pager_t)dnameof(_port_))

void pager_port_list_delete(ds)
	default_pager_t ds;
{
	mutex_lock(&all_pagers.lock);
	queue_remove(&all_pagers.queue, ds, default_pager_t, links);
	all_pagers.count--;
	mutex_unlock(&all_pagers.lock);
}

/*
 * Destroy a paging partition.
 * XXX this is not re-entrant XXX
 */
kern_return_t
destroy_paging_partition(name, pp_private)
	char		*name;
	void **pp_private;
{
	register unsigned int	id = part_id(name);
	register partition_t	part;
	boolean_t		all_ok = TRUE;
	default_pager_t		entry;
	int			pindex;

	/*
	 * Find and take partition out of list
	 * This prevents choose_partition from
	 * getting in the way.
	 */
	mutex_lock(&all_partitions.lock);
	for (pindex = 0; pindex < all_partitions.n_partitions; pindex++) {
		part = partition_of(pindex);
		if (part && (part->id == id)) break;
	}
	if (pindex == all_partitions.n_partitions) {
		mutex_unlock(&all_partitions.lock);
		return KERN_INVALID_ARGUMENT;
	}
	part->going_away = TRUE;
	mutex_unlock(&all_partitions.lock);

	/*
	 * This might take a while..
	 */
all_over_again:
#if debug
dprintf("Partition x%x (id x%x) for %s, all_ok %d\n", part, id, name, all_ok);
#endif
	all_ok = TRUE;
	mutex_lock(&part->p_lock);

	mutex_lock(&all_pagers.lock);
	queue_iterate(&all_pagers.queue, entry, default_pager_t, links) {

		dstruct_lock(entry);

		if (!mutex_try_lock(&entry->dpager.lock)) {

			dstruct_unlock(entry);
			mutex_unlock(&all_pagers.lock);
			mutex_unlock(&part->p_lock);

			/* yield the processor */
			(void) thread_switch(MACH_PORT_NULL,
					     SWITCH_OPTION_NONE, 0);

			goto all_over_again;

		}

		/*
		 * See if we can relocate all the pages of this object
		 * currently on this partition on some other partition
		 */
		all_ok = pager_realloc(&entry->dpager, pindex);

		mutex_unlock(&entry->dpager.lock);
		dstruct_unlock(entry);

		if (!all_ok)  break;

	}
	mutex_unlock(&all_pagers.lock);

	if (all_ok) {
		/* No need to unlock partition, there are no refs left */

		set_partition_of(pindex, 0);
		*pp_private = part->file;
		kfree(part->bitmap, howmany(part->total_size, NB_BM) * sizeof(bm_entry_t));
		kfree(part, sizeof(struct part));
		dprintf("%s Removed paging partition %s\n", my_name, name);
		return KERN_SUCCESS;
	}

	/*
	 * Put partition back in.
	 */
	part->going_away = FALSE;

	return KERN_FAILURE;
}


/*
 *	We use the sequence numbers on requests to regulate
 *	our parallelism.  In general, we allow multiple reads and writes
 *	to proceed in parallel, with the exception that reads must
 *	wait for previous writes to finish.  (Because the kernel might
 *	generate a data-request for a page on the heels of a data-write
 *	for the same page, and we must avoid returning stale data.)
 *	terminate requests wait for proceeding reads and writes to finish.
 */

unsigned int default_pager_total = 0;		/* debugging */
unsigned int default_pager_wait_seqno = 0;	/* debugging */
unsigned int default_pager_wait_read = 0;	/* debugging */
unsigned int default_pager_wait_write = 0;	/* debugging */
unsigned int default_pager_wait_refs = 0;	/* debugging */

#if	PARALLEL
/*
 * Waits for correct sequence number.  Leaves pager locked.
 */
void pager_port_lock(ds, seqno)
	default_pager_t ds;
	mach_port_seqno_t seqno;
{
	default_pager_total++;
ddprintf ("pager_port_lock <%p>: <%p>: %d: 1\n", &ds, ds, seqno);
	dstruct_lock(ds);
ddprintf ("pager_port_lock <%p>: <%p>: %d: 2\n", &ds, ds, seqno);
	while (ds->seqno != seqno) {
ddprintf ("pager_port_lock <%p>: <%p>: %d: 3\n", &ds, ds, seqno);
		default_pager_wait_seqno++;
		condition_wait(&ds->waiting_seqno, &ds->lock);
ddprintf ("pager_port_lock <%p>: <%p>: %d: 4\n", &ds, ds, seqno);
	}
}

/*
 * Increments sequence number and unlocks pager.
 */
void pager_port_unlock(ds)
	default_pager_t ds;
{
	ds->seqno++;
ddprintf ("pager_port_unlock <%p>: <%p>: seqno => %d\n", &ds, ds, ds->seqno);
	dstruct_unlock(ds);
ddprintf ("pager_port_unlock <%p>: <%p>: 2\n", &ds, ds);
	condition_broadcast(&ds->waiting_seqno);
ddprintf ("pager_port_unlock <%p>: <%p>: 3\n", &ds, ds);
}

/*
 * Start a read - one more reader.  Pager must be locked.
 */
void pager_port_start_read(ds)
	default_pager_t ds;
{
	ds->readers++;
}

/*
 * Wait for readers.  Unlocks and relocks pager if wait needed.
 */
void pager_port_wait_for_readers(ds)
	default_pager_t ds;
{
	while (ds->readers != 0) {
		default_pager_wait_read++;
		condition_wait(&ds->waiting_read, &ds->lock);
	}
}

/*
 * Finish a read.  Pager is unlocked and returns unlocked.
 */
void pager_port_finish_read(ds)
	default_pager_t ds;
{
	dstruct_lock(ds);
	if (--ds->readers == 0) {
		dstruct_unlock(ds);
		condition_broadcast(&ds->waiting_read);
	}
	else {
		dstruct_unlock(ds);
	}
}

/*
 * Start a write - one more writer.  Pager must be locked.
 */
void pager_port_start_write(ds)
	default_pager_t ds;
{
	ds->writers++;
}

/*
 * Wait for writers.  Unlocks and relocks pager if wait needed.
 */
void pager_port_wait_for_writers(ds)
	default_pager_t ds;
{
	while (ds->writers != 0) {
		default_pager_wait_write++;
		condition_wait(&ds->waiting_write, &ds->lock);
	}
}

/*
 * Finish a write.  Pager is unlocked and returns unlocked.
 */
void pager_port_finish_write(ds)
	default_pager_t ds;
{
	dstruct_lock(ds);
	if (--ds->writers == 0) {
		dstruct_unlock(ds);
		condition_broadcast(&ds->waiting_write);
	}
	else {
		dstruct_unlock(ds);
	}
}

/*
 * Wait for concurrent default_pager_objects.
 * Unlocks and relocks pager if wait needed.
 */
void pager_port_wait_for_refs(ds)
	default_pager_t ds;
{
	while (ds->name_refs == 0) {
		default_pager_wait_refs++;
		condition_wait(&ds->waiting_refs, &ds->lock);
	}
}

/*
 * Finished creating name refs - wake up waiters.
 */
void pager_port_finish_refs(ds)
	default_pager_t ds;
{
	condition_broadcast(&ds->waiting_refs);
}

#else	/* PARALLEL */

#define	pager_port_lock(ds,seqno)
#define	pager_port_unlock(ds)
#define	pager_port_start_read(ds)
#define	pager_port_wait_for_readers(ds)
#define	pager_port_finish_read(ds)
#define	pager_port_start_write(ds)
#define	pager_port_wait_for_writers(ds)
#define	pager_port_finish_write(ds)
#define pager_port_wait_for_refs(ds)
#define pager_port_finish_refs(ds)

#endif	/* PARALLEL */

/*
 *	Default pager.
 */
task_t		default_pager_self;	/* Our task port. */

mach_port_t default_pager_default_port;	/* Port for memory_object_create. */

/* We catch exceptions on ourself & startup using this port. */
mach_port_t default_pager_exception_port;

mach_port_t default_pager_internal_set;	/* Port set for internal objects. */
mach_port_t default_pager_external_set;	/* Port set for external objects. */
mach_port_t default_pager_default_set;	/* Port set for "default" thread. */

typedef struct default_pager_thread {
	cthread_t	dpt_thread;	/* Server thread. */
	vm_offset_t	dpt_buffer;	/* Read buffer. */
	boolean_t	dpt_internal;	/* Do we handle internal objects? */
} default_pager_thread_t;

#if	PARALLEL
	/* determine number of threads at run time */
#define DEFAULT_PAGER_INTERNAL_COUNT	(0)

#else	/* PARALLEL */
#define	DEFAULT_PAGER_INTERNAL_COUNT	(1)
#endif	/* PARALLEL */

/* Memory created by default_pager_object_create should mostly be resident. */
#define DEFAULT_PAGER_EXTERNAL_COUNT	(1)

unsigned int default_pager_internal_count = DEFAULT_PAGER_INTERNAL_COUNT;
					/* Number of "internal" threads. */
unsigned int default_pager_external_count = DEFAULT_PAGER_EXTERNAL_COUNT;
					/* Number of "external" threads. */

default_pager_t pager_port_alloc(size)
	vm_size_t size;
{
	default_pager_t ds;
	p_index_t	part;

	ds = (default_pager_t) kalloc(sizeof *ds);
	if (ds == DEFAULT_PAGER_NULL)
	    panic("%spager_port_alloc",my_name);
	bzero((char *) ds, sizeof *ds);

	dstruct_lock_init(ds);

	/*
	 * Get a suitable partition.  If none big enough
	 * just pick one and overcommit.  If no partitions
	 * at all.. well just fake one so that we will
	 * kill specific objects on pageouts rather than
	 * panicing the system now.
	 */
	part = choose_partition(size, P_INDEX_INVALID);
	if (no_partition(part)) {
		overcommitted(FALSE, atop(size));
		part = choose_partition(0,P_INDEX_INVALID);
#if debug
		if (no_partition(part))
			dprintf("%s No paging space at all !!\n", my_name);
#endif
	}
	pager_alloc(&ds->dpager, part, size);

	return ds;
}

mach_port_urefs_t default_pager_max_urefs = 10000;

/*
 * Check user reference count on pager_request port.
 * Pager must be locked.
 * Unlocks and re-locks pager if needs to call kernel.
 */
void pager_port_check_request(ds, pager_request)
	default_pager_t ds;
	mach_port_t pager_request;
{
	mach_port_delta_t delta;
	kern_return_t kr;

	assert(ds->pager_request == pager_request);

	if (++ds->request_refs > default_pager_max_urefs) {
		delta = 1 - ds->request_refs;
		ds->request_refs = 1;

		dstruct_unlock(ds);

		/*
		 *	Deallocate excess user references.
		 */

		kr = mach_port_mod_refs(default_pager_self, pager_request,
					MACH_PORT_RIGHT_SEND, delta);
		if (kr != KERN_SUCCESS)
			panic("%spager_port_check_request",my_name);

		dstruct_lock(ds);
	}
}

void default_pager_add(ds, internal)
	default_pager_t ds;
	boolean_t internal;
{
	mach_port_t		pager = ds->pager;
	mach_port_t		pset;
	mach_port_mscount_t 	sync;
	mach_port_t		previous;
	kern_return_t		kr;
	static char		here[] = "%sdefault_pager_add";

	/*
	 *	The port currently has a make-send count of zero,
	 *	because either we just created the port or we just
	 *	received the port in a memory_object_create request.
	 */

	if (internal) {
		/* possibly generate an immediate no-senders notification */
		sync = 0;
		pset = default_pager_internal_set;
		ds->external = FALSE;
	} else {
		/* delay notification till send right is created */
		sync = 1;
		pset = default_pager_external_set;
		ds->external = TRUE;
	}

	kr = mach_port_request_notification(default_pager_self, pager,
				MACH_NOTIFY_NO_SENDERS, sync,
				pager, MACH_MSG_TYPE_MAKE_SEND_ONCE,
				&previous);
	if ((kr != KERN_SUCCESS) || (previous != MACH_PORT_NULL))
		panic(here,my_name);

	kr = mach_port_move_member(default_pager_self, pager, pset);
	if (kr != KERN_SUCCESS)
		panic(here,my_name);
}

/*
 *	Routine:	memory_object_create
 *	Purpose:
 *		Handle requests for memory objects from the
 *		kernel.
 *	Notes:
 *		Because we only give out the default memory
 *		manager port to the kernel, we don't have to
 *		be so paranoid about the contents.
 */
kern_return_t
seqnos_memory_object_create(old_pager, seqno, new_pager, new_size,
			    new_pager_request, new_pager_name, new_page_size)
	mach_port_t	old_pager;
	mach_port_seqno_t seqno;
	mach_port_t	new_pager;
	vm_size_t	new_size;
	mach_port_t	new_pager_request;
	mach_port_t	new_pager_name;
	vm_size_t	new_page_size;
{
	register default_pager_t	ds;
	kern_return_t			kr;

	assert(old_pager == default_pager_default_port);
	assert(MACH_PORT_VALID(new_pager_request));
	assert(MACH_PORT_VALID(new_pager_name));
	assert(new_page_size == vm_page_size);

	ds = pager_port_alloc(new_size);
rename_it:
	kr = mach_port_rename(	default_pager_self,
				new_pager, (mach_port_t)pnameof(ds));
	if (kr != KERN_SUCCESS) {
		default_pager_t	ds1;

		if (kr != KERN_NAME_EXISTS)
			panic("%s m_o_create", my_name);
		ds1 = (default_pager_t) kalloc(sizeof *ds1);
		*ds1 = *ds;
		mutex_lock(&all_pagers.lock);
		queue_enter(&all_pagers.leak_queue, ds, default_pager_t, links);
		mutex_unlock(&all_pagers.lock);
		ds = ds1;
		goto rename_it;
	}

	new_pager = (mach_port_t) pnameof(ds);

	/*
	 *	Set up associations between these ports
	 *	and this default_pager structure
	 */

	ds->pager = new_pager;
	ds->pager_request = new_pager_request;
	ds->request_refs = 1;
	ds->pager_name = new_pager_name;
	ds->name_refs = 1;

	/*
	 *	After this, other threads might receive requests
	 *	for this memory object or find it in the port list.
	 */

	pager_port_list_insert(new_pager, ds);
	default_pager_add(ds, TRUE);

	return(KERN_SUCCESS);
}

memory_object_copy_strategy_t default_pager_copy_strategy =
					MEMORY_OBJECT_COPY_DELAY;

kern_return_t
seqnos_memory_object_init(pager, seqno, pager_request, pager_name,
			  pager_page_size)
	mach_port_t	pager;
	mach_port_seqno_t seqno;
	mach_port_t	pager_request;
	mach_port_t	pager_name;
	vm_size_t	pager_page_size;
{
	register default_pager_t ds;
	kern_return_t		 kr;
	static char		 here[] = "%sinit";

	assert(MACH_PORT_VALID(pager_request));
	assert(MACH_PORT_VALID(pager_name));
	assert(pager_page_size == vm_page_size);

	ds = pager_port_lookup(pager);
	if (ds == DEFAULT_PAGER_NULL)
	    panic(here, my_name);
	pager_port_lock(ds, seqno);

	if (ds->pager_request != MACH_PORT_NULL)
	    panic(here, my_name);

	ds->pager_request = pager_request;
	ds->request_refs = 1;
	ds->pager_name = pager_name;
	ds->name_refs = 1;

	/*
	 *	Even if the kernel immediately terminates the object,
	 *	the pager_request port won't be destroyed until
	 *	we process the terminate request, which won't happen
	 *	until we unlock the object.
	 */

	kr = memory_object_set_attributes(pager_request,
					  TRUE,
					  FALSE,	/* do not cache */
					  default_pager_copy_strategy);
	if (kr != KERN_SUCCESS)
	    panic(here, my_name);

	pager_port_unlock(ds);

	return(KERN_SUCCESS);
}

kern_return_t
seqnos_memory_object_terminate(pager, seqno, pager_request, pager_name)
	mach_port_t	pager;
	mach_port_seqno_t seqno;
	mach_port_t	pager_request;
	mach_port_t	pager_name;
{
	register default_pager_t	ds;
	mach_port_urefs_t		request_refs, name_refs;
	kern_return_t			kr;
	static char			here[] = "%sterminate";

	/*
	 *	pager_request and pager_name are receive rights,
	 *	not send rights.
	 */

	ds = pager_port_lookup(pager);
	if (ds == DEFAULT_PAGER_NULL)
		panic(here, my_name);
ddprintf ("seqnos_memory_object_terminate <%p>: pager_port_lock: <%p>[s:%d,r:%d,w:%d,l:%d], %d\n",
	&kr, ds, ds->seqno, ds->readers, ds->writers, ds->lock.held, seqno);
	pager_port_lock(ds, seqno);

	/*
	 *	Wait for read and write requests to terminate.
	 */

	pager_port_wait_for_readers(ds);
	pager_port_wait_for_writers(ds);

	/*
	 *	After memory_object_terminate both memory_object_init
	 *	and a no-senders notification are possible, so we need
	 *	to clean up the request and name ports but leave
	 *	the pager port.
	 *
	 *	A concurrent default_pager_objects might be allocating
	 *	more references for the name port.  In this case,
	 *	we must first wait for it to finish.
	 */

	pager_port_wait_for_refs(ds);

	ds->pager_request = MACH_PORT_NULL;
	request_refs = ds->request_refs;
	ds->request_refs = 0;
	assert(ds->pager_name == pager_name);
	ds->pager_name = MACH_PORT_NULL;
	name_refs = ds->name_refs;
	ds->name_refs = 0;
ddprintf ("seqnos_memory_object_terminate <%p>: pager_port_unlock: <%p>[s:%d,r:%d,w:%d,l:%d]\n",
	&kr, ds, ds->seqno, ds->readers, ds->writers, ds->lock.held);
	pager_port_unlock(ds);

	/*
	 *	Now we deallocate our various port rights.
	 */

	kr = mach_port_mod_refs(default_pager_self, pager_request,
				MACH_PORT_RIGHT_SEND, -request_refs);
	if (kr != KERN_SUCCESS)
	    panic(here,my_name);

	kr = mach_port_mod_refs(default_pager_self, pager_request,
				MACH_PORT_RIGHT_RECEIVE, -1);
	if (kr != KERN_SUCCESS)
	    panic(here,my_name);

	kr = mach_port_mod_refs(default_pager_self, pager_name,
				MACH_PORT_RIGHT_SEND, -name_refs);
	if (kr != KERN_SUCCESS)
	    panic(here,my_name);

	kr = mach_port_mod_refs(default_pager_self, pager_name,
				MACH_PORT_RIGHT_RECEIVE, -1);
	if (kr != KERN_SUCCESS)
	    panic(here,my_name);

	return (KERN_SUCCESS);
}

void default_pager_no_senders(pager, seqno, mscount)
	memory_object_t pager;
	mach_port_seqno_t seqno;
	mach_port_mscount_t mscount;
{
	register default_pager_t ds;
	kern_return_t		 kr;
	static char		 here[] = "%sno_senders";

	/*
	 *	Because we don't give out multiple send rights
	 *	for a memory object, there can't be a race
	 *	between getting a no-senders notification
	 *	and creating a new send right for the object.
	 *	Hence we don't keep track of mscount.
	 */


	ds = pager_port_lookup(pager);
	if (ds == DEFAULT_PAGER_NULL)
		panic(here,my_name);
	pager_port_lock(ds, seqno);

	/*
	 *	We shouldn't get a no-senders notification
	 *	when the kernel has the object cached.
	 */

	if (ds->pager_request != MACH_PORT_NULL)
		panic(here,my_name);

	/*
	 *	Unlock the pager (though there should be no one
	 *	waiting for it).
	 */
	dstruct_unlock(ds);

	/*
	 *	Remove the memory object port association, and then
	 *	the destroy the port itself.  We must remove the object
	 *	from the port list before deallocating the pager,
	 *	because of default_pager_objects.
	 */

	pager_port_list_delete(ds);
	pager_dealloc(&ds->dpager);

	kr = mach_port_mod_refs(default_pager_self, pager,
				MACH_PORT_RIGHT_RECEIVE, -1);
	if (kr != KERN_SUCCESS)
		panic(here,my_name);

	/*
	 * Do this *after* deallocating the port name
	 */
	kfree((char *) ds, sizeof(*ds));

	/*
	 * Recover memory that we might have wasted because
	 * of name conflicts
	 */
	mutex_lock(&all_pagers.lock);

	while (!queue_empty(&all_pagers.leak_queue)) {

		ds = (default_pager_t) queue_first(&all_pagers.leak_queue);
		queue_remove_first(&all_pagers.leak_queue, ds, default_pager_t, links);
		kfree((char *) ds, sizeof(*ds));
	}

	mutex_unlock(&all_pagers.lock);
}

int		default_pager_pagein_count = 0;
int		default_pager_pageout_count = 0;

kern_return_t
seqnos_memory_object_data_request(pager, seqno, reply_to, offset,
				  length, protection_required)
	memory_object_t	pager;
	mach_port_seqno_t seqno;
	mach_port_t	reply_to;
	vm_offset_t	offset;
	vm_size_t	length;
	vm_prot_t	protection_required;
{
	default_pager_thread_t	*dpt;
	default_pager_t		ds;
	vm_offset_t		addr;
	unsigned int 		errors;
	kern_return_t		rc;
	static char		here[] = "%sdata_request";

	dpt = (default_pager_thread_t *) cthread_data(cthread_self());

	if (length != vm_page_size)
	    panic(here,my_name);

	ds = pager_port_lookup(pager);
	if (ds == DEFAULT_PAGER_NULL)
	    panic(here,my_name);
ddprintf ("seqnos_memory_object_data_request <%p>: pager_port_lock: <%p>[s:%d,r:%d,w:%d,l:%d], %d\n",
	&ds, ds, ds->seqno, ds->readers, ds->writers, ds->lock.held, seqno);
	pager_port_lock(ds, seqno);
	pager_port_check_request(ds, reply_to);
	pager_port_wait_for_writers(ds);
	pager_port_start_read(ds);

	/*
	 * Get error count while pager locked.
	 */
	errors = ds->errors;

ddprintf ("seqnos_memory_object_data_request <%p>: pager_port_unlock: <%p>[s:%d,r:%d,w:%d,l:%d]\n",
	&ds, ds, ds->seqno, ds->readers, ds->writers, ds->lock.held);
	pager_port_unlock(ds);

	if (errors) {
	    dprintf("%s %s\n", my_name,
		   "dropping data_request because of previous paging errors");
	    (void) memory_object_data_error(reply_to,
				offset, vm_page_size,
				KERN_FAILURE);
	    goto done;
	}

	if (offset >= ds->dpager.limit)
	  rc = PAGER_ERROR;
	else
	  rc = default_read(&ds->dpager, dpt->dpt_buffer,
			    vm_page_size, offset,
			    &addr, protection_required & VM_PROT_WRITE,
			    ds->external);

	switch (rc) {
	    case PAGER_SUCCESS:
		if (addr != dpt->dpt_buffer) {
		    /*
		     *	Deallocates data buffer
		     */
		    (void) memory_object_data_supply(
		        reply_to, offset,
			addr, vm_page_size, TRUE,
			VM_PROT_NONE,
			FALSE, MACH_PORT_NULL);
		} else {
		    (void) memory_object_data_provided(
			reply_to, offset,
			addr, vm_page_size,
			VM_PROT_NONE);
		}
		break;

	    case PAGER_ABSENT:
		(void) memory_object_data_unavailable(
			reply_to,
			offset,
			vm_page_size);
		break;

	    case PAGER_ERROR:
		(void) memory_object_data_error(
			reply_to,
			offset,
			vm_page_size,
			KERN_FAILURE);
		break;
	}

	default_pager_pagein_count++;

    done:
	pager_port_finish_read(ds);
	return(KERN_SUCCESS);
}

/*
 * memory_object_data_initialize: check whether we already have each page, and
 * write it if we do not.  The implementation is far from optimized, and
 * also assumes that the default_pager is single-threaded.
 */
kern_return_t
seqnos_memory_object_data_initialize(pager, seqno, pager_request,
				     offset, addr, data_cnt)
	memory_object_t	pager;
	mach_port_seqno_t seqno;
	mach_port_t	pager_request;
	register
	vm_offset_t	offset;
	register
	pointer_t	addr;
	vm_size_t	data_cnt;
{
	vm_offset_t	amount_sent;
	default_pager_t	ds;
	static char	here[] = "%sdata_initialize";

#ifdef	lint
	pager_request++;
#endif	 /* lint */

	ds = pager_port_lookup(pager);
	if (ds == DEFAULT_PAGER_NULL)
	    panic(here,my_name);
ddprintf ("seqnos_memory_object_data_initialize <%p>: pager_port_lock: <%p>[s:%d,r:%d,w:%d,l:%d], %d\n",
	&ds, ds, ds->seqno, ds->readers, ds->writers, ds->lock.held, seqno);
	pager_port_lock(ds, seqno);
	pager_port_check_request(ds, pager_request);
	pager_port_start_write(ds);
ddprintf ("seqnos_memory_object_data_initialize <%p>: pager_port_unlock: <%p>[s:%d,r:%d,w:%d,l:%d]\n",
	&ds, ds, ds->seqno, ds->readers, ds->writers, ds->lock.held);
	pager_port_unlock(ds);

	for (amount_sent = 0;
	     amount_sent < data_cnt;
	     amount_sent += vm_page_size) {

	     if (!default_has_page(&ds->dpager, offset + amount_sent)) {
		if (default_write(&ds->dpager,
				  addr + amount_sent,
				  vm_page_size,
				  offset + amount_sent)
			 != PAGER_SUCCESS) {
		    dprintf("%s%s write error\n", my_name, here);
		    dstruct_lock(ds);
		    ds->errors++;
		    dstruct_unlock(ds);
		}
	     }
	}

	pager_port_finish_write(ds);
	if (vm_deallocate(default_pager_self, addr, data_cnt) != KERN_SUCCESS)
	    panic(here,my_name);

	return(KERN_SUCCESS);
}

/*
 * memory_object_data_write: split up the stuff coming in from
 * a memory_object_data_write call
 * into individual pages and pass them off to default_write.
 */
kern_return_t
seqnos_memory_object_data_write(pager, seqno, pager_request,
				offset, addr, data_cnt)
	memory_object_t	pager;
	mach_port_seqno_t seqno;
	mach_port_t	pager_request;
	register
	vm_offset_t	offset;
	register
	pointer_t	addr;
	vm_size_t	data_cnt;
{
	register
	vm_size_t	amount_sent;
	default_pager_t	ds;
	static char	here[] = "%sdata_write";
	int err;

#ifdef	lint
	pager_request++;
#endif	 /* lint */

ddprintf ("seqnos_memory_object_data_write <%p>: 1\n", &err);
	if ((data_cnt % vm_page_size) != 0)
	  {
	    ddprintf ("fail 1: %d %d\n", data_cnt, vm_page_size);
	    panic(here,my_name);
	  }


ddprintf ("seqnos_memory_object_data_write <%p>: 2\n", &err);
	ds = pager_port_lookup(pager);
ddprintf ("seqnos_memory_object_data_write <%p>: 3\n", &err);
	if (ds == DEFAULT_PAGER_NULL)
	  {
	    ddprintf ("fail 2: %d %d\n", pager, ds);
	    panic(here,my_name);
	  }

ddprintf ("seqnos_memory_object_data_write <%p>: 4\n", &err);
ddprintf ("seqnos_memory_object_data_write <%p>: pager_port_lock: <%p>[s:%d,r:%d,w:%d,l:%d], %d\n",
	&err, ds, ds->seqno, ds->readers, ds->writers, ds->lock.held, seqno);
	pager_port_lock(ds, seqno);
ddprintf ("seqnos_memory_object_data_write <%p>: 5\n", &err);
	pager_port_check_request(ds, pager_request);
ddprintf ("seqnos_memory_object_data_write <%p>: 6\n", &err);
	pager_port_start_write(ds);
ddprintf ("seqnos_memory_object_data_write <%p>: 7\n", &err);
ddprintf ("seqnos_memory_object_data_write <%p>: pager_port_unlock: <%p>[s:%d,r:%d,w:%d,l:%d]\n",
	&err, ds, ds->seqno, ds->readers, ds->writers, ds->lock.held);
	pager_port_unlock(ds);

ddprintf ("seqnos_memory_object_data_write <%p>: 8\n", &err);
	for (amount_sent = 0;
	     amount_sent < data_cnt;
	     amount_sent += vm_page_size) {

	    register int result;

ddprintf ("seqnos_memory_object_data_write <%p>: 9\n", &err);
	    result = default_write(&ds->dpager,
			      addr + amount_sent,
			      vm_page_size,
			      offset + amount_sent);
ddprintf ("seqnos_memory_object_data_write <%p>: 10\n", &err);
	    if (result != KERN_SUCCESS) {
ddprintf ("seqnos_memory_object_data_write <%p>: 11\n", &err);
#if debug
		dprintf("%s WRITE ERROR on default_pageout:", my_name);
		dprintf(" pager=%x, offset=0x%x, length=0x%x, result=%d\n",
			pager, offset+amount_sent, vm_page_size, result);
#endif
		dstruct_lock(ds);
		ds->errors++;
		dstruct_unlock(ds);
	    }
	    default_pager_pageout_count++;
	}

ddprintf ("seqnos_memory_object_data_write <%p>: 12\n", &err);
	pager_port_finish_write(ds);
ddprintf ("seqnos_memory_object_data_write <%p>: 13\n", &err);
	err = vm_deallocate(default_pager_self, addr, data_cnt);
ddprintf ("seqnos_memory_object_data_write <%p>: 14\n", &err);
	if (err != KERN_SUCCESS)
	  {
	    ddprintf ("fail 3: %s %s %s %s\n", default_pager_self, addr, data_cnt, &err);

	      panic(here,my_name);
	  }


ddprintf ("seqnos_memory_object_data_write <%p>: 15\n", &err);
	return(KERN_SUCCESS);
}

/*ARGSUSED*/
kern_return_t
seqnos_memory_object_copy(old_memory_object, seqno, old_memory_control,
			  offset, length, new_memory_object)
	memory_object_t	old_memory_object;
	mach_port_seqno_t seqno;
	memory_object_control_t
			old_memory_control;
	vm_offset_t	offset;
	vm_size_t	length;
	memory_object_t	new_memory_object;
{
	panic("%scopy", my_name);
	return KERN_FAILURE;
}

/* We get this when our memory_object_lock_request has completed
   after we truncated an object.  */
kern_return_t
seqnos_memory_object_lock_completed (memory_object_t pager,
				     mach_port_seqno_t seqno,
				     mach_port_t pager_request,
				     vm_offset_t offset,
				     vm_size_t length)
{
  default_pager_t ds;

  ds = pager_port_lookup(pager);
  assert(ds != DEFAULT_PAGER_NULL);

  pager_port_lock(ds, seqno);
  pager_port_wait_for_readers(ds);
  pager_port_wait_for_writers(ds);

  /* Now that any in-core pages have been flushed, we can apply
     the limit to prevent any new page-ins.  */
  assert (page_aligned (offset));
  ds->dpager.limit = offset;

  default_pager_object_set_size_reply (ds->lock_request, KERN_SUCCESS);
  ds->lock_request = MACH_PORT_NULL;

  if (ds->dpager.size > ds->dpager.limit / vm_page_size)
    /* Deallocate the old backing store pages and shrink the page map.  */
    pager_truncate (&ds->dpager, ds->dpager.limit / vm_page_size);

  pager_port_unlock(ds, seqno);

  return KERN_SUCCESS;
}

kern_return_t
seqnos_memory_object_data_unlock(pager, seqno, pager_request,
				 offset, addr, data_cnt)
	memory_object_t	pager;
	mach_port_seqno_t seqno;
	mach_port_t	pager_request;
	vm_offset_t	offset;
	pointer_t	addr;
	vm_size_t	data_cnt;
{
	panic("%sdata_unlock",my_name);
	return(KERN_FAILURE);
}

kern_return_t
seqnos_memory_object_supply_completed(pager, seqno, pager_request,
				      offset, length,
				      result, error_offset)
	memory_object_t	pager;
	mach_port_seqno_t seqno;
	mach_port_t	pager_request;
	vm_offset_t	offset;
	vm_size_t	length;
	kern_return_t	result;
	vm_offset_t	error_offset;
{
	panic("%ssupply_completed",my_name);
	return(KERN_FAILURE);
}

kern_return_t
seqnos_memory_object_data_return(pager, seqno, pager_request,
				 offset, addr, data_cnt,
				 dirty, kernel_copy)
	memory_object_t	pager;
	mach_port_seqno_t seqno;
	mach_port_t	pager_request;
	vm_offset_t	offset;
	pointer_t	addr;
	vm_size_t	data_cnt;
	boolean_t	dirty;
	boolean_t	kernel_copy;
{
	panic("%sdata_return",my_name);
	return(KERN_FAILURE);
}

kern_return_t
seqnos_memory_object_change_completed(pager, seqno, may_cache, copy_strategy)
	memory_object_t	pager;
	mach_port_seqno_t seqno;
	boolean_t	may_cache;
	memory_object_copy_strategy_t copy_strategy;
{
	panic("%schange_completed",my_name);
	return(KERN_FAILURE);
}


boolean_t default_pager_notify_server(in, out)
	mach_msg_header_t *in, *out;
{
	register mach_no_senders_notification_t *n =
			(mach_no_senders_notification_t *) in;

	/*
	 *	The only send-once rights we create are for
	 *	receiving no-more-senders notifications.
	 *	Hence, if we receive a message directed to
	 *	a send-once right, we can assume it is
	 *	a genuine no-senders notification from the kernel.
	 */

	if ((n->not_header.msgh_bits !=
			MACH_MSGH_BITS(0, MACH_MSG_TYPE_PORT_SEND_ONCE)) ||
	    (n->not_header.msgh_id != MACH_NOTIFY_NO_SENDERS))
		return FALSE;

	assert(n->not_header.msgh_size == sizeof *n);
	assert(n->not_header.msgh_remote_port == MACH_PORT_NULL);

	assert(n->not_type.msgt_name == MACH_MSG_TYPE_INTEGER_32);
	assert(n->not_type.msgt_size == 32);
	assert(n->not_type.msgt_number == 1);
	assert(n->not_type.msgt_inline);
	assert(! n->not_type.msgt_longform);

	default_pager_no_senders(n->not_header.msgh_local_port,
				 n->not_header.msgh_seqno, n->not_count);

	out->msgh_remote_port = MACH_PORT_NULL;
	return TRUE;
}

extern boolean_t seqnos_memory_object_server();
extern boolean_t seqnos_memory_object_default_server();
extern boolean_t default_pager_server();
extern boolean_t exc_server();
extern boolean_t bootstrap_server();
extern void bootstrap_compat();

mach_msg_size_t default_pager_msg_size_object = 128;

boolean_t
default_pager_demux_object(in, out)
	mach_msg_header_t	*in;
	mach_msg_header_t	*out;
{
	/*
	 *	We receive memory_object_data_initialize messages in
	 *	the memory_object_default interface.
	 */

int rval;
ddprintf ("DPAGER DEMUX OBJECT <%p>: %d\n", in, in->msgh_id);
rval =
 (seqnos_memory_object_server(in, out) ||
		seqnos_memory_object_default_server(in, out) ||
		default_pager_notify_server(in, out) ||
                default_pager_server(in, out));
ddprintf ("DPAGER DEMUX OBJECT DONE <%p>: %d\n", in, in->msgh_id);
return rval;
}

mach_msg_size_t default_pager_msg_size_default = 8 * 1024;

boolean_t
default_pager_demux_default(in, out)
	mach_msg_header_t	*in;
	mach_msg_header_t	*out;
{
	if (in->msgh_local_port == default_pager_default_port) {
		/*
		 *	We receive memory_object_create messages in
		 *	the memory_object_default interface.
		 */

int rval;
ddprintf ("DPAGER DEMUX DEFAULT <%p>: %d\n", in, in->msgh_id);
rval =
		 (seqnos_memory_object_default_server(in, out) ||
			default_pager_server(in, out));
ddprintf ("DPAGER DEMUX DEFAULT DONE <%p>: %d\n", in, in->msgh_id);
return rval;
	} else if (in->msgh_local_port == default_pager_exception_port) {
		/*
		 *	We receive exception messages for
		 *	ourself and the startup task.
		 */

		return exc_server(in, out);
	} else {
		panic(my_name);
		return FALSE;
	}
}

/*
 *	We use multiple threads, for two reasons.
 *
 *	First, memory objects created by default_pager_object_create
 *	are "external", instead of "internal".  This means the kernel
 *	sends data (memory_object_data_write) to the object pageable.
 *	To prevent deadlocks, the external and internal objects must
 *	be managed by different threads.
 *
 *	Second, the default pager uses synchronous IO operations.
 *	Spreading requests across multiple threads should
 *	recover some of the performance loss from synchronous IO.
 *
 *	We have 3+ threads.
 *	One receives memory_object_create and
 *	default_pager_object_create requests.
 *	One or more manage internal objects.
 *	One or more manage external objects.
 */

void
default_pager_thread_privileges()
{
	/*
	 *	Set thread privileges.
	 */
	cthread_wire();		/* attach kernel thread to cthread */
	wire_thread();		/* grab a kernel stack and memory allocation
				   privileges */
}

any_t
default_pager_default_thread (arg)
     any_t arg;
{
  kern_return_t kr;
	default_pager_thread_privileges ();
   	for (;;) {
		kr = mach_msg_server(default_pager_demux_default,
				     default_pager_msg_size_default,
				     default_pager_default_set);
		panic(my_name, kr);
	}
}



any_t
default_pager_thread(arg)
	any_t	arg;
{
	default_pager_thread_t *dpt = (default_pager_thread_t *) arg;
	mach_port_t pset;
	kern_return_t kr;

	cthread_set_data(cthread_self(), (any_t) dpt);


	/*
	 *	Threads handling external objects cannot have
	 *	privileges.  Otherwise a burst of data-requests for an
	 *	external object could empty the free-page queue,
	 *	because the fault code only reserves real pages for
	 *	requests sent to internal objects.
	 */

	if (dpt->dpt_internal) {
		default_pager_thread_privileges();
		pset = default_pager_internal_set;
	} else {
		pset = default_pager_external_set;
	}

	for (;;) {
		kr = mach_msg_server(default_pager_demux_object,
				     default_pager_msg_size_object,
				     pset);
		panic(my_name, kr);
	}
}

void
start_default_pager_thread(internal)
	boolean_t internal;
{
	default_pager_thread_t *dpt;
	kern_return_t kr;

	dpt = (default_pager_thread_t *) kalloc(sizeof *dpt);
	if (dpt == 0)
		panic(my_name);

	dpt->dpt_internal = internal;

	kr = vm_allocate(default_pager_self, &dpt->dpt_buffer,
			 vm_page_size, TRUE);
	if (kr != KERN_SUCCESS)
		panic(my_name);
	wire_memory(dpt->dpt_buffer, vm_page_size,
		    VM_PROT_READ|VM_PROT_WRITE);

	dpt->dpt_thread = cthread_fork(default_pager_thread, (any_t) dpt);
}

void
default_pager_initialize(host_port)
	mach_port_t	host_port;
{
	memory_object_t		DMM;
	kern_return_t		kr;

	/*
	 *	This task will become the default pager.
	 */
	default_pager_self = mach_task_self();

	/*
	 *	Initialize the "default pager" port.
	 */
	kr = mach_port_allocate(default_pager_self, MACH_PORT_RIGHT_RECEIVE,
				&default_pager_default_port);
	if (kr != KERN_SUCCESS)
		panic(my_name);

	DMM = default_pager_default_port;
	kr = vm_set_default_memory_manager(host_port, &DMM);
	if ((kr != KERN_SUCCESS) || MACH_PORT_VALID(DMM))
		panic(my_name);

	/*
	 *	Initialize the exception port.
	 */
	kr = mach_port_allocate(default_pager_self, MACH_PORT_RIGHT_RECEIVE,
				&default_pager_exception_port);
	if (kr != KERN_SUCCESS)
		panic(my_name);

	/*
	 * Arrange for wiring privileges.
	 */
	wire_setup(host_port);

	/*
	 * Find out how many CPUs we have, to determine the number
	 * of threads to create.
	 */
	if (default_pager_internal_count == 0) {
		host_basic_info_data_t h_info;
		natural_t h_info_count;

		h_info_count = HOST_BASIC_INFO_COUNT;
		(void) host_info(host_port, HOST_BASIC_INFO,
				 (host_info_t)&h_info, &h_info_count);

		/*
		 *	Random computation to get more parallelism on
		 *	multiprocessors.
		 */
		default_pager_internal_count =
		    (h_info.avail_cpus > 32 ? 32 : h_info.avail_cpus) / 4 + 3;
	}
}

/*
 * Initialize and Run the default pager
 */
void
default_pager()
{
	kern_return_t kr;
	int i;

	default_pager_thread_privileges();

	/*
	 * Wire down code, data, stack
	 */
	wire_all_memory();


	/*
	 *	Initialize the list of all pagers.
	 */
	pager_port_list_init();

	kr = mach_port_allocate(default_pager_self, MACH_PORT_RIGHT_PORT_SET,
				&default_pager_internal_set);
	if (kr != KERN_SUCCESS)
		panic(my_name);

	kr = mach_port_allocate(default_pager_self, MACH_PORT_RIGHT_PORT_SET,
				&default_pager_external_set);
	if (kr != KERN_SUCCESS)
		panic(my_name);

	kr = mach_port_allocate(default_pager_self, MACH_PORT_RIGHT_PORT_SET,
				&default_pager_default_set);
	if (kr != KERN_SUCCESS)
		panic(my_name);

	kr = mach_port_move_member(default_pager_self,
				   default_pager_default_port,
				   default_pager_default_set);
	if (kr != KERN_SUCCESS)
		panic(my_name);

	kr = mach_port_move_member(default_pager_self,
				   default_pager_exception_port,
				   default_pager_default_set);
	if (kr != KERN_SUCCESS)
		panic(my_name);

	/*
	 *	Now we create the threads that will actually
	 *	manage objects.
	 */

	for (i = 0; i < default_pager_internal_count; i++)
		start_default_pager_thread(TRUE);

	for (i = 0; i < default_pager_external_count; i++)
		start_default_pager_thread(FALSE);

	default_pager_default_thread(0); /* Become the default_pager server */
#if 0
	cthread_fork (default_pager_default_thread, 0);
	/*	cthread_exit (cthread_self ()); */
	thread_suspend (mach_thread_self ());
#endif
}

/*
 * Create an external object.
 */
kern_return_t
S_default_pager_object_create (mach_port_t pager,
			       mach_port_t *mem_obj,
			       vm_size_t size)
{
	default_pager_t ds;
	mach_port_t port;
	kern_return_t result;

	if (pager != default_pager_default_port)
		return KERN_INVALID_ARGUMENT;

	ds = pager_port_alloc(size);
rename_it:
	port = (mach_port_t) pnameof(ds);
	result = mach_port_allocate_name(default_pager_self,
				    MACH_PORT_RIGHT_RECEIVE, port);
	if (result != KERN_SUCCESS) {
		default_pager_t	ds1;

		if (result != KERN_NAME_EXISTS) return (result);

		ds1 = (default_pager_t) kalloc(sizeof *ds1);
		*ds1 = *ds;
		mutex_lock(&all_pagers.lock);
		queue_enter(&all_pagers.leak_queue, ds, default_pager_t, links);
		mutex_unlock(&all_pagers.lock);
		ds = ds1;
		goto rename_it;
	}

	/*
	 *	Set up associations between these ports
	 *	and this default_pager structure
	 */

	ds->pager = port;
	pager_port_list_insert(port, ds);
	default_pager_add(ds, FALSE);

	*mem_obj = port;
	return (KERN_SUCCESS);
}

kern_return_t
S_default_pager_info (mach_port_t pager,
		      default_pager_info_t *infop)
{
	vm_size_t		total, free;

	if (pager != default_pager_default_port)
		return KERN_INVALID_ARGUMENT;

	mutex_lock(&all_partitions.lock);
	paging_space_info(&total, &free);
	mutex_unlock(&all_partitions.lock);

	infop->dpi_total_space = ptoa(total);
	infop->dpi_free_space = ptoa(free);
	infop->dpi_page_size = vm_page_size;
	return KERN_SUCCESS;
}

kern_return_t
S_default_pager_objects (mach_port_t pager,
			 default_pager_object_array_t *objectsp,
			 natural_t *ocountp,
			 mach_port_array_t *portsp,
			 natural_t *pcountp)
{
	vm_offset_t			oaddr;	/* memory for objects */
	vm_size_t			osize;	/* current size */
	default_pager_object_t		*objects;
	natural_t			opotential;

	vm_offset_t			paddr;	/* memory for ports */
	vm_size_t			psize;	/* current size */
	mach_port_t			*ports;
	natural_t			ppotential;

	unsigned int			actual;
	unsigned int			num_pagers;
	kern_return_t			kr;
	default_pager_t			entry;

	if (pager != default_pager_default_port)
		return KERN_INVALID_ARGUMENT;

	/* start with the inline memory */

	num_pagers = 0;

	objects = *objectsp;
	opotential = *ocountp;

	ports = *portsp;
	ppotential = *pcountp;

	mutex_lock(&all_pagers.lock);
	/*
	 * We will send no more than this many
	 */
	actual = all_pagers.count;
	mutex_unlock(&all_pagers.lock);

	if (opotential < actual) {
		vm_offset_t	newaddr;
		vm_size_t	newsize;

		newsize = 2 * round_page(actual * sizeof *objects);

		kr = vm_allocate(default_pager_self, &newaddr, newsize, TRUE);
		if (kr != KERN_SUCCESS)
			goto nomemory;

		oaddr = newaddr;
		osize = newsize;
		opotential = osize/sizeof *objects;
		objects = (default_pager_object_t *) oaddr;
	}

	if (ppotential < actual) {
		vm_offset_t	newaddr;
		vm_size_t	newsize;

		newsize = 2 * round_page(actual * sizeof *ports);

		kr = vm_allocate(default_pager_self, &newaddr, newsize, TRUE);
		if (kr != KERN_SUCCESS)
			goto nomemory;

		paddr = newaddr;
		psize = newsize;
		ppotential = psize/sizeof *ports;
		ports = (mach_port_t *) paddr;
	}

	/*
	 * Now scan the list.
	 */

	mutex_lock(&all_pagers.lock);

	num_pagers = 0;
	queue_iterate(&all_pagers.queue, entry, default_pager_t, links) {

		mach_port_t		port;
		vm_size_t		size;

		if ((num_pagers >= opotential) ||
		    (num_pagers >= ppotential)) {
			/*
			 *	This should be rare.  In any case,
			 *	we will only miss recent objects,
			 *	because they are added at the end.
			 */
			break;
		}

		/*
		 * Avoid interfering with normal operations
		 */
		if (!mutex_try_lock(&entry->dpager.lock))
			goto not_this_one;
		size = pager_allocated(&entry->dpager);
		mutex_unlock(&entry->dpager.lock);

		dstruct_lock(entry);

		port = entry->pager_name;
		if (port == MACH_PORT_NULL) {
			/*
			 *	The object is waiting for no-senders
			 *	or memory_object_init.
			 */
			dstruct_unlock(entry);
			goto not_this_one;
		}

		/*
		 *	We need a reference for the reply message.
		 *	While we are unlocked, the bucket queue
		 *	can change and the object might be terminated.
		 *	memory_object_terminate will wait for us,
		 *	preventing deallocation of the entry.
		 */

		if (--entry->name_refs == 0) {
			dstruct_unlock(entry);

			/* keep the list locked, wont take long */

			kr = mach_port_mod_refs(default_pager_self,
					port, MACH_PORT_RIGHT_SEND,
					default_pager_max_urefs);
			if (kr != KERN_SUCCESS)
				panic("%sdefault_pager_objects",my_name);

			dstruct_lock(entry);

			entry->name_refs += default_pager_max_urefs;
			pager_port_finish_refs(entry);
		}
		dstruct_unlock(entry);

		/* the arrays are wired, so no deadlock worries */

		objects[num_pagers].dpo_object = (vm_offset_t) entry;
		objects[num_pagers].dpo_size = size;
		ports  [num_pagers++] = port;
		continue;
not_this_one:
		/*
		 * Do not return garbage
		 */
		objects[num_pagers].dpo_object = (vm_offset_t) 0;
		objects[num_pagers].dpo_size = 0;
		ports  [num_pagers++] = MACH_PORT_NULL;

	}

	mutex_unlock(&all_pagers.lock);

	/*
	 *	Deallocate and clear unused memory.
	 *	(Returned memory will automagically become pageable.)
	 */

	if (objects == *objectsp) {
		/*
		 *	Our returned information fit inline.
		 *	Nothing to deallocate.
		 */

		*ocountp = num_pagers;
	} else if (actual == 0) {
		(void) vm_deallocate(default_pager_self, oaddr, osize);

		/* return zero items inline */
		*ocountp = 0;
	} else {
		vm_offset_t used;

		used = round_page(actual * sizeof *objects);

		if (used != osize)
			(void) vm_deallocate(default_pager_self,
					     oaddr + used, osize - used);

		*objectsp = objects;
		*ocountp = num_pagers;
	}

	if (ports == *portsp) {
		/*
		 *	Our returned information fit inline.
		 *	Nothing to deallocate.
		 */

		*pcountp = num_pagers;
	} else if (actual == 0) {
		(void) vm_deallocate(default_pager_self, paddr, psize);

		/* return zero items inline */
		*pcountp = 0;
	} else {
		vm_offset_t used;

		used = round_page(actual * sizeof *ports);

		if (used != psize)
			(void) vm_deallocate(default_pager_self,
					     paddr + used, psize - used);

		*portsp = ports;
		*pcountp = num_pagers;
	}

	return KERN_SUCCESS;

    nomemory:

	{
		register int	i;
		for (i = 0; i < num_pagers; i++)
		    (void) mach_port_deallocate(default_pager_self, ports[i]);
	}

	if (objects != *objectsp)
		(void) vm_deallocate(default_pager_self, oaddr, osize);

	if (ports != *portsp)
		(void) vm_deallocate(default_pager_self, paddr, psize);

	return KERN_RESOURCE_SHORTAGE;
}


kern_return_t
S_default_pager_object_pages (mach_port_t pager,
			      mach_port_t object,
			      default_pager_page_array_t *pagesp,
			      natural_t *countp)
{
	vm_offset_t			addr;	/* memory for page offsets */
	vm_size_t			size;	/* current memory size */
	default_pager_page_t		*pages;
	natural_t 			potential, actual;
	kern_return_t			kr;

	if (pager != default_pager_default_port)
		return KERN_INVALID_ARGUMENT;

	/* we start with the inline space */

	pages = *pagesp;
	potential = *countp;

	for (;;) {
		default_pager_t		entry;

		mutex_lock(&all_pagers.lock);
		queue_iterate(&all_pagers.queue, entry, default_pager_t, links) {
			dstruct_lock(entry);
			if (entry->pager_name == object) {
				mutex_unlock(&all_pagers.lock);
				goto found_object;
			}
			dstruct_unlock(entry);
		}
		mutex_unlock(&all_pagers.lock);

		/* did not find the object */

		if (pages != *pagesp)
			(void) vm_deallocate(default_pager_self, addr, size);
		return KERN_INVALID_ARGUMENT;

	    found_object:

		if (!mutex_try_lock(&entry->dpager.lock)) {
			/* oh well bad luck */

			dstruct_unlock(entry);

			/* yield the processor */
			(void) thread_switch(MACH_PORT_NULL,
					     SWITCH_OPTION_NONE, 0);
			continue;
		}

		actual = pager_pages(&entry->dpager, pages, potential);
		mutex_unlock(&entry->dpager.lock);
		dstruct_unlock(entry);

		if (actual <= potential)
			break;

		/* allocate more memory */

		if (pages != *pagesp)
			(void) vm_deallocate(default_pager_self, addr, size);
		size = round_page(actual * sizeof *pages);
		kr = vm_allocate(default_pager_self, &addr, size, TRUE);
		if (kr != KERN_SUCCESS)
			return kr;
		pages = (default_pager_page_t *) addr;
		potential = size/sizeof *pages;
	}

	/*
	 *	Deallocate and clear unused memory.
	 *	(Returned memory will automagically become pageable.)
	 */

	if (pages == *pagesp) {
		/*
		 *	Our returned information fit inline.
		 *	Nothing to deallocate.
		 */

		*countp = actual;
	} else if (actual == 0) {
		(void) vm_deallocate(default_pager_self, addr, size);

		/* return zero items inline */
		*countp = 0;
	} else {
		vm_offset_t used;

		used = round_page(actual * sizeof *pages);

		if (used != size)
			(void) vm_deallocate(default_pager_self,
					     addr + used, size - used);

		*pagesp = pages;
		*countp = actual;
	}
	return KERN_SUCCESS;
}


kern_return_t
S_default_pager_object_set_size (mach_port_t pager,
				 mach_port_seqno_t seqno,
				 mach_port_t reply_to,
				 vm_size_t limit)
{
  kern_return_t kr;
  default_pager_t ds;

  ds = pager_port_lookup(pager);
  if (ds == DEFAULT_PAGER_NULL)
    return KERN_INVALID_ARGUMENT;

  pager_port_lock(ds, seqno);
  pager_port_check_request(ds, reply_to);
  pager_port_wait_for_readers(ds);
  pager_port_wait_for_writers(ds);

  limit = round_page (limit);
  if (ds->dpager.size <= limit / vm_page_size)
    {
      /* The limit has not been exceeded heretofore.  Just change it.  */
      ds->dpager.limit = limit;
      kr = KERN_SUCCESS;
    }
  else if (ds->lock_request == MACH_PORT_NULL)
    {
      /* Tell the kernel to flush from core all the pages being removed.
	 We will get the memory_object_lock_completed callback when they
	 have been flushed.  We handle that by completing the limit update
	 and posting the reply to the pending truncation.  */
      kr = memory_object_lock_request (ds->pager_request,
				       limit,
				       ds->dpager.size * vm_page_size - limit,
				       MEMORY_OBJECT_RETURN_NONE, TRUE,
				       VM_PROT_ALL, ds->pager);
      if (kr != KERN_SUCCESS)
	panic ("memory_object_lock_request: %d", kr);
      ds->lock_request = reply_to;
      kr = MIG_NO_REPLY;
    }
  else
    /* There is already another call in progress.  Tough titties.  */
    kr = KERN_FAILURE;

  pager_port_unlock(ds, seqno);

  return kr;
}

/*
 * Add/remove extra paging space
 */

extern mach_port_t bootstrap_master_device_port;
extern mach_port_t bootstrap_master_host_port;

kern_return_t
S_default_pager_paging_file (pager, mdport, file_name, add)
	mach_port_t			pager;
	mach_port_t			mdport;
	default_pager_filename_t	file_name;
	boolean_t			add;
{
	kern_return_t   kr;

	if (pager != default_pager_default_port)
		return KERN_INVALID_ARGUMENT;

#if 0
dprintf("bmd %x md %x\n", bootstrap_master_device_port, mdport);
#endif
	if (add) {
		kr = add_paging_file(bootstrap_master_device_port,
				     file_name, 0);
	} else {
		kr = remove_paging_file(file_name);
	}

	/* XXXX more code needed */
	if (mdport != bootstrap_master_device_port)
		mach_port_deallocate( mach_task_self(), mdport);

	return kr;
}

default_pager_register_fileserver(pager, fileserver)
	mach_port_t			pager;
	mach_port_t			fileserver;
{
	if (pager != default_pager_default_port)
		return KERN_INVALID_ARGUMENT;
#if	notyet
	mach_port_deallocate(mach_task_self(), fileserver);
	if (0) dp_helper_paging_space(0,0,0);/*just linkit*/
#endif
	return KERN_SUCCESS;
}

/*
 * When things do not quite workout...
 */
no_paging_space(out_of_memory)
	boolean_t		out_of_memory;
{
	static char		here[] = "%s *** NOT ENOUGH PAGING SPACE ***";

	if (out_of_memory)
		dprintf("*** OUT OF MEMORY *** ");
	panic(here, my_name);
}

overcommitted(got_more_space, space)
	boolean_t	got_more_space;
	vm_size_t	space;		/* in pages */
{
	vm_size_t	pages_free, pages_total;

	static boolean_t user_warned = FALSE;
	static vm_size_t pages_shortage = 0;

	paging_space_info(&pages_total, &pages_free);

	/*
	 * If user added more space, see if it is enough
	 */
	if (got_more_space) {
		pages_free -= pages_shortage;
		if (pages_free > 0) {
			pages_shortage = 0;
			if (user_warned)
				dprintf("%s paging space ok now.\n", my_name);
		} else
			pages_shortage = pages_free;
		user_warned = FALSE;
		return;
	}
	/*
	 * We ran out of gas, let user know.
	 */
	pages_free -= space;
	pages_shortage = (pages_free > 0) ? 0 : -pages_free;
	if (!user_warned && pages_shortage) {
		user_warned = TRUE;
		dprintf("%s paging space over-committed.\n", my_name);
	}
#if debug
	user_warned = FALSE;
	dprintf("%s paging space over-committed [+%d (%d) pages].\n",
			my_name, space, pages_shortage);
#endif
}

paging_space_info(totp, freep)
	vm_size_t	*totp, *freep;
{
	register vm_size_t	total, free;
	register partition_t	part;
	register int		i;

	total = free = 0;
	for (i = 0; i < all_partitions.n_partitions; i++) {

		if ((part = partition_of(i)) == 0) continue;

		/* no need to lock: by the time this data
		   gets back to any remote requestor it
		   will be obsolete anyways */
		total += part->total_size;
		free += part->free;
#if debug
		dprintf("Partition %d: x%x total, x%x free\n",
		       i, part->total_size, part->free);
#endif
	}
	*totp = total;
	*freep = free;
}

/*
 *	Catch exceptions.
 */

kern_return_t
catch_exception_raise(exception_port, thread, task, exception, code, subcode)
	mach_port_t exception_port;
	mach_port_t thread, task;
	int exception, code, subcode;
{
	ddprintf ("(default_pager)catch_exception_raise(%d,%d,%d)\n",
	       exception, code, subcode);
	panic(my_name);

	/* mach_msg_server will deallocate thread/task for us */

	return KERN_FAILURE;
}
