#ifndef _RAID1_H
#define _RAID1_H

#include <linux/md.h>

struct mirror_info {
	int		number;
	int		raid_disk;
	kdev_t		dev;
	int		next;
	int		sect_limit;

	/*
	 * State bits:
	 */
	int		operational;
	int		write_only;
	int		spare;
};

struct raid1_data {
	struct md_dev *mddev;
	struct mirror_info mirrors[MD_SB_DISKS];  	/* RAID1 devices, 2 to MD_SB_DISKS */
	int raid_disks;
	int working_disks;			/* Number of working disks */
	int last_used;
	unsigned long	next_sect;
	int		sect_count;
	int resync_running;
};

/*
 * this is our 'private' 'collective' RAID1 buffer head.
 * it contains information about what kind of IO operations were started
 * for this RAID5 operation, and about their status:
 */

struct raid1_bh {
	unsigned int		remaining;
	int			cmd;
	unsigned long		state;
	struct md_dev		*mddev;
	struct buffer_head	*master_bh;
	struct buffer_head	*mirror_bh [MD_SB_DISKS];
	struct buffer_head	bh_req;
	struct buffer_head	*next_retry;
};

#endif
