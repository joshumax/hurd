/*****************************************************************************/

/*
 *      hfmodem.h  --  Linux soundcard HF FSK driver.
 *
 *      Copyright (C) 1997  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *        Swiss Federal Institute of Technology (ETH), Electronics Lab
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  This is the Linux realtime sound output driver
 */

/*****************************************************************************/

#ifndef _HFMODEM_H
#define _HFMODEM_H
/* --------------------------------------------------------------------- */

#include <linux/version.h>

#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/fs.h>
#if LINUX_VERSION_CODE >= 0x20100
#include <linux/poll.h>
#endif

/* --------------------------------------------------------------------- */

#define HFMODEM_MINOR         145

#define HFMODEM_SRATE        8000
#define HFMODEM_MAXBITS      4800   /* required for GTOR 300 baud mode */
#define HFMODEM_MINBAUD        40
#define HFMODEM_MAXBAUD       400
#define HFMODEM_MAXCORRLEN   ((HFMODEM_SRATE+HFMODEM_MINBAUD-1)/HFMODEM_MINBAUD)

/* --------------------------------------------------------------------- */

typedef unsigned long hfmodem_time_t;
typedef int hfmodem_soft_t;
typedef unsigned long hfmodem_id_t;

/* --------------------------------------------------------------------- */

struct hfmodem_ioctl_fsk_tx_request {
	hfmodem_time_t tstart;
	hfmodem_time_t tinc;
	int inv;
	hfmodem_id_t id;
	unsigned int nbits;
	unsigned char *data;
	unsigned int freq[2];
};

struct hfmodem_ioctl_fsk_rx_request {
	hfmodem_time_t tstart;
	hfmodem_time_t tinc;
	unsigned int baud;
	hfmodem_id_t id;
	unsigned int nbits;
	hfmodem_soft_t *data;
	unsigned int freq[2];
};

struct hfmodem_ioctl_mixer_params {
	int src;
	int igain;
	int ogain;
};

struct hfmodem_ioctl_sample_params {
	__s16 *data;
	int len;
};

#define HFMODEM_IOCTL_FSKTXREQUEST    _IOW('H', 0, struct hfmodem_ioctl_fsk_tx_request)
#define HFMODEM_IOCTL_FSKRXREQUEST    _IOW('H', 1, struct hfmodem_ioctl_fsk_rx_request)
#define HFMODEM_IOCTL_CLEARRQ         _IO('H',  3)
#define HFMODEM_IOCTL_GETCURTIME      _IOR('H', 4, hfmodem_time_t)
#define HFMODEM_IOCTL_WAITRQ          _IOR('H', 5, hfmodem_id_t)
#define HFMODEM_IOCTL_MIXERPARAMS     _IOW('H', 6, struct hfmodem_ioctl_mixer_params)
#define HFMODEM_IOCTL_SAMPLESTART     _IOW('H', 7, struct hfmodem_ioctl_sample_params)
#define HFMODEM_IOCTL_SAMPLEFINISHED  _IO('H',  8)

/* --------------------------------------------------------------------- */
#ifdef __KERNEL__

#include <linux/parport.h>

#define DMA_MODE_AUTOINIT      0x10

#define NR_DEVICE 1

#define HFMODEM_FRAGSAMPLES (HFMODEM_SRATE/100)
#define HFMODEM_FRAGSIZE    (HFMODEM_FRAGSAMPLES*2)
#define HFMODEM_NUMFRAGS    8
#define HFMODEM_EXCESSFRAGS 3

#define HFMODEM_NUMRXSLOTS 20
#define HFMODEM_NUMTXSLOTS 4

#define HFMODEM_CORRELATOR_CACHE 8

enum slot_st { ss_unused = 0, ss_ready, ss_oper, ss_retired };
typedef int hfmodem_conv_t;

struct hfmodem_state {
	const struct hfmodem_scops *scops;

	/* io params */
	struct {
		unsigned int base_addr;
		unsigned int dma;
		unsigned int irq;
	} io;

	struct {
		unsigned int seriobase;
		unsigned int pariobase;
		unsigned int midiiobase;
		unsigned int flags;
		struct pardevice *pardev;
	} ptt_out;

	struct {
		__s16 *buf;
		unsigned int lastfrag;
		unsigned int fragptr;
		unsigned int last_dmaptr;
		int ptt_frames;
	} dma;

	struct {
		unsigned int last_tvusec;
		unsigned long long time_cnt;
		hfmodem_time_t lasttime;
#ifdef __i386__
		unsigned int starttime_lo, starttime_hi;
#endif /* __i386__ */
	} clk;

	int active;
	struct wait_queue *wait;

	struct {
		__s16 *kbuf;
		__s16 *ubuf;
		__s16 *kptr;
		unsigned int size;
		int rem;
	} sbuf;

	struct {
		hfmodem_time_t last_time; 
		unsigned int tx_phase;
		
		struct hfmodem_l1_rxslot {
			enum slot_st state;
			hfmodem_time_t tstart, tinc;
			hfmodem_soft_t *data;
			hfmodem_soft_t *userdata;
			unsigned int nbits;
			unsigned int cntbits;
			hfmodem_id_t id;
			unsigned int corrlen;
			hfmodem_conv_t scale;
			unsigned int corr_cache;
		} rxslots[HFMODEM_NUMRXSLOTS];
		
		struct hfmodem_l1_txslot {
			enum slot_st state;
			hfmodem_time_t tstart, tinc;
			unsigned char *data;
			unsigned int nbits;
			unsigned int cntbits;
			hfmodem_id_t id;
			unsigned char inv;
			unsigned int phinc;
			unsigned int phase_incs[2];
		} txslots[HFMODEM_NUMTXSLOTS];
	} l1;
};

struct hfmodem_correlator_cache {
	int refcnt;
	int lru;
	unsigned short phase_incs[2];
	hfmodem_conv_t correlator[2][2][HFMODEM_MAXCORRLEN];
};

struct hfmodem_scops {
	unsigned int extent;

	void (*init)(struct hfmodem_state *dev);
	void (*prepare_input)(struct hfmodem_state *dev);
	void (*trigger_input)(struct hfmodem_state *dev);
	void (*prepare_output)(struct hfmodem_state *dev);
	void (*trigger_output)(struct hfmodem_state *dev);
	void (*stop)(struct hfmodem_state *dev);
	unsigned int (*intack)(struct hfmodem_state *dev);
	void (*mixer)(struct hfmodem_state *dev, int src, int igain, int ogain);
};

/* --------------------------------------------------------------------- */

extern int hfmodem_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
#if LINUX_VERSION_CODE >= 0x20100
extern unsigned int hfmodem_poll(struct file *file, poll_table *wait);
#else
extern int hfmodem_select(struct inode *inode, struct file *file, int sel_type, select_table *wait);
#endif

extern void hfmodem_clear_rq(struct hfmodem_state *dev);
extern void hfmodem_input_samples(struct hfmodem_state *dev, hfmodem_time_t tstart, 
				hfmodem_time_t tinc, __s16 *samples);
extern int hfmodem_output_samples(struct hfmodem_state *dev, hfmodem_time_t tstart, 
				hfmodem_time_t tinc, __s16 *samples);
extern long hfmodem_next_tx_event(struct hfmodem_state *dev, hfmodem_time_t curr);
extern void hfmodem_finish_pending_rx_requests(struct hfmodem_state *dev);
extern void hfmodem_wakeup(struct hfmodem_state *dev);


extern int hfmodem_sbcprobe(struct hfmodem_state *dev);
extern int hfmodem_wssprobe(struct hfmodem_state *dev);

extern void hfmodem_refclock_probe(void);
extern void hfmodem_refclock_init(struct hfmodem_state *dev);
extern hfmodem_time_t hfmodem_refclock_current(struct hfmodem_state *dev, hfmodem_time_t expected, int exp_valid);

/* --------------------------------------------------------------------- */

extern const char hfmodem_drvname[];
extern const char hfmodem_drvinfo[];

extern struct hfmodem_state hfmodem_state[NR_DEVICE];
extern struct hfmodem_correlator_cache hfmodem_correlator_cache[HFMODEM_CORRELATOR_CACHE];

/* --------------------------------------------------------------------- */
#endif /* __KERNEL__ */
/* --------------------------------------------------------------------- */
#endif /* _HFMODEM_H */
