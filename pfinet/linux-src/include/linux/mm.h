#ifndef _LINUX_MM_H
#define _LINUX_MM_H

#include <linux/sched.h>
#include <linux/errno.h>

#ifdef __KERNEL__

#include <linux/string.h>

extern unsigned long max_mapnr;
extern unsigned long num_physpages;
extern void * high_memory;
extern int page_cluster;

#include <asm/page.h>
#include <asm/atomic.h>

/*
 * Linux kernel virtual memory manager primitives.
 * The idea being to have a "virtual" mm in the same way
 * we have a virtual fs - giving a cleaner interface to the
 * mm details, and allowing different kinds of memory mappings
 * (from shared memory to executable loading to arbitrary
 * mmap() functions).
 */

/*
 * This struct defines a memory VMM memory area. There is one of these
 * per VM-area/task.  A VM area is any part of the process virtual memory
 * space that has a special rule for the page-fault handlers (ie a shared
 * library, the executable area etc).
 */
struct vm_area_struct {
	struct mm_struct * vm_mm;	/* VM area parameters */
	unsigned long vm_start;
	unsigned long vm_end;

	/* linked list of VM areas per task, sorted by address */
	struct vm_area_struct *vm_next;

	pgprot_t vm_page_prot;
	unsigned short vm_flags;

	/* AVL tree of VM areas per task, sorted by address */
	short vm_avl_height;
	struct vm_area_struct * vm_avl_left;
	struct vm_area_struct * vm_avl_right;

	/* For areas with inode, the list inode->i_mmap, for shm areas,
	 * the list of attaches, otherwise unused.
	 */
	struct vm_area_struct *vm_next_share;
	struct vm_area_struct **vm_pprev_share;

	struct vm_operations_struct * vm_ops;
	unsigned long vm_offset;
	struct file * vm_file;
	unsigned long vm_pte;			/* shared mem */
};

/*
 * vm_flags..
 */
#define VM_READ		0x0001	/* currently active flags */
#define VM_WRITE	0x0002
#define VM_EXEC		0x0004
#define VM_SHARED	0x0008

#define VM_MAYREAD	0x0010	/* limits for mprotect() etc */
#define VM_MAYWRITE	0x0020
#define VM_MAYEXEC	0x0040
#define VM_MAYSHARE	0x0080

#define VM_GROWSDOWN	0x0100	/* general info on the segment */
#define VM_GROWSUP	0x0200
#define VM_SHM		0x0400	/* shared memory area, don't swap out */
#define VM_DENYWRITE	0x0800	/* ETXTBSY on write attempts.. */

#define VM_EXECUTABLE	0x1000
#define VM_LOCKED	0x2000
#define VM_IO           0x4000  /* Memory mapped I/O or similar */

#define VM_STACK_FLAGS	0x0177

/*
 * mapping from the currently active vm_flags protection bits (the
 * low four bits) to a page protection mask..
 */
extern pgprot_t protection_map[16];


/*
 * These are the virtual MM functions - opening of an area, closing and
 * unmapping it (needed to keep files on disk up-to-date etc), pointer
 * to the functions called when a no-page or a wp-page exception occurs. 
 */
struct vm_operations_struct {
	void (*open)(struct vm_area_struct * area);
	void (*close)(struct vm_area_struct * area);
	void (*unmap)(struct vm_area_struct *area, unsigned long, size_t);
	void (*protect)(struct vm_area_struct *area, unsigned long, size_t, unsigned int newprot);
	int (*sync)(struct vm_area_struct *area, unsigned long, size_t, unsigned int flags);
	void (*advise)(struct vm_area_struct *area, unsigned long, size_t, unsigned int advise);
	unsigned long (*nopage)(struct vm_area_struct * area, unsigned long address, int write_access);
	unsigned long (*wppage)(struct vm_area_struct * area, unsigned long address,
		unsigned long page);
	int (*swapout)(struct vm_area_struct *, struct page *);
	pte_t (*swapin)(struct vm_area_struct *, unsigned long, unsigned long);
};

/*
 * Try to keep the most commonly accessed fields in single cache lines
 * here (16 bytes or greater).  This ordering should be particularly
 * beneficial on 32-bit processors.
 *
 * The first line is data used in page cache lookup, the second line
 * is used for linear searches (eg. clock algorithm scans). 
 */
typedef struct page {
	/* these must be first (free area handling) */
	struct page *next;
	struct page *prev;
	struct inode *inode;
	unsigned long offset;
	struct page *next_hash;
	atomic_t count;
	unsigned long flags;	/* atomic flags, some possibly updated asynchronously */
	struct wait_queue *wait;
	struct page **pprev_hash;
	struct buffer_head * buffers;
} mem_map_t;

/* Page flag bit values */
#define PG_locked		 0
#define PG_error		 1
#define PG_referenced		 2
#define PG_dirty		 3
#define PG_uptodate		 4
#define PG_free_after		 5
#define PG_decr_after		 6
#define PG_swap_unlock_after	 7
#define PG_DMA			 8
#define PG_Slab			 9
#define PG_swap_cache		10
#define PG_skip			11
#define PG_reserved		31

/* Make it prettier to test the above... */
#define PageLocked(page)	(test_bit(PG_locked, &(page)->flags))
#define PageError(page)		(test_bit(PG_error, &(page)->flags))
#define PageReferenced(page)	(test_bit(PG_referenced, &(page)->flags))
#define PageDirty(page)		(test_bit(PG_dirty, &(page)->flags))
#define PageUptodate(page)	(test_bit(PG_uptodate, &(page)->flags))
#define PageFreeAfter(page)	(test_bit(PG_free_after, &(page)->flags))
#define PageDecrAfter(page)	(test_bit(PG_decr_after, &(page)->flags))
#define PageSwapUnlockAfter(page) (test_bit(PG_swap_unlock_after, &(page)->flags))
#define PageDMA(page)		(test_bit(PG_DMA, &(page)->flags))
#define PageSlab(page)		(test_bit(PG_Slab, &(page)->flags))
#define PageSwapCache(page)	(test_bit(PG_swap_cache, &(page)->flags))
#define PageReserved(page)	(test_bit(PG_reserved, &(page)->flags))

#define PageSetSlab(page)	(set_bit(PG_Slab, &(page)->flags))
#define PageSetSwapCache(page)	(set_bit(PG_swap_cache, &(page)->flags))

#define PageTestandSetDirty(page)	\
			(test_and_set_bit(PG_dirty, &(page)->flags))
#define PageTestandSetSwapCache(page)	\
			(test_and_set_bit(PG_swap_cache, &(page)->flags))

#define PageClearSlab(page)	(clear_bit(PG_Slab, &(page)->flags))
#define PageClearSwapCache(page)(clear_bit(PG_swap_cache, &(page)->flags))

#define PageTestandClearDirty(page) \
			(test_and_clear_bit(PG_dirty, &(page)->flags))
#define PageTestandClearSwapCache(page)	\
			(test_and_clear_bit(PG_swap_cache, &(page)->flags))

/*
 * Various page->flags bits:
 *
 * PG_reserved is set for a page which must never be accessed (which
 * may not even be present).
 *
 * PG_DMA is set for those pages which lie in the range of
 * physical addresses capable of carrying DMA transfers.
 *
 * Multiple processes may "see" the same page. E.g. for untouched
 * mappings of /dev/null, all processes see the same page full of
 * zeroes, and text pages of executables and shared libraries have
 * only one copy in memory, at most, normally.
 *
 * For the non-reserved pages, page->count denotes a reference count.
 *   page->count == 0 means the page is free.
 *   page->count == 1 means the page is used for exactly one purpose
 *   (e.g. a private data page of one process).
 *
 * A page may be used for kmalloc() or anyone else who does a
 * get_free_page(). In this case the page->count is at least 1, and
 * all other fields are unused but should be 0 or NULL. The
 * management of this page is the responsibility of the one who uses
 * it.
 *
 * The other pages (we may call them "process pages") are completely
 * managed by the Linux memory manager: I/O, buffers, swapping etc.
 * The following discussion applies only to them.
 *
 * A page may belong to an inode's memory mapping. In this case,
 * page->inode is the pointer to the inode, and page->offset is the
 * file offset of the page (not necessarily a multiple of PAGE_SIZE).
 *
 * A page may have buffers allocated to it. In this case,
 * page->buffers is a circular list of these buffer heads. Else,
 * page->buffers == NULL.
 *
 * For pages belonging to inodes, the page->count is the number of
 * attaches, plus 1 if buffers are allocated to the page.
 *
 * All pages belonging to an inode make up a doubly linked list
 * inode->i_pages, using the fields page->next and page->prev. (These
 * fields are also used for freelist management when page->count==0.)
 * There is also a hash table mapping (inode,offset) to the page
 * in memory if present. The lists for this hash table use the fields
 * page->next_hash and page->pprev_hash.
 *
 * All process pages can do I/O:
 * - inode pages may need to be read from disk,
 * - inode pages which have been modified and are MAP_SHARED may need
 *   to be written to disk,
 * - private pages which have been modified may need to be swapped out
 *   to swap space and (later) to be read back into memory.
 * During disk I/O, PG_locked is used. This bit is set before I/O
 * and reset when I/O completes. page->wait is a wait queue of all
 * tasks waiting for the I/O on this page to complete.
 * PG_uptodate tells whether the page's contents is valid.
 * When a read completes, the page becomes uptodate, unless a disk I/O
 * error happened.
 * When a write completes, and PG_free_after is set, the page is
 * freed without any further delay.
 *
 * For choosing which pages to swap out, inode pages carry a
 * PG_referenced bit, which is set any time the system accesses
 * that page through the (inode,offset) hash table.
 *
 * PG_skip is used on sparc/sparc64 architectures to "skip" certain
 * parts of the address space.
 *
 * PG_error is set to indicate that an I/O error occurred on this page.
 */

extern mem_map_t * mem_map;

/*
 * This is timing-critical - most of the time in getting a new page
 * goes to clearing the page. If you want a page without the clearing
 * overhead, just use __get_free_page() directly..
 */
#define __get_free_page(gfp_mask) __get_free_pages((gfp_mask),0)
#define __get_dma_pages(gfp_mask, order) __get_free_pages((gfp_mask) | GFP_DMA,(order))
extern unsigned long FASTCALL(__get_free_pages(int gfp_mask, unsigned long gfp_order));

extern inline unsigned long get_free_page(int gfp_mask)
{
	unsigned long page;

	page = __get_free_page(gfp_mask);
	if (page)
		clear_page(page);
	return page;
}

extern int low_on_memory;

/* memory.c & swap.c*/

#define free_page(addr) free_pages((addr),0)
extern void FASTCALL(free_pages(unsigned long addr, unsigned long order));
extern void FASTCALL(__free_page(struct page *));

extern void show_free_areas(void);
extern unsigned long put_dirty_page(struct task_struct * tsk,unsigned long page,
	unsigned long address);

extern void free_page_tables(struct mm_struct * mm);
extern void clear_page_tables(struct mm_struct *, unsigned long, int);
extern int new_page_tables(struct task_struct * tsk);

extern void zap_page_range(struct mm_struct *mm, unsigned long address, unsigned long size);
extern int copy_page_range(struct mm_struct *dst, struct mm_struct *src, struct vm_area_struct *vma);
extern int remap_page_range(unsigned long from, unsigned long to, unsigned long size, pgprot_t prot);
extern int zeromap_page_range(unsigned long from, unsigned long size, pgprot_t prot);

extern void vmtruncate(struct inode * inode, unsigned long offset);
extern int handle_mm_fault(struct task_struct *tsk,struct vm_area_struct *vma, unsigned long address, int write_access);
extern int make_pages_present(unsigned long addr, unsigned long end);

extern int pgt_cache_water[2];
extern int check_pgt_cache(void);

extern unsigned long paging_init(unsigned long start_mem, unsigned long end_mem);
extern void mem_init(unsigned long start_mem, unsigned long end_mem);
extern void show_mem(void);
extern void si_meminfo(struct sysinfo * val);

/* mmap.c */
extern void vma_init(void);
extern void merge_segments(struct mm_struct *, unsigned long, unsigned long);
extern void insert_vm_struct(struct mm_struct *, struct vm_area_struct *);
extern void build_mmap_avl(struct mm_struct *);
extern void exit_mmap(struct mm_struct *);
extern unsigned long get_unmapped_area(unsigned long, unsigned long);

extern unsigned long do_mmap(struct file *, unsigned long, unsigned long,
	unsigned long, unsigned long, unsigned long);
extern int do_munmap(unsigned long, size_t);

/* filemap.c */
extern void remove_inode_page(struct page *);
extern unsigned long page_unuse(struct page *);
extern int shrink_mmap(int, int);
extern void truncate_inode_pages(struct inode *, unsigned long);
extern unsigned long get_cached_page(struct inode *, unsigned long, int);
extern void put_cached_page(unsigned long);

/*
 * GFP bitmasks..
 */
#define __GFP_WAIT	0x01
#define __GFP_LOW	0x02
#define __GFP_MED	0x04
#define __GFP_HIGH	0x08
#define __GFP_IO	0x10
#define __GFP_SWAP	0x20

#define __GFP_DMA	0x80

#define GFP_BUFFER	(__GFP_LOW | __GFP_WAIT)
#define GFP_ATOMIC	(__GFP_HIGH)
#define GFP_USER	(__GFP_LOW | __GFP_WAIT | __GFP_IO)
#define GFP_KERNEL	(__GFP_MED | __GFP_WAIT | __GFP_IO)
#define GFP_NFS		(__GFP_HIGH | __GFP_WAIT | __GFP_IO)
#define GFP_KSWAPD	(__GFP_IO | __GFP_SWAP)

/* Flag - indicates that the buffer will be suitable for DMA.  Ignored on some
   platforms, used as appropriate on others */

#define GFP_DMA		__GFP_DMA

/* vma is the first one with  address < vma->vm_end,
 * and even  address < vma->vm_start. Have to extend vma. */
static inline int expand_stack(struct vm_area_struct * vma, unsigned long address)
{
	unsigned long grow;

	address &= PAGE_MASK;
	grow = vma->vm_start - address;
	if ((vma->vm_end - address
	    > current->rlim[RLIMIT_STACK].rlim_cur) ||
	    ((current->rlim[RLIMIT_AS].rlim_cur < RLIM_INFINITY) &&
	    ((vma->vm_mm->total_vm << PAGE_SHIFT) + grow
	    > current->rlim[RLIMIT_AS].rlim_cur)))
		return -ENOMEM;
	vma->vm_start = address;
	vma->vm_offset -= grow;
	vma->vm_mm->total_vm += grow >> PAGE_SHIFT;
	if (vma->vm_flags & VM_LOCKED)
		vma->vm_mm->locked_vm += grow >> PAGE_SHIFT;
	return 0;
}

/* Look up the first VMA which satisfies  addr < vm_end,  NULL if none. */
extern struct vm_area_struct * find_vma(struct mm_struct * mm, unsigned long addr);

/* Look up the first VMA which intersects the interval start_addr..end_addr-1,
   NULL if none.  Assume start_addr < end_addr. */
static inline struct vm_area_struct * find_vma_intersection(struct mm_struct * mm, unsigned long start_addr, unsigned long end_addr)
{
	struct vm_area_struct * vma = find_vma(mm,start_addr);

	if (vma && end_addr <= vma->vm_start)
		vma = NULL;
	return vma;
}

#define buffer_under_min()	((buffermem >> PAGE_SHIFT) * 100 < \
				buffer_mem.min_percent * num_physpages)
#define pgcache_under_min()	(page_cache_size * 100 < \
				page_cache.min_percent * num_physpages)

#endif /* __KERNEL__ */

#endif
