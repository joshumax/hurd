#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

#include <asm/system.h>

/*
 * Page-mapping primitive inline functions
 *
 * Copyright 1995 Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/fs.h>

static inline unsigned long page_address(struct page * page)
{
	return PAGE_OFFSET + PAGE_SIZE * (page - mem_map);
}

/*
 * The page cache can done in larger chunks than
 * one page, because it allows for more efficient
 * throughput (it can then be mapped into user
 * space in smaller chunks for same flexibility).
 *
 * Or rather, it _will_ be done in larger chunks.
 */
#define PAGE_CACHE_SHIFT	PAGE_SHIFT
#define PAGE_CACHE_SIZE		PAGE_SIZE
#define PAGE_CACHE_MASK		PAGE_MASK

#define page_cache_alloc()	__get_free_page(GFP_USER)
#define page_cache_free(x)	free_page(x)
#define page_cache_release(x)	__free_page(x)

/*
 * From a kernel address, get the "struct page *"
 */
#define page_cache_entry(x)	(mem_map + MAP_NR(x))

#define PAGE_HASH_BITS page_hash_bits
#define PAGE_HASH_MASK page_hash_mask

extern unsigned long page_cache_size; /* # of pages currently in the hash table */
extern unsigned int page_hash_bits;
extern unsigned int page_hash_mask;
extern struct page **page_hash_table;

extern void page_cache_init(unsigned long);

/*
 * We use a power-of-two hash table to avoid a modulus,
 * and get a reasonable hash by knowing roughly how the
 * inode pointer and offsets are distributed (ie, we
 * roughly know which bits are "significant")
 */
static inline unsigned long _page_hashfn(struct inode * inode, unsigned long offset)
{
#define i (((unsigned long) inode)/(sizeof(struct inode) & ~ (sizeof(struct inode) - 1)))
#define o ((offset >> PAGE_SHIFT) + (offset & ~PAGE_MASK))
	return ((i+o) & PAGE_HASH_MASK);
#undef i
#undef o
}

#define page_hash(inode,offset) (page_hash_table+_page_hashfn(inode,offset))

static inline struct page * __find_page(struct inode * inode, unsigned long offset, struct page *page)
{
	goto inside;
	for (;;) {
		page = page->next_hash;
inside:
		if (!page)
			goto not_found;
		if (page->inode != inode)
			continue;
		if (page->offset == offset)
			break;
	}
	/* Found the page. */
	atomic_inc(&page->count);
	set_bit(PG_referenced, &page->flags);
not_found:
	return page;
}

static inline struct page *find_page(struct inode * inode, unsigned long offset)
{
	return __find_page(inode, offset, *page_hash(inode, offset));
}

static inline void remove_page_from_hash_queue(struct page * page)
{
	if(page->pprev_hash) {
		if(page->next_hash)
			page->next_hash->pprev_hash = page->pprev_hash;
		*page->pprev_hash = page->next_hash;
		page->pprev_hash = NULL;
	}
	page_cache_size--;
}

static inline void __add_page_to_hash_queue(struct page * page, struct page **p)
{
	page_cache_size++;
	if((page->next_hash = *p) != NULL)
		(*p)->pprev_hash = &page->next_hash;
	*p = page;
	page->pprev_hash = p;
}

static inline void add_page_to_hash_queue(struct page * page, struct inode * inode, unsigned long offset)
{
	__add_page_to_hash_queue(page, page_hash(inode,offset));
}

static inline void remove_page_from_inode_queue(struct page * page)
{
	struct inode * inode = page->inode;

	page->inode = NULL;
	inode->i_nrpages--;
	if (inode->i_pages == page)
		inode->i_pages = page->next;
	if (page->next)
		page->next->prev = page->prev;
	if (page->prev)
		page->prev->next = page->next;
	page->next = NULL;
	page->prev = NULL;
}

static inline void add_page_to_inode_queue(struct inode * inode, struct page * page)
{
	struct page **p = &inode->i_pages;

	inode->i_nrpages++;
	page->inode = inode;
	page->prev = NULL;
	if ((page->next = *p) != NULL)
		page->next->prev = page;
	*p = page;
}

extern void __wait_on_page(struct page *);
static inline void wait_on_page(struct page * page)
{
	if (PageLocked(page))
		__wait_on_page(page);
}

extern void update_vm_cache_conditional(struct inode *, unsigned long, const char *, int, unsigned long);
extern void update_vm_cache(struct inode *, unsigned long, const char *, int);

#endif
