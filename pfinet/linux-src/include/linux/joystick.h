#ifndef _LINUX_JOYSTICK_H
#define _LINUX_JOYSTICK_H

/*
 * /usr/include/linux/joystick.h  Version 1.2
 *
 * Copyright (C) 1996-1999 Vojtech Pavlik
 *
 * Sponsored by SuSE
 */

/*
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * 
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@suse.cz>, or by paper mail:
 * Vojtech Pavlik, Ucitelska 1576, Prague 8, 182 00 Czech Republic
 */

#include <asm/types.h>
#include <linux/module.h>

/*
 * Version
 */

#define JS_VERSION		0x01020f

/*
 * Types and constants for reading from /dev/js
 */

#define JS_EVENT_BUTTON		0x01	/* button pressed/released */
#define JS_EVENT_AXIS		0x02	/* joystick moved */
#define JS_EVENT_INIT		0x80	/* initial state of device */

struct js_event {
	__u32 time;	/* event timestamp in miliseconds */
	__s16 value;	/* value */
	__u8 type;	/* event type */
	__u8 number;	/* axis/button number */
};

/*
 * IOCTL commands for joystick driver
 */

#define JSIOCGVERSION		_IOR('j', 0x01, __u32)			/* get driver version */

#define JSIOCGAXES		_IOR('j', 0x11, __u8)			/* get number of axes */
#define JSIOCGBUTTONS		_IOR('j', 0x12, __u8)			/* get number of buttons */
#define JSIOCGNAME(len)		_IOC(_IOC_READ, 'j', 0x13, len)         /* get identifier string */

#define JSIOCSCORR		_IOW('j', 0x21, struct js_corr)		/* set correction values */
#define JSIOCGCORR		_IOR('j', 0x22, struct js_corr)		/* get correction values */

/*
 * Types and constants for get/set correction
 */

#define JS_CORR_NONE		0x00	/* returns raw values */
#define JS_CORR_BROKEN		0x01	/* broken line */

struct js_corr {
	__s32 coef[8];
	__s16 prec;
	__u16 type;
};

/*
 * v0.x compatibility definitions
 */

#define JS_RETURN		sizeof(struct JS_DATA_TYPE)
#define JS_TRUE			1
#define JS_FALSE		0
#define JS_X_0			0x01
#define JS_Y_0			0x02
#define JS_X_1			0x04
#define JS_Y_1			0x08
#define JS_MAX			2

#define JS_DEF_TIMEOUT		0x1300
#define JS_DEF_CORR		0
#define JS_DEF_TIMELIMIT	10L

#define JS_SET_CAL		1
#define JS_GET_CAL		2
#define JS_SET_TIMEOUT		3
#define JS_GET_TIMEOUT		4
#define JS_SET_TIMELIMIT	5
#define JS_GET_TIMELIMIT	6
#define JS_GET_ALL		7
#define JS_SET_ALL		8

struct JS_DATA_TYPE {
	int buttons;
	int x;
	int y;
};

struct JS_DATA_SAVE_TYPE {
	int JS_TIMEOUT;
	int BUSY;
	long JS_EXPIRETIME;
	long JS_TIMELIMIT;
	struct JS_DATA_TYPE JS_SAVE;
	struct JS_DATA_TYPE JS_CORR;
};

/*
 * Internal definitions
 */

#ifdef __KERNEL__

#define JS_BUFF_SIZE		64		/* output buffer size */

#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,2,0)
#error "You need to use at least v2.2 Linux kernel."
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
#include <asm/spinlock.h>
typedef struct wait_queue *wait_queue_head_t;
#define __setup(a,b)
#define BASE_ADDRESS(x,i)	((x)->base_address[i])
#define DECLARE_WAITQUEUE(x,y)	struct wait_queue x = { y, NULL }
#define init_waitqueue_head(x)	do { *(x) = NULL; } while (0)
#define __set_current_state(x)	current->state = x
#define SETUP_PARAM		char *str, int *ints
#define SETUP_PARSE(x)		do {} while (0)
#else
#include <linux/spinlock.h>
#define BASE_ADDRESS(x,i)	((x)->resource[i].start)
#define SETUP_PARAM		char *str
#define SETUP_PARSE(x)		int ints[x]; get_options(str, x, ints)
#endif

#define PCI_VENDOR_ID_AUREAL	0x12eb

/*
 * Parport stuff
 */

#include <linux/parport.h>

#define JS_PAR_STATUS_INVERT	(0x80)
#define JS_PAR_CTRL_INVERT	(0x04)
#define JS_PAR_DATA_IN(y)	parport_read_data(y->port)
#define JS_PAR_DATA_OUT(x,y)	parport_write_data(y->port, x)
#define JS_PAR_STATUS(y)	parport_read_status(y->port)

#ifndef PARPORT_NEED_GENERIC_OPS
#define JS_PAR_CTRL_IN(y)	parport_read_control(y->port)
#else
#define JS_PAR_CTRL_IN(y)	inb(y->port->base+2) 
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
#define JS_PAR_CTRL_OUT(x,y)	parport_write_control(y->port, x)
#define JS_PAR_ECTRL_OUT(x,y)	parport_write_econtrol(y->port, x)
#else
#define JS_PAR_CTRL_OUT(x,y)					\
	do {							\
		if ((x) & 0x20) parport_data_reverse(y->port);	\
		else parport_data_forward(y->port);		\
		parport_write_control(y->port, (x) & ~0x20);	\
	} while (0)
#define JS_PAR_ECTRL_OUT(x,y)	/*parport sets PS/2 mode on ECR chips */
#define PARPORT_MODE_PCPS2	PARPORT_MODE_TRISTATE
#define PARPORT_MODE_PCECPPS2	PARPORT_MODE_TRISTATE
#endif

/*
 * Internal types
 */

struct js_dev;

typedef int (*js_read_func)(void *info, int **axes, int **buttons);
typedef int (*js_ops_func)(struct js_dev *dev);

struct js_data {
	int *axes;
	int *buttons;
};

struct js_dev {
	struct js_dev *next;
	struct js_list *list;
	struct js_port *port;
	wait_queue_head_t wait;
	struct js_data cur;
	struct js_data new;
	struct js_corr *corr;
	struct js_event buff[JS_BUFF_SIZE];
	js_ops_func open;
	js_ops_func close;
	int ahead;
	int bhead;
	int tail;
	int num_axes;
	int num_buttons;
	char *name;
};

struct js_list {
	struct js_list *next;
	struct js_dev *dev;
	int tail;
	int startup;
};

struct js_port {
	struct js_port *next;
	struct js_port *prev;
	js_read_func read;
	struct js_dev **devs;
	int **axes;
	int **buttons;
	struct js_corr **corr;
	void *info;
	int ndevs;
	int fail;
	int total;
};

/*
 * Sub-module interface
 */

extern struct js_port *js_register_port(struct js_port *port, void *info,
	int devs, int infos, js_read_func read);
extern struct js_port *js_unregister_port(struct js_port *port);

extern int js_register_device(struct js_port *port, int number, int axes,
	int buttons, char *name, js_ops_func open, js_ops_func close);
extern void js_unregister_device(struct js_dev *dev);

/*
 * Kernel interface
 */

extern int js_init(void);
extern int js_am_init(void);
extern int js_an_init(void);
extern int js_as_init(void);
extern int js_console_init(void);
extern int js_cr_init(void);
extern int js_db9_init(void);
extern int js_gr_init(void);
extern int js_l4_init(void);
extern int js_lt_init(void);
extern int js_mag_init(void);
extern int js_pci_init(void);
extern int js_sw_init(void);
extern int js_sball_init(void);
extern int js_orb_init(void);
extern int js_tm_init(void);
extern int js_tg_init(void);
extern int js_war_init(void);

#endif /* __KERNEL__ */

#endif /* _LINUX_JOYSTICK_H */
