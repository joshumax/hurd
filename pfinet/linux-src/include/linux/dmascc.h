/*
 * $Id: dmascc.h,v 1.1 1997/12/01 10:44:55 oe1kib Exp $
 *
 * Driver for high-speed SCC boards (those with DMA support)
 * Copyright (C) 1997 Klaus Kudielka
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Ioctls */
#define SIOCGSCCPARAM SIOCDEVPRIVATE
#define SIOCSSCCPARAM (SIOCDEVPRIVATE+1)

/* Frequency of timer 0 */
#define TMR_0_HZ      25600

/* Configurable parameters */
struct scc_param {
  int pclk_hz;  /* frequency of BRG input (read-only - don't change) */
  int brg_tc;   /* baud rate generator terminal count - BRG disabled if < 0 */
  int nrzi;     /* 0 (nrz), 1 (nrzi) */
  int clocks;   /* see documentation */
  int txdelay;  /* [1/TMR_0_HZ] */
  int txtime;   /* [1/HZ] */
  int sqdelay;  /* [1/TMR_0_HZ] */
  int waittime; /* [1/TMR_0_HZ] */
  int slottime; /* [1/TMR_0_HZ] */
  int persist;  /* 0 ... 255 */
  int dma;      /* 1, 3 */
};
