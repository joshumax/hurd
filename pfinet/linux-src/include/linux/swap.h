#ifndef _LINUX_SWAP_H
#define _LINUX_SWAP_H

#include <asm/page.h>

#define SWAP_FLAG_PREFER	0x8000	/* set if swap priority specified */
#define SWAP_FLAG_PRIO_MASK	0x7fff
#define SWAP_FLAG_PRIO_SHIFT	0

#define MAX_SWAPFILES 8

union swap_header {
	struct 
	{
		char reserved[PAGE_SIZE - 10];
		char magic[10];
	} magic;
	struct 
	{
		char	     bootbits[1024];	/* Space for disklabel etc. */
		unsigned int version;
		unsigned int last_page;
		unsigned int nr_badpages;
		unsigned int padding[125];
		unsigned int badpages[1];
	} info;
};

#ifdef __KERNEL__

/*
 * Max bad pages in the new format..
 */
#define __swapoffset(x) ((unsigned long)&((union swap_header *)0)->x)
#define MAX_SWAP_BADPAGES \
	((__swapoffset(magic.magic) - __swapoffset(info.badpages)) / sizeof(int))

#undef DEBUG_SWAP

#include <asm/atomic.h>

#define SWP_USED	1
#define SWP_WRITEOK	3

#define SWAP_CLUSTER_MAX 32

#define SWAP_MAP_MAX	0x7fff
#define SWAP_MAP_BAD	0x8000

struct swap_info_struct {
	unsigned int flags;
	kdev_t swap_device;
	struct dentry * swap_file;
	unsigned short * swap_map;
	unsigned char * swap_lockmap;
	unsigned int lowest_bit;
	unsigned int highest_bit;
	unsigned int cluster_next;
	unsigned int cluster_nr;
	int prio;			/* swap priority */
	int pages;
	unsigned long max;
	int next;			/* next entry on swap list */
};

extern int nr_swap_pages;
extern int nr_free_pages;
extern atomic_t nr_async_pages;
extern struct inode swapper_inode;
extern unsigned long page_cache_size;
extern int buffermem;

/* Incomplete types for prototype declarations: */
struct task_struct;
struct vm_area_struct;
struct sysinfo;

/* linux/ipc/shm.c */
extern int shm_swap (int, int);

/* linux/mm/swap.c */
extern void swap_setup (void);

/* linux/mm/vmscan.c */
extern int try_to_free_pages(unsigned int gfp_mask);

/* linux/mm/page_io.c */
extern void rw_swap_page(int, unsigned long, char *, int);
extern void rw_swap_page_nocache(int, unsigned long, char *);
extern void rw_swap_page_nolock(int, unsigned long, char *, int);
extern void swap_after_unlock_page (unsigned long entry);

/* linux/mm/page_alloc.c */
extern void swap_in(struct task_struct *, struct vm_area_struct *,
		    pte_t *, unsigned long, int);


/* linux/mm/swap_state.c */
extern void show_swap_cache_info(void);
extern int add_to_swap_cache(struct page *, unsigned long);
extern int swap_duplicate(unsigned long);
extern int swap_check_entry(unsigned long);
struct page * lookup_swap_cache(unsigned long);
extern struct page * read_swap_cache_async(unsigned long, int);
#define read_swap_cache(entry) read_swap_cache_async(entry, 1);
extern int FASTCALL(swap_count(unsigned long));
/*
 * Make these inline later once they are working properly.
 */
extern void delete_from_swap_cache(struct page *page);
extern void free_page_and_swap_cache(unsigned long addr);

/* linux/mm/swapfile.c */
extern unsigned int nr_swapfiles;
extern struct swap_info_struct swap_info[];
void si_swapinfo(struct sysinfo *);
unsigned long get_swap_page(void);
extern void FASTCALL(swap_free(unsigned long));
struct swap_list_t {
	int head;	/* head of priority-ordered swapfile list */
	int next;	/* swapfile to be used next */
};
extern struct swap_list_t swap_list;
asmlinkage int sys_swapoff(const char *);
asmlinkage int sys_swapon(const char *, int);

/*
 * vm_ops not present page codes for shared memory.
 *
 * Will go away eventually..
 */
#define SHM_SWP_TYPE 0x20

/*
 * swap cache stuff (in linux/mm/swap_state.c)
 */

#define SWAP_CACHE_INFO

#ifdef SWAP_CACHE_INFO
extern unsigned long swap_cache_add_total;
extern unsigned long swap_cache_del_total;
extern unsigned long swap_cache_find_total;
extern unsigned long swap_cache_find_success;
#endif

extern inline unsigned long in_swap_cache(struct page *page)
{
	if (PageSwapCache(page))
		return page->offset;
	return 0;
}

/*
 * Work out if there are any other processes sharing this page, ignoring
 * any page reference coming from the swap cache, or from outstanding
 * swap IO on this page.  (The page cache _does_ count as another valid
 * reference to the page, however.)
 */
static inline int is_page_shared(struct page *page)
{
	unsigned int count;
	if (PageReserved(page))
		return 1;
	count = atomic_read(&page->count);
	if (PageSwapCache(page))
		count += swap_count(page->offset) - 2;
	if (PageFreeAfter(page))
		count--;
	return  count > 1;
}

#endif /* __KERNEL__*/

#endif /* _LINUX_SWAP_H */
