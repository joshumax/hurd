#ifndef _LINUX_BLKDEV_H
#define _LINUX_BLKDEV_H

#include <linux/major.h>
#include <linux/sched.h>
#include <linux/genhd.h>
#include <linux/tqueue.h>

/*
 * Ok, this is an expanded form so that we can use the same
 * request for paging requests when that is implemented. In
 * paging, 'bh' is NULL, and the semaphore is used to wait
 * for read/write completion.
 */
struct request {
	volatile int rq_status;	/* should split this into a few status bits */
#define RQ_INACTIVE		(-1)
#define RQ_ACTIVE		1
#define RQ_SCSI_BUSY		0xffff
#define RQ_SCSI_DONE		0xfffe
#define RQ_SCSI_DISCONNECTING	0xffe0

	kdev_t rq_dev;
	int cmd;		/* READ or WRITE */
	int errors;
	unsigned long sector;
	unsigned long nr_sectors;
	unsigned long nr_segments;
	unsigned long current_nr_sectors;
	char * buffer;
	struct semaphore * sem;
	struct buffer_head * bh;
	struct buffer_head * bhtail;
	struct request * next;
};

typedef void (request_fn_proc) (void);
typedef struct request ** (queue_proc) (kdev_t dev);

struct blk_dev_struct {
	request_fn_proc		*request_fn;
	/*
	 * queue_proc has to be atomic
	 */
	queue_proc		*queue;
	void			*data;
	struct request		*current_request;
	struct request   plug;
	struct tq_struct plug_tq;
};

struct sec_size {
	unsigned block_size;
	unsigned block_size_bits;
};

extern struct sec_size * blk_sec[MAX_BLKDEV];
extern struct blk_dev_struct blk_dev[MAX_BLKDEV];
extern struct wait_queue * wait_for_request;
extern void resetup_one_dev(struct gendisk *dev, int drive);
extern void unplug_device(void * data);
extern void make_request(int major,int rw, struct buffer_head * bh);

/* md needs this function to remap requests */
extern int md_map (int minor, kdev_t *rdev, unsigned long *rsector, unsigned long size);
extern int md_make_request (int minor, int rw, struct buffer_head * bh);
extern int md_error (kdev_t mddev, kdev_t rdev);

extern int * blk_size[MAX_BLKDEV];

extern int * blksize_size[MAX_BLKDEV];

extern int * hardsect_size[MAX_BLKDEV];

extern int * max_readahead[MAX_BLKDEV];

extern int * max_sectors[MAX_BLKDEV];

extern int * max_segments[MAX_BLKDEV];

#define MAX_SECTORS 128

#define MAX_SEGMENTS MAX_SECTORS

#define PageAlignSize(size) (((size) + PAGE_SIZE -1) & PAGE_MASK)
#if 0  /* small readahead */
#define MAX_READAHEAD PageAlignSize(4096*7)
#define MIN_READAHEAD PageAlignSize(4096*2)
#else /* large readahead */
#define MAX_READAHEAD PageAlignSize(4096*31)
#define MIN_READAHEAD PageAlignSize(4096*3)
#endif

#endif
