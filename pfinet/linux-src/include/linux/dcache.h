#ifndef __LINUX_DCACHE_H
#define __LINUX_DCACHE_H

#ifdef __KERNEL__

/*
 * linux/include/linux/dcache.h
 *
 * Dirent cache data structures
 *
 * (C) Copyright 1997 Thomas Schoebel-Theuer,
 * with heavy changes by Linus Torvalds
 */

#define D_MAXLEN 1024

#define IS_ROOT(x) ((x) == (x)->d_parent)

/*
 * "quick string" -- eases parameter passing, but more importantly
 * saves "metadata" about the string (ie length and the hash).
 */
struct qstr {
	const unsigned char * name;
	unsigned int len;
	unsigned int hash;
};

/* Name hashing routines. Initial hash value */
#define init_name_hash()		0

/* partial hash update function. Assume roughly 4 bits per character */
static __inline__ unsigned long partial_name_hash(unsigned long c, unsigned long prevhash)
{
	prevhash = (prevhash << 4) | (prevhash >> (8*sizeof(unsigned long)-4));
	return prevhash ^ c;
}

/* Finally: cut down the number of bits to a int value (and try to avoid losing bits) */
static __inline__ unsigned long end_name_hash(unsigned long hash)
{
	if (sizeof(hash) > sizeof(unsigned int))
		hash += hash >> 4*sizeof(hash);
	return (unsigned int) hash;
}

/* Compute the hash for a name string. */
static __inline__ unsigned int full_name_hash(const unsigned char * name, unsigned int len)
{
	unsigned long hash = init_name_hash();
	while (len--)
		hash = partial_name_hash(*name++, hash);
	return end_name_hash(hash);
}

#define DNAME_INLINE_LEN 16

struct dentry {
	int d_count;
	unsigned int d_flags;
	struct inode  * d_inode;	/* Where the name belongs to - NULL is negative */
	struct dentry * d_parent;	/* parent directory */
	struct dentry * d_mounts;	/* mount information */
	struct dentry * d_covers;
	struct list_head d_hash;	/* lookup hash list */
	struct list_head d_lru;		/* d_count = 0 LRU list */
	struct list_head d_child;	/* child of parent list */
	struct list_head d_subdirs;	/* our children */
	struct list_head d_alias;	/* inode alias list */
	struct qstr d_name;
	unsigned long d_time;		/* used by d_revalidate */
	struct dentry_operations  *d_op;
	struct super_block * d_sb;	/* The root of the dentry tree */
	unsigned long d_reftime;	/* last time referenced */
	void * d_fsdata;		/* fs-specific data */
	unsigned char d_iname[DNAME_INLINE_LEN]; /* small names */
};

struct dentry_operations {
	int (*d_revalidate)(struct dentry *, int);
	int (*d_hash) (struct dentry *, struct qstr *);
	int (*d_compare) (struct dentry *, struct qstr *, struct qstr *);
	void (*d_delete)(struct dentry *);
	void (*d_release)(struct dentry *);
	void (*d_iput)(struct dentry *, struct inode *);
};

/* the dentry parameter passed to d_hash and d_compare is the parent
 * directory of the entries to be compared. It is used in case these
 * functions need any directory specific information for determining
 * equivalency classes.  Using the dentry itself might not work, as it
 * might be a negative dentry which has no information associated with
 * it */



/* d_flags entries */
#define DCACHE_AUTOFS_PENDING 0x0001    /* autofs: "under construction" */
#define DCACHE_NFSFS_RENAMED  0x0002    /* this dentry has been "silly
					 * renamed" and has to be
					 * deleted on the last dput()
					 */

/*
 * d_drop() unhashes the entry from the parent
 * dentry hashes, so that it won't be found through
 * a VFS lookup any more. Note that this is different
 * from deleting the dentry - d_delete will try to
 * mark the dentry negative if possible, giving a
 * successful _negative_ lookup, while d_drop will
 * just make the cache lookup fail.
 *
 * d_drop() is used mainly for stuff that wants
 * to invalidate a dentry for some reason (NFS
 * timeouts or autofs deletes).
 */
static __inline__ void d_drop(struct dentry * dentry)
{
	list_del(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_hash);
}

static __inline__ int dname_external(struct dentry *d)
{
	return d->d_name.name != d->d_iname; 
}

/*
 * These are the low-level FS interfaces to the dcache..
 */
extern void d_instantiate(struct dentry *, struct inode *);
extern void d_delete(struct dentry *);

/* allocate/de-allocate */
extern struct dentry * d_alloc(struct dentry * parent, const struct qstr *name);
extern int prune_dcache(int, int);
extern void shrink_dcache_sb(struct super_block *);
extern void shrink_dcache_parent(struct dentry *);
extern int d_invalidate(struct dentry *);

#define shrink_dcache() prune_dcache(0, -1)

/* dcache memory management */
extern void shrink_dcache_memory(int, unsigned int);
extern void check_dcache_memory(void);
extern void free_inode_memory(int);	/* defined in fs/inode.c */

/* only used at mount-time */
extern struct dentry * d_alloc_root(struct inode * root_inode, struct dentry * old_root);

/* test whether root is busy without destroying dcache */
extern int is_root_busy(struct dentry *);

/* test whether we have any submounts in a subdir tree */
extern int have_submounts(struct dentry *);

/*
 * This adds the entry to the hash queues.
 */
extern void d_rehash(struct dentry * entry);
/*
 * This adds the entry to the hash queues and initializes "d_inode".
 * The entry was actually filled in earlier during "d_alloc()"
 */
static __inline__ void d_add(struct dentry * entry, struct inode * inode)
{
	d_rehash(entry);
	d_instantiate(entry, inode);
}

/* used for rename() and baskets */
extern void d_move(struct dentry * entry, struct dentry * newdentry);

/* appendix may either be NULL or be used for transname suffixes */
extern struct dentry * d_lookup(struct dentry * dir, struct qstr * name);

/* validate "insecure" dentry pointer */
extern int d_validate(struct dentry *dentry, struct dentry *dparent,
		      unsigned int hash, unsigned int len);

/* write full pathname into buffer and return start of pathname */
extern char * d_path(struct dentry * entry, char * buf, int buflen);

/* Allocation counts.. */
static __inline__ struct dentry * dget(struct dentry *dentry)
{
	if (dentry)
		dentry->d_count++;
	return dentry;
}

extern void dput(struct dentry *);

#endif /* __KERNEL__ */

#endif	/* __LINUX_DCACHE_H */
