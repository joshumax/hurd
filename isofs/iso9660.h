/* 
   Copyright (C) 1997 Free Software Foundation, Inc.
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

/* Specification of ISO 9660 format */

/* Volume descriptor */

struct voldesc 
{
  unsigned char type;
  unsigned char id[5];
  unsigned char version;
  unsigned char data[0];
};

/* Volume descriptor types */
#define VOLDESC_PRIMARY 1
#define VOLDESC_END 255

/* We don't support any other types */

/* Expected ID */
#define ISO_STANDARD_ID "CD001"

/* Primary descriptor */
struct sblock
{
  unsigned char type;
  unsigned char id[5];
  unsigned char version;
  unsigned char skip1;
  unsigned char sysid[32];
  unsigned char volid[32];
  unsigned char skip2[8];
  unsigned char vol_sp_size[8];		/* total number of logical blocks */
  unsigned char skip[32];
  unsigned char vol_set_size[4];
  unsigned char vol_seqno[4];
  unsigned char blksize[4];		/* logical block size */
  unsigned char ptsize[8];
  unsigned char type_l_pt[4];
  unsigned char opt_type_l_pt[4];
  unsigned char type_m_pt[4];
  unsigned char opt_type_m_pt[4];
  unsigned char root[34];
  unsigned char volset_id[128];
  unsigned char pub_id[128];
  unsigned char prep_id[128];
  unsigned char app_id[128];
  unsigned char copyr_id[37];
  unsigned char abstr_id[37];
  unsigned char biblio_id[37];
  unsigned char creation_time[17];
  unsigned char mod_time[17];
  unsigned char expir_time[17];
  unsigned char effect_time[17];
  unsigned char file_structure;
  unsigned char skip4;
  unsigned char appl_data[512];
  unsigned char skip5[652];
};

/* Directory record */
struct dirrect
{
  unsigned char len;
  unsigned char ext_attr_len;
  unsigned char extent[8];
  unsigned char size[8];
  unsigned char date[7];
  unsigned char flags;
  unsigned char file_unit_size;
  unsigned char ileave;
  unsigned char vol_seqno[4];
  unsigned char namelen;
  unsigned char name[0];
};



/* Numeric conversions for these fields */

#include <endian.h>

static inline unsigned int 
isonum_733 (unsigned char *addr)
{
#if BYTE_ORDER == LITTLE_ENDIAN
  return *(unsigned int *)addr;
#elif BYTE_ORDER == BIG_ENDIAN
  return *(unsigned int *)(addr + 4);
#else
  return
    addr[0] | (addr[1] << 8) | (addr[2] << 16) | (addr[3] << 24);
#endif
}

static inline unsigned int
isonum_723 (unsigned char *addr)
{
#if BYTE_ORDER == LITTLE_ENDIAN
  return *(unsigned short *)addr;
#elif BYTE_ORDER == BIG_ENDIAN
  return *(unsigned short *)addr + 2;
#else
  return addr[0] | (addr[1] << 8);
#endif
}
