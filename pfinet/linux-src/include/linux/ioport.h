/*
 * portio.h	Definitions of routines for detecting, reserving and
 *		allocating system resources.
 *
 * Version:	0.01	8/30/93
 *
 * Author:	Donald Becker (becker@super.org)
 */

#ifndef _LINUX_PORTIO_H
#define _LINUX_PORTIO_H

#define HAVE_PORTRESERVE
/*
 * Call check_region() before probing for your hardware.
 * Once you have found you hardware, register it with request_region().
 * If you unload the driver, use release_region to free ports.
 */
extern void reserve_setup(char *str, int *ints);
extern int check_region(unsigned long from, unsigned long extent);
extern void request_region(unsigned long from, unsigned long extent,const char *name);
extern void release_region(unsigned long from, unsigned long extent);
extern int get_ioport_list(char *);

#ifdef __sparc__
extern unsigned long occupy_region(unsigned long base, unsigned long end,
				   unsigned long num, unsigned int align,
				   const char *name);
#endif

#define HAVE_AUTOIRQ
extern void autoirq_setup(int waittime);
extern int autoirq_report(int waittime);

#endif	/* _LINUX_PORTIO_H */
