/*
 * Copyright (c) 1980, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
/*static char sccsid[] = "from: @(#)mkfs.c	8.3 (Berkeley) 2/3/94";*/
static char *rcsid = "$Id: mkfs.c,v 1.22 2006/03/14 23:27:50 tschwinge Exp $";
#endif /* not lint */

#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <argp.h>
#include <assert.h>
#include <error.h>
#include <string.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "../ufs/dinode.h"
#include "../ufs/dir.h"
#include "../ufs/fs.h"
/* #include <sys/disklabel.h> */
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <version.h>

#include <device/device_types.h>
#include <device/disk_status.h>

#include <hurd.h>

/* Begin misc additions for GNU Hurd */

/* For GNU Hurd: the ufs DIRSIZ macro is different than the BSD
   4.4 version that mkfs expects.  So we provide here the BSD version. */
#undef DIRSIZ
#if (BYTE_ORDER == LITTLE_ENDIAN)
#define DIRSIZ(oldfmt, dp) \
    ((oldfmt) ? \
    ((sizeof (struct directory_entry) - (MAXNAMLEN+1)) + (((dp)->d_type+1 + 3) &~ 3)) : \
    ((sizeof (struct directory_entry) - (MAXNAMLEN+1)) + (((dp)->d_namlen+1 + 3) &~ 3)))
#else
#define DIRSIZ(oldfmt, dp) \
    ((sizeof (struct directory_entry) - (MAXNAMLEN+1)) + (((dp)->d_namlen+1 + 3) &~ 3))
#endif

#define MAXPHYS (64 * 1024)

/* Provide mode from struct dinode * */
#define DI_MODE(dp) (((dp)->di_modeh << 16) | (dp)->di_model)

#define DEV_BSIZE 512

#define btodb(bytes) ((bytes) / DEV_BSIZE)

/* End additions for GNU Hurd */

#ifndef STANDALONE
#include <a.out.h>
#include <stdio.h>
#include <time.h>
#endif

/*
 * make file system for cylinder-group style file systems
 */

extern error_t fd_get_disklabel (int fd, struct disklabel *lab);
static void mkfs (), initcg (), fsinit (), setblock ();
static void iput (), rdfs (), clrblock (), wtfs ();
static int makedir (), isblock ();

/*
 * We limit the size of the inode map to be no more than a
 * third of the cylinder group space, since we must leave at
 * least an equal amount of space for the block map.
 *
 * N.B.: MAXIPG must be a multiple of INOPB(fs).
 */
#define MAXIPG(fs)	roundup((fs)->fs_bsize * NBBY / 3, INOPB(fs))

#define UMASK		0755
#define MAXINOPB	(MAXBSIZE / sizeof(struct dinode))
#define POWEROF2(num)	(((num) & ((num) - 1)) == 0)

/*
 * variables set up by front end.
 */
#define extern
extern int	Nflag;		/* run mkfs without writing file system */
extern int	Oflag;		/* format as an 4.3BSD file system */
extern int	fssize;		/* file system size */
extern int	ntracks;	/* # tracks/cylinder */
extern int	nsectors;	/* # sectors/track */
extern int	nphyssectors;	/* # sectors/track including spares */
extern int	secpercyl;	/* sectors per cylinder */
extern int	sectorsize;	/* bytes/sector */
extern int	rpm;		/* revolutions/minute of drive */
extern int	interleave;	/* hardware sector interleave */
extern int	trackskew;	/* sector 0 skew, per track */
extern int	headswitch;	/* head switch time, usec */
extern int	trackseek;	/* track-to-track seek, usec */
extern int	fsize;		/* fragment size */
extern int	bsize;		/* block size */
extern int	cpg;		/* cylinders/cylinder group */
extern int	cpgflg;		/* cylinders/cylinder group flag was given */
extern int	minfree;	/* free space threshold */
extern int	opt;		/* optimization preference (space or time) */
extern int	density;	/* number of bytes per inode */
extern int	maxcontig;	/* max contiguous blocks to allocate */
extern int	rotdelay;	/* rotational delay between blocks */
extern int	maxbpg;		/* maximum blocks per file in a cyl group */
extern int	nrpos;		/* # of distinguished rotational positions */
extern int	bbsize;		/* boot block size */
extern int	sbsize;		/* superblock size */
#undef extern

union {
	struct fs fs;
	char pad[SBSIZE];
} fsun;
#define	sblock	fsun.fs
struct	csum *fscs;

union {
	struct cg cg;
	char pad[MAXBSIZE];
} cgun;
#define	acg	cgun.cg

struct dinode zino[MAXBSIZE / sizeof(struct dinode)];

int	fsi, fso;
daddr_t	alloc();

const char *argp_program_version = STANDARD_HURD_VERSION (mkfs.ufs);

#define _STRINGIFY(arg) #arg
#define STRINGIFY(arg) _STRINGIFY (arg)

static const struct argp_option options[] = {
  {0,0,0,0,0, 1},
  {"just-print", 'N', 0, 0,
     "Just print the file system parameters that would be used"},
  {"old-format", 'O', 0, 0, "Create a 4.3BSD format filesystem"},
  {"max-contig", 'a', "BLOCKS", 0,
     "The maximum number of contiguous blocks that will be laid out before"
     " forcing a rotational delay; the default is no limit"},
  {"block-size", 'b', "BYTES", 0, "The block size of the file system"},
  {"group-cylinders", 'c', "N", 0,
     "The number of cylinders per cylinder group; the default 16"},
  {"rot-delay", 'd', "MSEC", 0,
     "The expected time to service a transfer completion interrupt and"
     " initiate a new transfer on the same disk; the default is 0"},
  {"max-bpg", 'e', "BLOCKS", 0,
     "Maximum number of blocks any single file can allocate out of a cylinder"
     " group before it is forced to begin allocating blocks from another"
     " cylinder group; the default is about one quarter of the total blocks"
     " in a cylinder group"},
  {"frag-size",  'f', "BYTES", 0, "The fragment size"},
  {"inode-density", 'i', "BYTES", 0,
     "The density of inodes in the file system, in bytes of data space per"
     " inode; the default is one inode per 4 filesystem frags"},
  {"minfree", 'm', "PERCENT", 0,
     "The percentage of space reserved from normal users; the default is "
     STRINGIFY (MINFREE) "%"},
  {"rot-positions", 'n', "N", 0,
     "The number of distinct rotational positions; the default is 8"},
  {"optimization", 'o', "METH", 0, "``space'' or ``time''"},
  {"size", 's', "SECTORS", 0, "The size of the file system"},

  {0,0,0,0,
     "The following options override the standard sizes for the disk"
     " geometry; their default values are taken from the disk label:", 2},

  {"sector-size", 'S', "BYTES", 0, "The size of a sector (usually 512)"},
  {"skew", 'k', "SECTORS", 0, "Sector 0 skew, per track"},
  {"interleave", 'l', "LOG-PHYS-RATIO", 0, "Hardware sector interleave"},
  {"rpm", 'r', "RPM", 0, "The speed of the disk in revolutions per minute"},
  {"tracks", 't', "N", 0, "The number of tracks/cylinder"},
  {"sectors", 'u', "N", 0,
     "The number of sectors per track (does not include sectors reserved for"
     " bad block replacement"},
  {"spare-sectors", 'p', "N", 0,
     "Spare sectors (for bad sector replacement) at the end of each track"},
  {"cyl-spare-sectors", 'x', "N", 0,
     "Spare sectors (for bad sector replacement) at the end of the last track"
     " in each cylinder"},
  {0, 0}
};
static char *args_doc = "DEVICE";
static char *doc = "Write a ufs filesystem image onto DEVICE.";

struct amark { void *addr; struct amark *next; };

static void
amarks_add (struct amark **amarks, void *addr)
  {
    struct amark *up = malloc (sizeof (struct amark));
    assert (up != 0);
    up->addr = addr;
    up->next = *amarks;
    *amarks = up;
  }
static int
amarks_contains (struct amark *amarks, void *addr)
  {
    while (amarks)
      if (amarks->addr == addr)
	return 1;
      else
	amarks = amarks->next;
    return 0;
  }

static const struct disklabel default_disklabel = {
  d_rpm: 3600,
  d_interleave: 1,
  d_secsize: DEV_BSIZE,
  d_sparespertrack: 0,
  d_sparespercyl: 0,
  d_trackskew: 0,
  d_cylskew: 0,
  d_headswitch: 0,
  d_trkseek: 0,
};

char *device = 0;

#define deverr(code, err, fmt, args...) \
  error (code, err, "%s: " fmt, device , ##args)

int
main (int argc, char **argv)
{
  int fdo, fdi;
  struct amark *uparams = 0;
  error_t label_err = 0;
  struct disklabel label_buf, *label = 0;
  int nspares = 0, ncspares = 0;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'N': Nflag = 1; break;
	case 'O': Oflag = 1; break;

/* Mark &VAR as being a uparam, and set VAR.  */
#define UP_INT(var) { amarks_add (&uparams, &var); var = atoi (arg); }

	case 'a': UP_INT (maxcontig); break;
	case 'b': UP_INT (bsize); break;
	case 'c': UP_INT (cpg); cpgflg = 1; break;
	case 'd': UP_INT (rotdelay); break;
	case 'e': UP_INT (maxbpg); break;
	case 'f': UP_INT (fsize); break;
	case 'i': UP_INT (density); break;
	case 'm': UP_INT (minfree); break;
	case 'n': UP_INT (nrpos); break;
	case 's': UP_INT (fssize); break;
	case 'S': UP_INT (sectorsize); break;
	case 'k': UP_INT (trackskew); break;
	case 'l': UP_INT (interleave); break;
	case 'r': UP_INT (rpm); break;
	case 't': UP_INT (ntracks); break;
	case 'u': UP_INT (nsectors); break;
	case 'p': UP_INT (nspares); break;
	case 'x': UP_INT (ncspares); break;

	case 'o':
	  amarks_add (&uparams, &opt);
	  if (strcmp (arg, "time") == 0)
	    opt = FS_OPTTIME;
	  else if (strcmp (arg, "space") == 0)
	    opt = FS_OPTSPACE;
	  else
	    argp_error (state, "%s: Invalid value for --optimization", arg);
	  break;

	case ARGP_KEY_ARG:
	  if (state->arg_num > 0)
	    return ARGP_ERR_UNKNOWN;
	  device = arg;
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  const struct argp argp = { options, parse_opt, args_doc, doc };

  /* Tries to get the disklabel for DEVICE; if it can't, then if PARAM_NAME
     is 0, returns 0, otherwise an error is printed (using PARAM_NAME) and
     the program exits. */
  struct disklabel *dl (char *param_name)
    {
      if (! label)
	{
	  if (! label_err)
	    {
	      label_err = fd_get_disklabel (fdi, &label_buf);
	      if (! label_err)
		label = &label_buf;
	    }
	  if (label_err && param_name)
	    error (9, label_err,
		   "%s: Can't get disklabel; please specify --%s",
		   device, param_name);
	}
      return label;
    }
  /* Tries to get the integer field at offset OFFS from the disklabel for
     DEVICE; if it can't, then if PARAM_NAME is 0, returns the corresponding
     value from DEFAULT_DISKLABEL, otherwise an error is printed and the
     program exits. */
  int dl_int (char *param_name, size_t offs)
    {
      struct disklabel *l = dl (param_name);
      return *(int *)((char *)(l ?: &default_disklabel) + offs);
    }
  /* A version of dl_int that takes the field name instead of an offset.  */
#define DL_INT(param_name, field) \
  dl_int (param_name, offsetof (struct disklabel, field))

  /* Like dl_int, but adjust for any difference in sector size between the
     disklabel and SECTORSIZE.  */
  int dl_secs (char *param_name, size_t offs)
    {
      int val = dl_int (param_name, offs);
      int dl_ss = DL_INT (0, d_secsize);
      if (sectorsize < dl_ss)
	deverr (10, 0,
		"%d: Sector size is less than device sector size (%d)",
		sectorsize, dl_ss);
      else if (sectorsize > dl_ss)
	if (sectorsize % dl_ss != 0)
	  deverr (11, 0,
		  "%d: Sector size not a multiple of device sector size (%d)",
		  sectorsize, dl_ss);
	else
	  val /= sectorsize / dl_ss;
      return val;
    }
  /* A version of dl_secs that takes the field name instead of an offset.  */
#define DL_SECS(param_name, field) \
  dl_secs (param_name, offsetof (struct disklabel, field))

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, 0, 0, 0);

  fdi = open (device, O_RDONLY);
  if (fdi == -1)
    error (2, errno, "%s", device);
  fdo = open (device, O_WRONLY);
  if (fdo == -1)
    error (3, errno, "%s", device);

  /* If VAR hasn't been set by the user, set it to DEF_VAL.  */
#define DEFAULT(var, def_val) \
  (amarks_contains (uparams, &var) ? 0 : (((var) = (def_val)), 0))

  DEFAULT (sectorsize, DEV_BSIZE);
  DEFAULT (fssize,
	   ({ struct stat st;
	      if (fstat (fdi, &st) == -1)
		deverr (4, errno, "Cannot get size");
	      st.st_size / sectorsize; }));
  DEFAULT (ntracks, DL_INT ("tracks", d_ntracks));
  DEFAULT (nsectors, DL_SECS ("sectors", d_nsectors));
  DEFAULT (nspares, DL_SECS (0, d_sparespertrack));
  DEFAULT (ncspares, DL_SECS (0, d_sparespercyl));

  if (nspares >= nsectors)
    deverr (5, 0, "%d: Too many spare sectors per track", nspares);
  if (ncspares >= nsectors)
    deverr (5, 0, "%d: Too many spare sectors per cylinder", ncspares);
  nphyssectors = nsectors + nspares;

  secpercyl = nsectors * ntracks;

  DEFAULT (rpm, DL_INT (0, d_rpm));
  DEFAULT (interleave, DL_INT (0, d_interleave));
  DEFAULT (trackskew, DL_SECS (0, d_trackskew));
  DEFAULT (headswitch, DL_INT (0, d_headswitch));
  DEFAULT (trackseek, DL_INT (0, d_trkseek));

  DEFAULT (fsize, 1024);
  DEFAULT (bsize, 8192);

  DEFAULT (cpg, 16);
  DEFAULT (minfree, MINFREE);
  DEFAULT (opt, DEFAULTOPT);
  DEFAULT (density, 4 * fsize);
/*  maxcontig = MAX (1, MIN (MAXPHYS, MAXBSIZE) / bsize - 1); */
  DEFAULT (maxcontig, 0);
  DEFAULT (rotdelay, 4);
#define MAXBLKPG(bsize)	((bsize) / sizeof(daddr_t))
  DEFAULT (maxbpg, MAXBLKPG (bsize));
  DEFAULT (nrpos, 8);

  bbsize = BBSIZE;
  sbsize = SBSIZE;

  mkfs (0, device, fdi, fdo);

  return 0;
}

void
mkfs(pp, fsys, fi, fo)
	struct partition *pp;
	char *fsys;
	int fi, fo;
{
	register long i, mincpc, mincpg, inospercg;
	long cylno, rpos, blk, j, warn = 0;
	long used, mincpgcnt, bpcg;
	long mapcramped, inodecramped;
	long postblsize, rotblsize, totalsbsize;
	time_t utime;
	quad_t sizepb;

#ifndef STANDALONE
	time(&utime);
#endif
	fsi = fi;
	fso = fo;
	if (Oflag) {
		sblock.fs_inodefmt = FS_42INODEFMT;
		sblock.fs_maxsymlinklen = 0;
	} else {
		sblock.fs_inodefmt = FS_44INODEFMT;
		sblock.fs_maxsymlinklen = MAXSYMLINKLEN;
	}
	/*
	 * Validate the given file system size.
	 * Verify that its last block can actually be accessed.
	 */
	if (fssize <= 0)
	  deverr (13, 0, "preposterous size %d", fssize);
	wtfs(fssize - 1, sectorsize, (char *)&sblock);
	/*
	 * collect and verify the sector and track info
	 */
	sblock.fs_nsect = nsectors;
	sblock.fs_ntrak = ntracks;
	if (sblock.fs_ntrak <= 0)
	  deverr (14, 0, "preposterous ntrak %ld", sblock.fs_ntrak);
	if (sblock.fs_nsect <= 0)
	  deverr (15, 0, "preposterous nsect %ld", sblock.fs_nsect);
	/*
	 * collect and verify the block and fragment sizes
	 */
	sblock.fs_bsize = bsize;
	sblock.fs_fsize = fsize;
	if (!POWEROF2(sblock.fs_bsize))
	  deverr (16, 0,
		 "block size must be a power of 2, not %ld",
		 sblock.fs_bsize);
	if (!POWEROF2(sblock.fs_fsize))
	  deverr (17, 0,
		 "fragment size must be a power of 2, not %ld",
		 sblock.fs_fsize);
	if (sblock.fs_fsize < sectorsize)
	  deverr (18, 0,
		 "fragment size %ld is too small, minimum is %d",
		 sblock.fs_fsize, sectorsize);
	if (sblock.fs_bsize < MINBSIZE)
	  deverr (19, 0,
		 "block size %ld is too small, minimum is %d",
		 sblock.fs_bsize, MINBSIZE);
	if (sblock.fs_bsize < sblock.fs_fsize)
	  deverr (20, 0,
		 "block size (%ld) cannot be smaller than fragment size (%ld)",
		 sblock.fs_bsize, sblock.fs_fsize);
	sblock.fs_bmask = ~(sblock.fs_bsize - 1);
	sblock.fs_fmask = ~(sblock.fs_fsize - 1);
	sblock.fs_qbmask = ~sblock.fs_bmask;
	sblock.fs_qfmask = ~sblock.fs_fmask;
	for (sblock.fs_bshift = 0, i = sblock.fs_bsize; i > 1; i >>= 1)
		sblock.fs_bshift++;
	for (sblock.fs_fshift = 0, i = sblock.fs_fsize; i > 1; i >>= 1)
		sblock.fs_fshift++;
	sblock.fs_frag = numfrags(&sblock, sblock.fs_bsize);
	for (sblock.fs_fragshift = 0, i = sblock.fs_frag; i > 1; i >>= 1)
		sblock.fs_fragshift++;
	if (sblock.fs_frag > MAXFRAG)
	  deverr (21, 0,
		 "fragment size %ld is too small, minimum with block size %ld is %ld",
		 sblock.fs_fsize, sblock.fs_bsize,
		 sblock.fs_bsize / MAXFRAG);
	sblock.fs_nrpos = nrpos;
	sblock.fs_nindir = sblock.fs_bsize / sizeof(daddr_t);
	sblock.fs_inopb = sblock.fs_bsize / sizeof(struct dinode);
	sblock.fs_nspf = sblock.fs_fsize / sectorsize;
	for (sblock.fs_fsbtodb = 0, i = NSPF(&sblock); i > 1; i >>= 1)
		sblock.fs_fsbtodb++;
	sblock.fs_sblkno =
	    roundup(howmany(bbsize + sbsize, sblock.fs_fsize), sblock.fs_frag);
	sblock.fs_cblkno = (daddr_t)(sblock.fs_sblkno +
	    roundup(howmany(sbsize, sblock.fs_fsize), sblock.fs_frag));
	sblock.fs_iblkno = sblock.fs_cblkno + sblock.fs_frag;
	sblock.fs_cgoffset = roundup(
	    howmany(sblock.fs_nsect, NSPF(&sblock)), sblock.fs_frag);
	for (sblock.fs_cgmask = 0xffffffff, i = sblock.fs_ntrak; i > 1; i >>= 1)
		sblock.fs_cgmask <<= 1;
	if (!POWEROF2(sblock.fs_ntrak))
		sblock.fs_cgmask <<= 1;
	sblock.fs_maxfilesize = sblock.fs_bsize * NDADDR - 1;
	for (sizepb = sblock.fs_bsize, i = 0; i < NIADDR; i++) {
		sizepb *= NINDIR(&sblock);
		sblock.fs_maxfilesize += sizepb;
	}
	/*
	 * Validate specified/determined secpercyl
	 * and calculate minimum cylinders per group.
	 */
	sblock.fs_spc = secpercyl;
	for (sblock.fs_cpc = NSPB(&sblock), i = sblock.fs_spc;
	     sblock.fs_cpc > 1 && (i & 1) == 0;
	     sblock.fs_cpc >>= 1, i >>= 1)
		/* void */;
	mincpc = sblock.fs_cpc;
	bpcg = sblock.fs_spc * sectorsize;
	inospercg = roundup(bpcg / sizeof(struct dinode), INOPB(&sblock));
	if (inospercg > MAXIPG(&sblock))
		inospercg = MAXIPG(&sblock);
	used = (sblock.fs_iblkno + inospercg / INOPF(&sblock)) * NSPF(&sblock);
	mincpgcnt = howmany(sblock.fs_cgoffset * (~sblock.fs_cgmask) + used,
	    sblock.fs_spc);
	mincpg = roundup(mincpgcnt, mincpc);
	/*
	 * Ensure that cylinder group with mincpg has enough space
	 * for block maps.
	 */
	sblock.fs_cpg = mincpg;
	sblock.fs_ipg = inospercg;
	if (maxcontig > 1)
		sblock.fs_contigsumsize = MIN(maxcontig, FS_MAXCONTIG);
	mapcramped = 0;
	while (CGSIZE(&sblock) > sblock.fs_bsize) {
		mapcramped = 1;
		if (sblock.fs_bsize < MAXBSIZE) {
			sblock.fs_bsize <<= 1;
			if ((i & 1) == 0) {
				i >>= 1;
			} else {
				sblock.fs_cpc <<= 1;
				mincpc <<= 1;
				mincpg = roundup(mincpgcnt, mincpc);
				sblock.fs_cpg = mincpg;
			}
			sblock.fs_frag <<= 1;
			sblock.fs_fragshift += 1;
			if (sblock.fs_frag <= MAXFRAG)
				continue;
		}
		if (sblock.fs_fsize == sblock.fs_bsize)
		  deverr (22, 0,
			 "There is no block size that can support this disk");
		sblock.fs_frag >>= 1;
		sblock.fs_fragshift -= 1;
		sblock.fs_fsize <<= 1;
		sblock.fs_nspf <<= 1;
	}
	/*
	 * Ensure that cylinder group with mincpg has enough space for inodes.
	 */
	inodecramped = 0;
	used *= sectorsize;
	inospercg = roundup((mincpg * bpcg - used) / density, INOPB(&sblock));
	sblock.fs_ipg = inospercg;
	while (inospercg > MAXIPG(&sblock)) {
		inodecramped = 1;
		if (mincpc == 1 || sblock.fs_frag == 1 ||
		    sblock.fs_bsize == MINBSIZE)
			break;
		deverr (0, 0,
		       "With a block size of %ld %s %ld", sblock.fs_bsize,
		       "minimum bytes per inode is",
		       (mincpg * bpcg - used) / MAXIPG(&sblock) + 1);
		sblock.fs_bsize >>= 1;
		sblock.fs_frag >>= 1;
		sblock.fs_fragshift -= 1;
		mincpc >>= 1;
		sblock.fs_cpg = roundup(mincpgcnt, mincpc);
		if (CGSIZE(&sblock) > sblock.fs_bsize) {
			sblock.fs_bsize <<= 1;
			break;
		}
		mincpg = sblock.fs_cpg;
		inospercg =
		    roundup((mincpg * bpcg - used) / density, INOPB(&sblock));
		sblock.fs_ipg = inospercg;
	}
	if (inodecramped) {
		if (inospercg > MAXIPG(&sblock))
			deverr (0, 0, "Minimum bytes per inode is %ld",
			       (mincpg * bpcg - used) / MAXIPG(&sblock) + 1);
		else if (!mapcramped)
		  deverr (0, 0,
			 "With %d bytes per inode,"
			 " minimum cylinders per group is %ld",
			 density, mincpg);
	}
	if (mapcramped)
	  deverr (0, 0,
		 "With %ld sectors per cylinder,"
		 " minimum cylinders per group is %ld",
		 sblock.fs_spc, mincpg);
	if (inodecramped || mapcramped)
	  if (sblock.fs_bsize != bsize)
	    {
	      deverr (0, 0,
		     "This requires the block size to be changed from %d to %ld",
		     bsize, sblock.fs_bsize);
	      deverr (23, 0,
		     "and the fragment size to be changed from %d to %ld",
		     fsize, sblock.fs_fsize);
	    }
	  else
	    exit(23);
	/*
	 * Calculate the number of cylinders per group
	 */
	sblock.fs_cpg = cpg;
	if (sblock.fs_cpg % mincpc != 0) {
		deverr (0, 0,
		       "%s groups must have a multiple of %ld cylinders",
		       cpgflg ? "Cylinder" : "Warning: cylinder", mincpc);
		sblock.fs_cpg = roundup(sblock.fs_cpg, mincpc);
		if (!cpgflg)
			cpg = sblock.fs_cpg;
	}
	/*
	 * Must ensure there is enough space for inodes.
	 */
	sblock.fs_ipg = roundup((sblock.fs_cpg * bpcg - used) / density,
		INOPB(&sblock));
	while (sblock.fs_ipg > MAXIPG(&sblock)) {
		inodecramped = 1;
		sblock.fs_cpg -= mincpc;
		sblock.fs_ipg = roundup((sblock.fs_cpg * bpcg - used) / density,
			INOPB(&sblock));
	}
	/*
	 * Must ensure there is enough space to hold block map.
	 */
	while (CGSIZE(&sblock) > sblock.fs_bsize) {
		mapcramped = 1;
		sblock.fs_cpg -= mincpc;
		sblock.fs_ipg = roundup((sblock.fs_cpg * bpcg - used) / density,
			INOPB(&sblock));
	}
	sblock.fs_fpg = (sblock.fs_cpg * sblock.fs_spc) / NSPF(&sblock);
	if ((sblock.fs_cpg * sblock.fs_spc) % NSPB(&sblock) != 0)
	  deverr (24, 0, "panic (fs_cpg * fs_spc) %% NSPF != 0");
	if (sblock.fs_cpg < mincpg)
	  deverr (25, 0,
		 "cylinder groups must have at least %ld cylinders", mincpg);
	else if (sblock.fs_cpg != cpg)
	  {
	    if (cpgflg && !mapcramped && !inodecramped)
	      exit(26);
	    deverr (0, 0,
		   "%s%s cylinders per group to %ld",
		   (cpgflg ? "" : "Warning: "),
		   ((mapcramped && inodecramped)
		    ? "Block size and bytes per inode restrict"
		    : mapcramped ? "Block size restricts"
		    : "Bytes per inode restrict"),
		   sblock.fs_cpg);
	    if (cpgflg)
	      exit(27);
	  }
	sblock.fs_cgsize = fragroundup(&sblock, CGSIZE(&sblock));
	/*
	 * Now have size for file system and nsect and ntrak.
	 * Determine number of cylinders and blocks in the file system.
	 */
	sblock.fs_size = fssize = dbtofsb(&sblock, fssize);
	sblock.fs_ncyl = fssize * NSPF(&sblock) / sblock.fs_spc;
	if (fssize * NSPF(&sblock) > sblock.fs_ncyl * sblock.fs_spc) {
		sblock.fs_ncyl++;
		warn = 1;
	}
	if (sblock.fs_ncyl < 1)
	  deverr (28, 0, "file systems must have at least one cylinder");
	/*
	 * Determine feasability/values of rotational layout tables.
	 *
	 * The size of the rotational layout tables is limited by the
	 * size of the superblock, SBSIZE. The amount of space available
	 * for tables is calculated as (SBSIZE - sizeof (struct fs)).
	 * The size of these tables is inversely proportional to the block
	 * size of the file system. The size increases if sectors per track
	 * are not powers of two, because more cylinders must be described
	 * by the tables before the rotational pattern repeats (fs_cpc).
	 */
	sblock.fs_interleave = interleave;
	sblock.fs_trackskew = trackskew;
	sblock.fs_npsect = nphyssectors;
	sblock.fs_postblformat = FS_DYNAMICPOSTBLFMT;
	sblock.fs_sbsize = fragroundup(&sblock, sizeof(struct fs));
	if (sblock.fs_ntrak == 1) {
		sblock.fs_cpc = 0;
		goto next;
	}
	postblsize = sblock.fs_nrpos * sblock.fs_cpc * sizeof(short);
	rotblsize = sblock.fs_cpc * sblock.fs_spc / NSPB(&sblock);
	totalsbsize = sizeof(struct fs) + rotblsize;
	if (sblock.fs_nrpos == 8 && sblock.fs_cpc <= 16) {
		/* use old static table space */
		sblock.fs_postbloff = (char *)(&sblock.fs_opostbl[0][0]) -
		    (char *)(&sblock.fs_link);
		sblock.fs_rotbloff = &sblock.fs_space[0] -
		    (u_char *)(&sblock.fs_link);
	} else {
		/* use dynamic table space */
		sblock.fs_postbloff = &sblock.fs_space[0] -
		    (u_char *)(&sblock.fs_link);
		sblock.fs_rotbloff = sblock.fs_postbloff + postblsize;
		totalsbsize += postblsize;
	}
	if (totalsbsize > SBSIZE ||
	    sblock.fs_nsect > (1 << NBBY) * NSPB(&sblock))
	  {
	    deverr (0, 0,
		   "Warning: insufficient space in super block for "
		   "rotational layout tables with nsect %ld and ntrak %ld",
		   sblock.fs_nsect, sblock.fs_ntrak);
	    deverr (0, 0, "File system performance may be impaired");
	    sblock.fs_cpc = 0;
	    goto next;
	  }
	sblock.fs_sbsize = fragroundup(&sblock, totalsbsize);
	/*
	 * calculate the available blocks for each rotational position
	 */
	for (cylno = 0; cylno < sblock.fs_cpc; cylno++)
		for (rpos = 0; rpos < sblock.fs_nrpos; rpos++)
			fs_postbl(&sblock, cylno)[rpos] = -1;
	for (i = (rotblsize - 1) * sblock.fs_frag;
	     i >= 0; i -= sblock.fs_frag) {
		cylno = cbtocylno(&sblock, i);
		rpos = cbtorpos(&sblock, i);
		blk = fragstoblks(&sblock, i);
		if (fs_postbl(&sblock, cylno)[rpos] == -1)
			fs_rotbl(&sblock)[blk] = 0;
		else
			fs_rotbl(&sblock)[blk] =
			    fs_postbl(&sblock, cylno)[rpos] - blk;
		fs_postbl(&sblock, cylno)[rpos] = blk;
	}
next:
	/*
	 * Compute/validate number of cylinder groups.
	 */
	sblock.fs_ncg = sblock.fs_ncyl / sblock.fs_cpg;
	if (sblock.fs_ncyl % sblock.fs_cpg)
		sblock.fs_ncg++;
	sblock.fs_dblkno = sblock.fs_iblkno + sblock.fs_ipg / INOPF(&sblock);
	i = MIN(~sblock.fs_cgmask, sblock.fs_ncg - 1);
	if (cgdmin(&sblock, i) - cgbase(&sblock, i) >= sblock.fs_fpg)
	  {
	    deverr (0, 0,
		   "Inode blocks/cyl group (%ld) >= data blocks (%ld)",
		   cgdmin(&sblock, i) - cgbase(&sblock, i) / sblock.fs_frag,
		   sblock.fs_fpg / sblock.fs_frag);
	    deverr (29, 0,
		   "number of cylinders per cylinder group (%ld)"
		   " must be increased", sblock.fs_cpg);
	  }
	j = sblock.fs_ncg - 1;
	if ((i = fssize - j * sblock.fs_fpg) < sblock.fs_fpg &&
	    cgdmin(&sblock, j) - cgbase(&sblock, j) > i) {
		if (j == 0)
		  deverr (30, 0,
			 "Filesystem must have at least %ld sectors",
			 NSPF(&sblock)
			  * (cgdmin(&sblock, 0) + 3 * sblock.fs_frag));
		deverr (0, 0,
		       "Warning: inode blocks/cyl group (%ld) >="
		       " data blocks (%ld) in last cylinder group.",
		       ((cgdmin(&sblock, j) - cgbase(&sblock, j))
			/ sblock.fs_frag),
		       i / sblock.fs_frag);
		deverr (0, 0,
		       "This implies %ld sector(s) cannot be allocated",
		       i * NSPF(&sblock));
		sblock.fs_ncg--;
		sblock.fs_ncyl -= sblock.fs_ncyl % sblock.fs_cpg;
		sblock.fs_size = fssize = sblock.fs_ncyl * sblock.fs_spc /
		    NSPF(&sblock);
		warn = 0;
	}
	if (warn)
	  deverr (0, 0,
		 "Warning: %ld sector(s) in last cylinder unallocated",
		 sblock.fs_spc
		 - (fssize * NSPF(&sblock) - (sblock.fs_ncyl - 1)
		    * sblock.fs_spc));
	/*
	 * fill in remaining fields of the super block
	 */
	sblock.fs_csaddr = cgdmin(&sblock, 0);
	sblock.fs_cssize =
	    fragroundup(&sblock, sblock.fs_ncg * sizeof(struct csum));
	i = sblock.fs_bsize / sizeof(struct csum);
	sblock.fs_csmask = ~(i - 1);
	for (sblock.fs_csshift = 0; i > 1; i >>= 1)
		sblock.fs_csshift++;
	fscs = (struct csum *)calloc(1, sblock.fs_cssize);
	sblock.fs_magic = FS_MAGIC;
	sblock.fs_rotdelay = rotdelay;
	sblock.fs_minfree = minfree;
	sblock.fs_maxcontig = maxcontig;
	sblock.fs_headswitch = headswitch;
	sblock.fs_trkseek = trackseek;
	sblock.fs_maxbpg = maxbpg;
	sblock.fs_rps = rpm / 60;
	sblock.fs_optim = opt;
	sblock.fs_cgrotor = 0;
	sblock.fs_cstotal.cs_ndir = 0;
	sblock.fs_cstotal.cs_nbfree = 0;
	sblock.fs_cstotal.cs_nifree = 0;
	sblock.fs_cstotal.cs_nffree = 0;
	sblock.fs_fmod = 0;
	sblock.fs_ronly = 0;
	sblock.fs_clean = 1;

	/*
	 * Dump out summary information about file system.
	 */
	printf("%s:\n\t%ld sectors in %ld %s of %ld tracks, %ld sectors\n",
	    fsys, sblock.fs_size * NSPF(&sblock), sblock.fs_ncyl,
	    "cylinders", sblock.fs_ntrak, sblock.fs_nsect);
#define B2MBFACTOR (1 / (1024.0 * 1024.0))
	printf("\t%.1fMB in %ld cyl groups (%ld c/g, %.2fMB/g, %ld i/g)\n",
	    (float)sblock.fs_size * sblock.fs_fsize * B2MBFACTOR,
	    sblock.fs_ncg, sblock.fs_cpg,
	    (float)sblock.fs_fpg * sblock.fs_fsize * B2MBFACTOR,
	    sblock.fs_ipg);
#undef B2MBFACTOR

	/*
	 * Now build the cylinders group blocks and
	 * then print out indices of cylinder groups.
	 */
	printf("\tsuperblock backups at:");
	for (cylno = 0; cylno < sblock.fs_ncg; cylno++) {
		initcg(cylno, utime);
		if (cylno % 8 == 0)
			printf("\n\t");
		printf(" %ld,", fsbtodb(&sblock, cgsblock(&sblock, cylno)));
	}
	printf("\n");
	if (Nflag)
		exit(0);
	/*
	 * Now construct the initial file system,
	 * then write out the super-block.
	 */
	fsinit(utime);
	sblock.fs_time = utime;
	wtfs((int)SBOFF / sectorsize, sbsize, (char *)&sblock);
	for (i = 0; i < sblock.fs_cssize; i += sblock.fs_bsize)
		wtfs(fsbtodb(&sblock, sblock.fs_csaddr + numfrags(&sblock, i)),
			sblock.fs_cssize - i < sblock.fs_bsize ?
			    sblock.fs_cssize - i : sblock.fs_bsize,
			((char *)fscs) + i);
	/*
	 * Write out the duplicate super blocks
	 */
	for (cylno = 0; cylno < sblock.fs_ncg; cylno++)
		wtfs(fsbtodb(&sblock, cgsblock(&sblock, cylno)),
		    sbsize, (char *)&sblock);
#if 0 /* Not in Hurd (yet) */
	/*
	 * Update information about this partion in pack
	 * label, to that it may be updated on disk.
	 */
	pp->p_fstype = FS_BSDFFS;
	pp->p_fsize = sblock.fs_fsize;
	pp->p_frag = sblock.fs_frag;
	pp->p_cpg = sblock.fs_cpg;
#endif
}

/*
 * Initialize a cylinder group.
 */
void
initcg(cylno, utime)
	int cylno;
	time_t utime;
{
	long i;
	daddr_t cbase, d, dlower, dupper, dmax, blkno;
	register struct csum *cs;

	/*
	 * Determine block bounds for cylinder group.
	 * Allow space for super block summary information in first
	 * cylinder group.
	 */
	cbase = cgbase(&sblock, cylno);
	dmax = cbase + sblock.fs_fpg;
	if (dmax > sblock.fs_size)
		dmax = sblock.fs_size;
	dlower = cgsblock(&sblock, cylno) - cbase;
	dupper = cgdmin(&sblock, cylno) - cbase;
	if (cylno == 0)
		dupper += howmany(sblock.fs_cssize, sblock.fs_fsize);
	cs = fscs + cylno;
	bzero(&acg, sblock.fs_cgsize);
	acg.cg_time = utime;
	acg.cg_magic = CG_MAGIC;
	acg.cg_cgx = cylno;
	if (cylno == sblock.fs_ncg - 1)
		acg.cg_ncyl = sblock.fs_ncyl % sblock.fs_cpg;
	else
		acg.cg_ncyl = sblock.fs_cpg;
	acg.cg_niblk = sblock.fs_ipg;
	acg.cg_ndblk = dmax - cbase;
	if (sblock.fs_contigsumsize > 0)
		acg.cg_nclusterblks = acg.cg_ndblk / sblock.fs_frag;
	acg.cg_btotoff = &acg.cg_space[0] - (u_char *)(&acg.cg_link);
	acg.cg_boff = acg.cg_btotoff + sblock.fs_cpg * sizeof(long);
	acg.cg_iusedoff = acg.cg_boff +
		sblock.fs_cpg * sblock.fs_nrpos * sizeof(short);
	acg.cg_freeoff = acg.cg_iusedoff + howmany(sblock.fs_ipg, NBBY);
	if (sblock.fs_contigsumsize <= 0) {
		acg.cg_nextfreeoff = acg.cg_freeoff +
		   howmany(sblock.fs_cpg * sblock.fs_spc / NSPF(&sblock), NBBY);
	} else {
		acg.cg_clustersumoff = acg.cg_freeoff + howmany
		    (sblock.fs_cpg * sblock.fs_spc / NSPF(&sblock), NBBY) -
		    sizeof(long);
		acg.cg_clustersumoff =
		    roundup(acg.cg_clustersumoff, sizeof(long));
		acg.cg_clusteroff = acg.cg_clustersumoff +
		    (sblock.fs_contigsumsize + 1) * sizeof(long);
		acg.cg_nextfreeoff = acg.cg_clusteroff + howmany
		    (sblock.fs_cpg * sblock.fs_spc / NSPB(&sblock), NBBY);
	}
	if (acg.cg_nextfreeoff - (long)(&acg.cg_link) > sblock.fs_cgsize)
	  deverr (37, 0, "Panic: cylinder group too big");
	acg.cg_cs.cs_nifree += sblock.fs_ipg;
	if (cylno == 0)
		for (i = 0; i < ROOTINO; i++) {
			setbit(cg_inosused(&acg), i);
			acg.cg_cs.cs_nifree--;
		}
	for (i = 0; i < sblock.fs_ipg / INOPF(&sblock); i += sblock.fs_frag)
		wtfs(fsbtodb(&sblock, cgimin(&sblock, cylno) + i),
		    sblock.fs_bsize, (char *)zino);
	if (cylno > 0) {
		/*
		 * In cylno 0, beginning space is reserved
		 * for boot and super blocks.
		 */
		for (d = 0; d < dlower; d += sblock.fs_frag) {
			blkno = d / sblock.fs_frag;
			setblock(&sblock, cg_blksfree(&acg), blkno);
			if (sblock.fs_contigsumsize > 0)
				setbit(cg_clustersfree(&acg), blkno);
			acg.cg_cs.cs_nbfree++;
			cg_blktot(&acg)[cbtocylno(&sblock, d)]++;
			cg_blks(&sblock, &acg, cbtocylno(&sblock, d))
			    [cbtorpos(&sblock, d)]++;
		}
		sblock.fs_dsize += dlower;
	}
	sblock.fs_dsize += acg.cg_ndblk - dupper;
	i = dupper % sblock.fs_frag;
	if (i) {
		acg.cg_frsum[sblock.fs_frag - i]++;
		for (d = dupper + sblock.fs_frag - i; dupper < d; dupper++) {
			setbit(cg_blksfree(&acg), dupper);
			acg.cg_cs.cs_nffree++;
		}
	}
	for (d = dupper; d + sblock.fs_frag <= dmax - cbase; ) {
		blkno = d / sblock.fs_frag;
		setblock(&sblock, cg_blksfree(&acg), blkno);
		if (sblock.fs_contigsumsize > 0)
			setbit(cg_clustersfree(&acg), blkno);
		acg.cg_cs.cs_nbfree++;
		cg_blktot(&acg)[cbtocylno(&sblock, d)]++;
		cg_blks(&sblock, &acg, cbtocylno(&sblock, d))
		    [cbtorpos(&sblock, d)]++;
		d += sblock.fs_frag;
	}
	if (d < dmax - cbase) {
		acg.cg_frsum[dmax - cbase - d]++;
		for (; d < dmax - cbase; d++) {
			setbit(cg_blksfree(&acg), d);
			acg.cg_cs.cs_nffree++;
		}
	}
	if (sblock.fs_contigsumsize > 0) {
		long *sump = cg_clustersum(&acg);
		u_char *mapp = cg_clustersfree(&acg);
		int map = *mapp++;
		int bit = 1;
		int run = 0;

		for (i = 0; i < acg.cg_nclusterblks; i++) {
			if ((map & bit) != 0) {
				run++;
			} else if (run != 0) {
				if (run > sblock.fs_contigsumsize)
					run = sblock.fs_contigsumsize;
				sump[run]++;
				run = 0;
			}
			if ((i & (NBBY - 1)) != (NBBY - 1)) {
				bit <<= 1;
			} else {
				map = *mapp++;
				bit = 1;
			}
		}
		if (run != 0) {
			if (run > sblock.fs_contigsumsize)
				run = sblock.fs_contigsumsize;
			sump[run]++;
		}
	}
	sblock.fs_cstotal.cs_ndir += acg.cg_cs.cs_ndir;
	sblock.fs_cstotal.cs_nffree += acg.cg_cs.cs_nffree;
	sblock.fs_cstotal.cs_nbfree += acg.cg_cs.cs_nbfree;
	sblock.fs_cstotal.cs_nifree += acg.cg_cs.cs_nifree;
	*cs = acg.cg_cs;
	wtfs(fsbtodb(&sblock, cgtod(&sblock, cylno)),
		sblock.fs_bsize, (char *)&acg);
}

/*
 * initialize the file system
 */
struct dinode node;

#ifdef LOSTDIR
#define PREDEFDIR 3
#else
#define PREDEFDIR 2
#endif

struct directory_entry root_dir[] = {
	{ ROOTINO, sizeof(struct directory_entry), DT_DIR, 1, "." },
	{ ROOTINO, sizeof(struct directory_entry), DT_DIR, 2, ".." },
#ifdef LOSTDIR
	{ LOSTFOUNDINO, sizeof(struct directory_entry), DT_DIR, 10, "lost+found" },
#endif
};
struct odirectory_entry {
	u_long	d_ino;
	u_short	d_reclen;
	u_short	d_namlen;
	u_char	d_name[MAXNAMLEN + 1];
} oroot_dir[] = {
	{ ROOTINO, sizeof(struct directory_entry), 1, "." },
	{ ROOTINO, sizeof(struct directory_entry), 2, ".." },
#ifdef LOSTDIR
	{ LOSTFOUNDINO, sizeof(struct directory_entry), 10, "lost+found" },
#endif
};
#ifdef LOSTDIR
struct directory_entry lost_found_dir[] = {
	{ LOSTFOUNDINO, sizeof(struct directory_entry), DT_DIR, 1, "." },
	{ ROOTINO, sizeof(struct directory_entry), DT_DIR, 2, ".." },
	{ 0, DIRBLKSIZ, 0, 0, 0 },
};
struct odirectory_entry olost_found_dir[] = {
	{ LOSTFOUNDINO, sizeof(struct directory_entry), 1, "." },
	{ ROOTINO, sizeof(struct directory_entry), 2, ".." },
	{ 0, DIRBLKSIZ, 0, 0 },
};
#endif
char buf[MAXBSIZE];

void
fsinit(utime)
	time_t utime;
{
	/*
	 * initialize the node
	 */
	node.di_atime.tv_sec = utime;
	node.di_mtime.tv_sec = utime;
	node.di_ctime.tv_sec = utime;
#ifdef LOSTDIR
	/*
	 * create the lost+found directory
	 */
	if (Oflag) {
		(void)makedir((struct directory_entry *)olost_found_dir, 2);
		for (i = DIRBLKSIZ; i < sblock.fs_bsize; i += DIRBLKSIZ)
			bcopy(&olost_found_dir[2], &buf[i],
			    DIRSIZ(0, &olost_found_dir[2]));
	} else {
		(void)makedir(lost_found_dir, 2);
		for (i = DIRBLKSIZ; i < sblock.fs_bsize; i += DIRBLKSIZ)
			bcopy(&lost_found_dir[2], &buf[i],
			    DIRSIZ(0, &lost_found_dir[2]));
	}
	node.di_model = ifdir | UMASK;
	node.di_modeh = 0;
	node.di_nlink = 2;
	node.di_size = sblock.fs_bsize;
	node.di_db[0] = alloc(node.di_size, DI_MODE (&node));
	node.di_blocks = btodb(fragroundup(&sblock, node.di_size));
	wtfs(fsbtodb(&sblock, node.di_db[0]), node.di_size, buf);
	iput(&node, LOSTFOUNDINO);
#endif
	/*
	 * create the root directory
	 */
	node.di_model = IFDIR | UMASK;
	node.di_modeh = 0;
	node.di_nlink = PREDEFDIR;

	/* Set the uid/gid to non-root if run by a non-root user.  This
	   is what mke2fs does in e2fsprogs-1.18 (actually it uses the
	   real IDs iff geteuid()!=0, but that is just wrong).  */
	node.di_uid = geteuid();
	if (node.di_uid != 0)
	  node.di_gid = getegid();

	if (Oflag)
		node.di_size = makedir((struct directory_entry *)oroot_dir, PREDEFDIR);
	else
		node.di_size = makedir(root_dir, PREDEFDIR);
	node.di_db[0] = alloc(sblock.fs_fsize, DI_MODE (&node));
	node.di_blocks = btodb(fragroundup(&sblock, node.di_size));
	wtfs(fsbtodb(&sblock, node.di_db[0]), sblock.fs_fsize, buf);
	iput(&node, ROOTINO);
}

/*
 * construct a set of directory entries in "buf".
 * return size of directory.
 */
int
makedir(protodir, entries)
	register struct directory_entry *protodir;
	int entries;
{
	char *cp;
	int i, spcleft;

	spcleft = DIRBLKSIZ;
	for (cp = buf, i = 0; i < entries - 1; i++) {
		protodir[i].d_reclen = DIRSIZ(0, &protodir[i]);
		bcopy(&protodir[i], cp, protodir[i].d_reclen);
		cp += protodir[i].d_reclen;
		spcleft -= protodir[i].d_reclen;
	}
	protodir[i].d_reclen = spcleft;
	bcopy(&protodir[i], cp, DIRSIZ(0, &protodir[i]));
	return (DIRBLKSIZ);
}

/*
 * allocate a block or frag
 */
daddr_t
alloc(size, mode)
	int size;
	int mode;
{
	int i, frag;
	daddr_t d, blkno;

	rdfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	if (acg.cg_magic != CG_MAGIC) {
		deverr (0, 0, "cg 0: bad magic number");
		return (0);
	}
	if (acg.cg_cs.cs_nbfree == 0) {
		deverr (0, 0, "first cylinder group ran out of space");
		return (0);
	}
	for (d = 0; d < acg.cg_ndblk; d += sblock.fs_frag)
		if (isblock(&sblock, cg_blksfree(&acg), d / sblock.fs_frag))
			goto goth;
	deverr (0, 0, "internal error: can't find block in cyl 0");
	return (0);
goth:
	blkno = fragstoblks(&sblock, d);
	clrblock(&sblock, cg_blksfree(&acg), blkno);
	if (sblock.fs_contigsumsize > 0)
		clrbit(cg_clustersfree(&acg), blkno);
	acg.cg_cs.cs_nbfree--;
	sblock.fs_cstotal.cs_nbfree--;
	fscs[0].cs_nbfree--;
	if (mode & IFDIR) {
		acg.cg_cs.cs_ndir++;
		sblock.fs_cstotal.cs_ndir++;
		fscs[0].cs_ndir++;
	}
	cg_blktot(&acg)[cbtocylno(&sblock, d)]--;
	cg_blks(&sblock, &acg, cbtocylno(&sblock, d))[cbtorpos(&sblock, d)]--;
	if (size != sblock.fs_bsize) {
		frag = howmany(size, sblock.fs_fsize);
		fscs[0].cs_nffree += sblock.fs_frag - frag;
		sblock.fs_cstotal.cs_nffree += sblock.fs_frag - frag;
		acg.cg_cs.cs_nffree += sblock.fs_frag - frag;
		acg.cg_frsum[sblock.fs_frag - frag]++;
		for (i = frag; i < sblock.fs_frag; i++)
			setbit(cg_blksfree(&acg), d + i);
	}
	wtfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	return (d);
}

/*
 * Allocate an inode on the disk
 */
void
iput(ip, ino)
	register struct dinode *ip;
	register ino_t ino;
{
	struct dinode buf[MAXINOPB];
	daddr_t d;
	int c;

	c = ino_to_cg(&sblock, ino);
	rdfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	if (acg.cg_magic != CG_MAGIC)
	  deverr (31, 0, "cg 0: bad magic number");
	acg.cg_cs.cs_nifree--;
	setbit(cg_inosused(&acg), ino);
	wtfs(fsbtodb(&sblock, cgtod(&sblock, 0)), sblock.fs_cgsize,
	    (char *)&acg);
	sblock.fs_cstotal.cs_nifree--;
	fscs[0].cs_nifree--;
	if (ino >= sblock.fs_ipg * sblock.fs_ncg)
	  deverr (32, 0, "fsinit: inode value out of range (%Ld)", ino);
	d = fsbtodb(&sblock, ino_to_fsba(&sblock, ino));
	rdfs(d, sblock.fs_bsize, buf);
	buf[ino_to_fsbo(&sblock, ino)] = *ip;
	wtfs(d, sblock.fs_bsize, buf);
}

/*
 * read a block from the file system
 */
void
rdfs(bno, size, bf)
	daddr_t bno;
	int size;
	char *bf;
{
	int n;
	if (lseek(fsi, (off_t)bno * sectorsize, 0) < 0)
	  deverr (33, errno, "rdfs: %ld: seek error", bno);
	n = read(fsi, bf, size);
	if (n != size)
	  deverr (34, errno, "rdfs: %ld: read error", bno);
}

/*
 * write a block to the file system
 */
void
wtfs(bno, size, bf)
	daddr_t bno;
	int size;
	char *bf;
{
	int n;
	if (Nflag)
		return;
	if (lseek(fso, (off_t)bno * sectorsize, SEEK_SET) < 0)
	  deverr (35, errno, "wtfs: %ld: seek error", bno);
	n = write(fso, bf, size);
	if (n != size)
	  deverr (36, errno, "wtfs: %ld: write error", bno);
}

/*
 * check if a block is available
 */
int
isblock(fs, cp, h)
	struct fs *fs;
	unsigned char *cp;
	int h;
{
	unsigned char mask;

	switch (fs->fs_frag) {
	case 8:
		return (cp[h] == 0xff);
	case 4:
		mask = 0x0f << ((h & 0x1) << 2);
		return ((cp[h >> 1] & mask) == mask);
	case 2:
		mask = 0x03 << ((h & 0x3) << 1);
		return ((cp[h >> 2] & mask) == mask);
	case 1:
		mask = 0x01 << (h & 0x7);
		return ((cp[h >> 3] & mask) == mask);
	default:
		deverr (0, 0, "isblock bad fs_frag %ld", fs->fs_frag);
		return (0);
	}
}

/*
 * take a block out of the map
 */
void
clrblock(fs, cp, h)
	struct fs *fs;
	unsigned char *cp;
	int h;
{
	switch ((fs)->fs_frag) {
	case 8:
		cp[h] = 0;
		return;
	case 4:
		cp[h >> 1] &= ~(0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] &= ~(0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] &= ~(0x01 << (h & 0x7));
		return;
	default:
		deverr (0, 0, "clrblock bad fs_frag %ld", fs->fs_frag);
		return;
	}
}

/*
 * put a block into the map
 */
void
setblock(fs, cp, h)
	struct fs *fs;
	unsigned char *cp;
	int h;
{
	switch (fs->fs_frag) {
	case 8:
		cp[h] = 0xff;
		return;
	case 4:
		cp[h >> 1] |= (0x0f << ((h & 0x1) << 2));
		return;
	case 2:
		cp[h >> 2] |= (0x03 << ((h & 0x3) << 1));
		return;
	case 1:
		cp[h >> 3] |= (0x01 << (h & 0x7));
		return;
	default:
		deverr (0, 0, "setblock bad fs_frag %ld", fs->fs_frag);
		return;
	}
}
