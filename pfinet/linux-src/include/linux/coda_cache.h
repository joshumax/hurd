/* Coda filesystem -- Linux Minicache
 *
 * Copyright (C) 1989 - 1997 Carnegie Mellon University
 *
 * Carnegie Mellon University encourages users of this software to
 * contribute improvements to the Coda project. Contact Peter Braam
 * <coda@cs.cmu.edu>
 */

#ifndef _CFSNC_HEADER_
#define _CFSNC_HEADER_

/*
 * Structure for an element in the Coda Credential Cache.
 */

struct coda_cache {
	struct list_head   cc_cclist;  /* list of all cache entries */
	struct list_head   cc_cnlist;  /* list of cache entries/cnode */
	int                cc_mask;
	struct coda_cred   cc_cred;
};

/* credential cache */
void coda_cache_enter(struct inode *inode, int mask);
void coda_cache_clear_inode(struct inode *);
void coda_cache_clear_all(struct super_block *sb);
void coda_cache_clear_cred(struct super_block *sb, struct coda_cred *cred);
int coda_cache_check(struct inode *inode, int mask);

/* for downcalls and attributes and lookups */
void coda_flag_inode(struct inode *inode, int flag);
void coda_flag_inode_children(struct inode *inode, int flag);


/*
 * Structure to contain statistics on the cache usage
 */

struct cfsnc_statistics {
	unsigned	hits;
	unsigned	misses;
	unsigned	enters;
	unsigned	dbl_enters;
	unsigned	long_name_enters;
	unsigned	long_name_lookups;
	unsigned	long_remove;
	unsigned	lru_rm;
	unsigned	zapPfids;
	unsigned	zapFids;
	unsigned	zapFile;
	unsigned	zapUsers;
	unsigned	Flushes;
	unsigned        Sum_bucket_len;
	unsigned        Sum2_bucket_len;
	unsigned        Max_bucket_len;
	unsigned        Num_zero_len;
	unsigned        Search_len;
};


#define CFSNC_FIND		((u_long) 1)
#define CFSNC_REMOVE		((u_long) 2)
#define CFSNC_INIT		((u_long) 3)
#define CFSNC_ENTER		((u_long) 4)
#define CFSNC_LOOKUP		((u_long) 5)
#define CFSNC_ZAPPFID		((u_long) 6)
#define CFSNC_ZAPFID		((u_long) 7)
#define CFSNC_ZAPVNODE		((u_long) 8)
#define CFSNC_ZAPFILE		((u_long) 9)
#define CFSNC_PURGEUSER	((u_long) 10)
#define CFSNC_FLUSH		((u_long) 11)
#define CFSNC_PRINTCFSNC	((u_long) 12)
#define CFSNC_PRINTSTATS	((u_long) 13)
#define CFSNC_REPLACE		((u_long) 14)

#endif _CFSNC_HEADER_
