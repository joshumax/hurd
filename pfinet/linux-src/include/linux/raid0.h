#ifndef _RAID0_H
#define _RAID0_H

struct strip_zone
{
  int zone_offset;		/* Zone offset in md_dev */
  int dev_offset;		/* Zone offset in real dev */
  int size;			/* Zone size */
  int nb_dev;			/* Number of devices attached to the zone */
  struct real_dev *dev[MAX_REAL]; /* Devices attached to the zone */
};

struct raid0_hash
{
  struct strip_zone *zone0, *zone1;
};

struct raid0_data
{
  struct raid0_hash *hash_table; /* Dynamically allocated */
  struct strip_zone *strip_zone; /* This one too */
  int nr_strip_zones;
  struct strip_zone *smallest;
  int nr_zones;
};

#endif
