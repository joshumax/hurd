#ifndef _LINUX_LP_H
#define _LINUX_LP_H

/*
 * split in two parts by Joerg Dorchain
 * usr/include/linux/lp.h  modified for Amiga by Michael Rausch
 * modified for Atari by Andreas Schwab
 * bug fixed by Jes Sorensen 18/8-94:
 *     It was not possible to compile the kernel only for Atari or Amiga.
 *
 * linux i386 version  c.1991-1992 James Wiegand
 * many modifications copyright (C) 1992 Michael K. Johnson
 * Interrupt support added 1993 Nigel Gamble
 */

/*
 *  many many printers are we going to support? currently, this is the
 *  hardcoded limit
 */
#define MAX_LP 5

/*
 * Per POSIX guidelines, this module reserves the LP and lp prefixes
 * These are the lp_table[minor].flags flags...
 */
#define LP_EXIST 0x0001
#define LP_BUSY	 0x0004
#define LP_ABORT 0x0040
#define LP_CAREFUL 0x0080
#define LP_ABORTOPEN 0x0100

/* timeout for each character.  This is relative to bus cycles -- it
 * is the count in a busy loop.  THIS IS THE VALUE TO CHANGE if you
 * have extremely slow printing, or if the machine seems to slow down
 * a lot when you print.  If you have slow printing, increase this
 * number and recompile, and if your system gets bogged down, decrease
 * this number.  This can be changed with the tunelp(8) command as well.
 */

#define LP_INIT_CHAR 1000

/* The parallel port specs apparently say that there needs to be
 * a .5usec wait before and after the strobe.  Since there are wildly
 * different computers running linux, I can't come up with a perfect
 * value, but since it worked well on most printers before without,
 * I'll initialize it to 0.
 */

#define LP_INIT_WAIT 0

/* This is the amount of time that the driver waits for the printer to
 * catch up when the printer's buffer appears to be filled.  If you
 * want to tune this and have a fast printer (i.e. HPIIIP), decrease
 * this number, and if you have a slow printer, increase this number.
 * This is in hundredths of a second, the default 2 being .05 second.
 * Or use the tunelp(8) command, which is especially nice if you want
 * change back and forth between character and graphics printing, which
 * are wildly different...
 */

#define LP_INIT_TIME 40

/* IOCTL numbers */
#define LPCHAR   0x0601  /* corresponds to LP_INIT_CHAR */
#define LPTIME   0x0602  /* corresponds to LP_INIT_TIME */
#define LPABORT  0x0604  /* call with TRUE arg to abort on error,
			    FALSE to retry.  Default is retry.  */
#define LPSETIRQ 0x0605  /* call with new IRQ number,
			    or 0 for polling (no IRQ) */
#define LPGETIRQ 0x0606  /* get the current IRQ number */
#define LPWAIT   0x0608  /* corresponds to LP_INIT_WAIT */
#define LPCAREFUL   0x0609  /* call with TRUE arg to require out-of-paper, off-
			    line, and error indicators good on all writes,
			    FALSE to ignore them.  Default is ignore. */
#define LPABORTOPEN 0x060a  /* call with TRUE arg to abort open() on error,
			    FALSE to ignore error.  Default is ignore.  */
#define LPGETSTATUS 0x060b  /* return LP_S(minor) */
#define LPRESET     0x060c  /* reset printer */

/* timeout for printk'ing a timeout, in jiffies (100ths of a second).
   This is also used for re-checking error conditions if LP_ABORT is
   not set.  This is the default behavior. */

#define LP_TIMEOUT_INTERRUPT	(60 * HZ)
#define LP_TIMEOUT_POLLED	(10 * HZ)


#define LP_BUFFER_SIZE 1024 /*256*/

enum lp_type  {
LP_UNKNOWN = 0,
LP_AMIGA = 1,
LP_ATARI = 2,
LP_MFC = 3,
LP_IOEXT = 4,
LP_MVME167 = 5,
LP_BVME6000 = 6
};

/*
 * warning: this structure is in kernel space and has to fit in one page,
 * i.e. must not be larger than 4k
 */
struct lp_struct {
	char *name;
	unsigned int irq;
	void (*lp_out)(int,int);	/*output char function*/
	int (*lp_is_busy)(int);
	int (*lp_has_pout)(int);
	int (*lp_is_online)(int);
	int (*lp_dummy)(int);
	int (*lp_ioctl)(int, unsigned int, unsigned long);
	int (*lp_open)(int);		/* for module use counter */
	void (*lp_release)(int);	/* for module use counter */
	int flags;		/*for BUSY... */
	unsigned int chars;	/*busy timeout */
	unsigned int time;	/*wait time */
	unsigned int wait;
	struct wait_queue *lp_wait_q; /*strobe wait */
	void *base;			/* hardware drivers internal use*/
	enum lp_type type;
	char lp_buffer[LP_BUFFER_SIZE];
	int do_print;
	unsigned long copy_size,bytes_written;
};

extern struct lp_struct *lp_table[MAX_LP];
extern unsigned int lp_irq;

void lp_interrupt(int dev);
int lp_m68k_init(void);
int register_parallel(struct lp_struct *, int);
void unregister_parallel(int);

#endif
