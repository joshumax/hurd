/*
 *		IP_MASQ_VDOLIVE  - VDO Live masquerading module
 *
 *
 * Version:	@(#)$Id: ip_masq_vdolive.c,v 1.4 1998/10/06 04:49:07 davem Exp $
 *
 * Author:	Nigel Metheringham <Nigel.Metheringham@ThePLAnet.net>
 *		PLAnet Online Ltd
 *
 * Fixes:	Minor changes for 2.1 by
 *		Steven Clarke <Steven.Clarke@ThePlanet.Net>, Planet Online Ltd
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Thanks:
 *	Thank you to VDOnet Corporation for allowing me access to
 *	a protocol description without an NDA.  This means that
 *	this module can be distributed as source - a great help!
 *	
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <asm/system.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/init.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/ip_masq.h>

struct vdolive_priv_data {
	/* Ports used */
	unsigned short	origport;
	unsigned short	masqport;
	/* State of decode */
	unsigned short	state;
};

/* 
 * List of ports (up to MAX_MASQ_APP_PORTS) to be handled by helper
 * First port is set to the default port.
 */
static int ports[MAX_MASQ_APP_PORTS] = {7000}; /* I rely on the trailing items being set to zero */
struct ip_masq_app *masq_incarnations[MAX_MASQ_APP_PORTS];

/*
 *     Debug level
 */
#ifdef CONFIG_IP_MASQ_DEBUG
static int debug=0;
MODULE_PARM(debug, "i");
#endif

MODULE_PARM(ports, "1-" __MODULE_STRING(MAX_MASQ_APP_PORTS) "i");

static int
masq_vdolive_init_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
	MOD_INC_USE_COUNT;
	if ((ms->app_data = kmalloc(sizeof(struct vdolive_priv_data),
				    GFP_ATOMIC)) == NULL) 
		IP_MASQ_DEBUG(1-debug, "VDOlive: No memory for application data\n");
	else 
	{
		struct vdolive_priv_data *priv = 
			(struct vdolive_priv_data *)ms->app_data;
		priv->origport = 0;
		priv->masqport = 0;
		priv->state = 0;
	}
        return 0;
}

static int
masq_vdolive_done_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_DEC_USE_COUNT;
	if (ms->app_data)
		kfree_s(ms->app_data, sizeof(struct vdolive_priv_data));
        return 0;
}

int
masq_vdolive_out (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, __u32 maddr)
{
        struct sk_buff *skb;
	struct iphdr *iph;
	struct tcphdr *th;
	char *data, *data_limit;
	unsigned int tagval;	/* This should be a 32 bit quantity */
	struct ip_masq *n_ms;
	struct vdolive_priv_data *priv = 
		(struct vdolive_priv_data *)ms->app_data;

	/* This doesn't work at all if no priv data was allocated on startup */
	if (!priv)
		return 0;

	/* Everything running correctly already */
	if (priv->state == 3)
		return 0;

        skb = *skb_p;
	iph = skb->nh.iph;
        th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
        data = (char *)&th[1];

        data_limit = skb->h.raw + skb->len;

	if (data+8 > data_limit) {
		IP_MASQ_DEBUG(1-debug, "VDOlive: packet too short for ID %p %p\n", data, data_limit);
		return 0;
	}
	memcpy(&tagval, data+4, 4);
	IP_MASQ_DEBUG(1-debug, "VDOlive: packet seen, tag %ld, in initial state %d\n", ntohl(tagval), priv->state);

	/* Check for leading packet ID */
	if ((ntohl(tagval) != 6) && (ntohl(tagval) != 1)) {
		IP_MASQ_DEBUG(1-debug, "VDOlive: unrecognised tag %ld, in initial state %d\n", ntohl(tagval), priv->state);
		return 0;
	}
		

	/* Check packet is long enough for data - ignore if not */
	if ((ntohl(tagval) == 6) && (data+36 > data_limit)) {
		IP_MASQ_DEBUG(1-debug, "VDOlive: initial packet too short %p %p\n", data, data_limit);
		return 0;
	} else if ((ntohl(tagval) == 1) && (data+20 > data_limit)) {
		IP_MASQ_DEBUG(1-debug,"VDOlive: secondary packet too short %p %p\n", data, data_limit);
		return 0;
	}

	/* Adjust data pointers */
	/*
	 * I could check the complete protocol version tag 
	 * in here however I am just going to look for the
	 * "VDO Live" tag in the hope that this part will
	 * remain constant even if the version changes
	 */
	if (ntohl(tagval) == 6) {
		data += 24;
		IP_MASQ_DEBUG(1-debug, "VDOlive: initial packet found\n");
	} else {
		data += 8;
		IP_MASQ_DEBUG(1-debug, "VDOlive: secondary packet found\n");
	}

	if (memcmp(data, "VDO Live", 8) != 0) {
		IP_MASQ_DEBUG(1-debug,"VDOlive: did not find tag\n");
		return 0;
	}
	/* 
	 * The port number is the next word after the tag.
	 * VDOlive encodes all of these values
	 * in 32 bit words, so in this case I am
	 * skipping the first 2 bytes of the next
	 * word to get to the relevant 16 bits
	 */
	data += 10;

	/*
	 * If we have not seen the port already,
	 * set the masquerading tunnel up
	 */
	if (!priv->origport) {
		memcpy(&priv->origport, data, 2);
		IP_MASQ_DEBUG(1-debug, "VDOlive: found port %d\n", ntohs(priv->origport));

		/* Open up a tunnel */
		n_ms = ip_masq_new(IPPROTO_UDP,
				   maddr, 0,
				   ms->saddr, priv->origport,
				   ms->daddr, 0,
				   IP_MASQ_F_NO_DPORT);
					
		if (n_ms==NULL) {
		        ip_masq_put(n_ms);
			IP_MASQ_DEBUG(1-debug, "VDOlive: unable to build UDP tunnel for %x:%x\n", ms->saddr, priv->origport);
			/* Leave state as unset */
			priv->origport = 0;
			return 0;
		}
		ip_masq_listen(n_ms);

		ip_masq_put(ms);
		priv->masqport = n_ms->mport;
	} else if (memcmp(data, &(priv->origport), 2)) {
		IP_MASQ_DEBUG(1-debug, "VDOlive: ports do not match\n");
		/* Write the port in anyhow!!! */
	}

	/*
	 * Write masq port into packet
	 */
	memcpy(data, &(priv->masqport), 2);
	IP_MASQ_DEBUG(1-debug, "VDOlive: rewrote port %d to %d, server %08X\n", ntohs(priv->origport), ntohs(priv->masqport), ms->saddr);

	/*
	 * Set state bit to make which bit has been done
	 */

	priv->state |= (ntohl(tagval) == 6) ? 1 : 2;

	return 0;
}


struct ip_masq_app ip_masq_vdolive = {
        NULL,			/* next */
	"VDOlive",	       	/* name */
        0,                      /* type */
        0,                      /* n_attach */
        masq_vdolive_init_1,	/* ip_masq_init_1 */
        masq_vdolive_done_1,	/* ip_masq_done_1 */
        masq_vdolive_out,	/* pkt_out */
        NULL                    /* pkt_in */
};

/*
 * 	ip_masq_vdolive initialization
 */

__initfunc(int ip_masq_vdolive_init(void))
{
	int i, j;

	for (i=0; (i<MAX_MASQ_APP_PORTS); i++) {
		if (ports[i]) {
			if ((masq_incarnations[i] = kmalloc(sizeof(struct ip_masq_app),
							    GFP_KERNEL)) == NULL)
				return -ENOMEM;
			memcpy(masq_incarnations[i], &ip_masq_vdolive, sizeof(struct ip_masq_app));
			if ((j = register_ip_masq_app(masq_incarnations[i], 
						      IPPROTO_TCP, 
						      ports[i]))) {
				return j;
			}
			IP_MASQ_DEBUG(1-debug, "RealAudio: loaded support on port[%d] = %d\n", i, ports[i]);
		} else {
			/* To be safe, force the incarnation table entry to NULL */
			masq_incarnations[i] = NULL;
		}
	}
	return 0;
}

/*
 * 	ip_masq_vdolive fin.
 */

int ip_masq_vdolive_done(void)
{
	int i, j, k;

	k=0;
	for (i=0; (i<MAX_MASQ_APP_PORTS); i++) {
		if (masq_incarnations[i]) {
			if ((j = unregister_ip_masq_app(masq_incarnations[i]))) {
				k = j;
			} else {
				kfree(masq_incarnations[i]);
				masq_incarnations[i] = NULL;
				IP_MASQ_DEBUG(1-debug,"VDOlive: unloaded support on port[%d] = %d\n", i, ports[i]);
			}
		}
	}
	return k;
}


#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
        if (ip_masq_vdolive_init() != 0)
                return -EIO;
        return 0;
}

void cleanup_module(void)
{
        if (ip_masq_vdolive_done() != 0)
                IP_MASQ_DEBUG(1-debug, "ip_masq_vdolive: can't remove module");
}

#endif /* MODULE */
