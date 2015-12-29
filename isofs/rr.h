/*
   Copyright (C) 1997, 1999 Free Software Foundation, Inc.
   Written by Thomas Bushnell, n/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include "iso9660.h"

/* The results of an rrip_scan_lookup call are one of these */
struct rrip_lookup
{
  /* PX */
  mode_t mode;
  nlink_t nlink;
  uid_t uid;
  gid_t gid;

  /* PN */
  dev_t rdev;

  /* SL */
  char *target;

  /* NM */
  char *name;			/* name of this entry if changed (malloced) */

  /* CL */
  off_t newloc;			/* relocated directory */

  /* PL */
  off_t parloc;			/* parent of relocated directory */

  /* TF */
  int tfflags;
  struct timespec atime, mtime, ctime;	/* file times */

  /* CL */
  struct dirrect *realdirent;	/* actual directory entry for attributes */

  /* RL */
  off_t realfilestart;		/* override file start in dir entry */

  /* AU */
  uid_t author;

  /* TR */
  size_t translen;
  char *trans;

  /* MD */
  mode_t allmode;

  /* FL */
  long flags;

  int valid;
};

/* VALID in one of these is from the following bits */
#define VALID_PX	0x0001
#define VALID_PN	0x0002
#define VALID_SL	0x0004
#define VALID_NM	0x0008
#define VALID_CL	0x0010
#define VALID_PL	0x0020
#define VALID_TF	0x0040
#define VALID_RE	0x0080
#define VALID_AU	0x0100
#define VALID_TR	0x0200
#define VALID_MD	0x0400
#define VALID_FL	0x0800


/* Definitions for System Use Sharing Protocol.
   Version 1.  Revision 1.10.  Dated July 16, 1993. */

/* A system use field begins with the following header */
struct su_header
{
  char sig[2];
  unsigned char len;
  char version;
};

/* The body of a CE (Continuation Area) field */
struct su_ce
{
  unsigned char continuation[8];
  unsigned char offset[8];
  unsigned char size[8];
};

/* The body of a SP (Sharing Protocol Indicator) field */
struct su_sp
{
  unsigned char check[2];
  u_char skip;
};

#define SU_SP_CHECK_0 0xbe
#define SU_SP_CHECK_1 0xef

/* The body of a ER (Extension Reference) field */
struct su_er
{
  u_char len_id;
  u_char len_des;
  u_char len_src;
  u_char ext_ver;
  char more[0];
};




/* Definitions for Rock Ridge extensions.
   Version 1.  Revision 1.10.  Dated July 13, 1993. */

/* These are the ER values to indicate the presence of Rock-Ridge
   extensions. */
#define ROCK_VERS	1
#define ROCK_ID		"RRIP_1991A"
#define ROCK_DES	\
    "THE ROCK RIDGE INTERCHANGE PROTOCOL PROVIDES SUPPORT FOR POSIX FILE SYSTEM SEMANTICS"
#define ROCK_SRC	\
    "ROCK RIDGE SPECIFICATION VERSION 1 REVISION 1.10 JULY 13 1993"

/* The body of a PX (Posix Attributes) field. */
struct rr_px
{
  unsigned char mode[8];
  unsigned char nlink[8];
  unsigned char uid[8];
  unsigned char gid[8];
};

/* The body of a PN (Posix Device Node) field. */
struct rr_pn
{
  unsigned char high[8];
  unsigned char low[8];
};

/* The body of a SL (Symbolic Link) field. */
struct rr_sl
{
  u_char flags;
  char data[0];
};

/* Each component in the DATA is: */
struct rr_sl_comp
{
  u_char flags;
  u_char len;
  char name[0];
};

/* The body of a NM (Alternate Name) field. */
struct rr_nm
{
  u_char flags;
  char name[0];
};

/* Flags for SL and NM components */
#define NAME_CONTINUE	0x01
#define NAME_DOT	0x02
#define NAME_DOTDOT	0x04
#define NAME_ROOT	0x08
#define NAME_VOLROOT	0x10
#define NAME_HOST	0x20

/* The body of a CL (Child Directory Location) field. */
struct rr_cl
{
  unsigned char loc[8];
};

/* The body of a PL (Parent Directory Location) field. */
struct rr_pl
{
  unsigned char loc[8];
};

/* The body of a TF (Time Stamp) field. */
struct rr_tf
{
  u_char flags;
  char data[0];
};

/* Flags for a TF */
#define TF_CREATION	0x01
#define TF_MODIFY	0x02
#define TF_ACCESS	0x04
#define TF_ATTRIBUTES	0x08
#define TF_BACKUP	0x10
#define TF_EXPIRATION	0x20
#define TF_EFFECTIVE	0x40
#define TF_LONG_FORM	0x80


/* The body of a SF (Sparse File) field. */
struct rr_sf
{
  char size[8];
};


/* GNU extensions */

#define GNUEXT_VERS	1
#define GNUEXT_ID	"GNUEXT_1997"
#define GNUEXT_DES \
   "The GNU Extensions provide support for special GNU filesystem features"
#define GNUEXT_SRC \
   "GNU Hurd source release 0.3 or later"

/* AU -- author (version 1) */
struct gn_au
{
  unsigned char author[8];
};

/* TR -- translator (version 1) */
struct gn_tr
{
  u_char len;
  char data[0];
};

/* MD -- full mode (version 1) */
struct gn_md
{
  unsigned char mode[8];
};

/* FL -- flags (version 1) */
struct gn_fl
{
  unsigned char flags[8];
};


/* Rock-Ridge related functions. */

int rrip_match_lookup (struct dirrect *, const char *,
		       size_t, struct rrip_lookup *);
void rrip_lookup (struct dirrect *, struct rrip_lookup *, int);
void rrip_initialize (struct dirrect *);
void release_rrip (struct rrip_lookup *);
