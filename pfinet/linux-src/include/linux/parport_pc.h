#ifndef __LINUX_PARPORT_PC_H
#define __LINUX_PARPORT_PC_H

#include <asm/io.h>

/* --- register definitions ------------------------------- */

#define ECONTROL 0x402
#define CONFIGB  0x401
#define CONFIGA  0x400
#define EPPDATA  0x4
#define EPPADDR  0x3
#define CONTROL  0x2
#define STATUS   0x1
#define DATA     0

/* Private data for PC low-level driver. */
struct parport_pc_private {
	/* Contents of CTR. */
	unsigned char ctr;
};

extern int parport_pc_epp_clear_timeout(struct parport *pb);

extern volatile unsigned char parport_pc_ctr;

extern __inline__ void parport_pc_write_epp(struct parport *p, unsigned char d)
{
	outb(d, p->base+EPPDATA);
}

extern __inline__ unsigned char parport_pc_read_epp(struct parport *p)
{
	return inb(p->base+EPPDATA);
}

extern __inline__ void parport_pc_write_epp_addr(struct parport *p, unsigned char d)
{
	outb(d, p->base+EPPADDR);
}

extern __inline__ unsigned char parport_pc_read_epp_addr(struct parport *p)
{
	return inb(p->base+EPPADDR);
}

extern __inline__ int parport_pc_check_epp_timeout(struct parport *p)
{
	if (!(inb(p->base+STATUS) & 1))
		return 0;
	parport_pc_epp_clear_timeout(p);
	return 1;
}

extern __inline__ unsigned char parport_pc_read_configb(struct parport *p)
{
	return inb(p->base+CONFIGB);
}

extern __inline__ void parport_pc_write_data(struct parport *p, unsigned char d)
{
	outb(d, p->base+DATA);
}

extern __inline__ unsigned char parport_pc_read_data(struct parport *p)
{
	return inb(p->base+DATA);
}

extern __inline__ void parport_pc_write_control(struct parport *p, unsigned char d)
{
	struct parport_pc_private *priv = p->private_data;
	priv->ctr = d;/* update soft copy */
	outb(d, p->base+CONTROL);
}

extern __inline__ unsigned char parport_pc_read_control(struct parport *p)
{
	struct parport_pc_private *priv = p->private_data;
	return priv->ctr;
}

extern __inline__ unsigned char parport_pc_frob_control(struct parport *p, unsigned char mask,  unsigned char val)
{
	struct parport_pc_private *priv = p->private_data;
	unsigned char ctr = priv->ctr;
	ctr = (ctr & ~mask) ^ val;
	outb (ctr, p->base+CONTROL);
	return priv->ctr = ctr; /* update soft copy */
}

extern __inline__ void parport_pc_write_status(struct parport *p, unsigned char d)
{
	outb(d, p->base+STATUS);
}

extern __inline__ unsigned char parport_pc_read_status(struct parport *p)
{
	return inb(p->base+STATUS);
}

extern __inline__ void parport_pc_write_econtrol(struct parport *p, unsigned char d)
{
	outb(d, p->base+ECONTROL);
}

extern __inline__ unsigned char parport_pc_read_econtrol(struct parport *p)
{
	return inb(p->base+ECONTROL);
}

extern __inline__ unsigned char parport_pc_frob_econtrol(struct parport *p, unsigned char mask,  unsigned char val)
{
	unsigned char old = inb(p->base+ECONTROL);
	outb(((old & ~mask) ^ val), p->base+ECONTROL);
	return old;
}

extern void parport_pc_change_mode(struct parport *p, int m);

extern void parport_pc_write_fifo(struct parport *p, unsigned char v);

extern unsigned char parport_pc_read_fifo(struct parport *p);

extern void parport_pc_disable_irq(struct parport *p);

extern void parport_pc_enable_irq(struct parport *p);

extern void parport_pc_release_resources(struct parport *p);

extern int parport_pc_claim_resources(struct parport *p);

extern void parport_pc_init_state(struct parport_state *s);

extern void parport_pc_save_state(struct parport *p, struct parport_state *s);

extern void parport_pc_restore_state(struct parport *p, struct parport_state *s);

extern size_t parport_pc_epp_read_block(struct parport *p, void *buf, size_t length);

extern size_t parport_pc_epp_write_block(struct parport *p, void *buf, size_t length);

extern int parport_pc_ecp_read_block(struct parport *p, void *buf, size_t length, void (*fn)(struct parport *, void *, size_t), void *handle);

extern int parport_pc_ecp_write_block(struct parport *p, void *buf, size_t length, void (*fn)(struct parport *, void *, size_t), void *handle);

extern void parport_pc_inc_use_count(void);

extern void parport_pc_dec_use_count(void);

#endif
