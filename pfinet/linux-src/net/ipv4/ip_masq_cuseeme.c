/*
 *		IP_MASQ_FTP CUSeeMe masquerading module
 *
 *
 * Version:	@(#)$Id: ip_masq_cuseeme.c,v 1.4 1998/10/06 04:48:57 davem Exp $
 *
 * Author:	Richard Lynch
 *		
 *
 * Fixes:
 *	Richard Lynch     	:	Updated patch to conform to new module
 *					specifications
 *	Nigel Metheringham	:	Multiple port support
 *	Michael Owings		:	Fixed broken init code
 *					Added code to update inbound
 *					packets with correct local addresses.
 *					Fixes audio and "chat" problems
 *					Thanx to the CU-SeeMe Consortium for
 *					technical docs
 *	Steven Clarke		:	Small changes for 2.1	
 *
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 * Multiple Port Support
 *	The helper can be made to handle up to MAX_MASQ_APP_PORTS (normally 12)
 *	with the port numbers being defined at module load time.  The module
 *	uses the symbol "ports" to define a list of monitored ports, which can
 *	be specified on the insmod command line as
 *		ports=x1,x2,x3...
 *	where x[n] are integer port numbers.  This option can be put into
 *	/etc/conf.modules (or /etc/modules.conf depending on your config)
 *	where modload will pick it up should you use modload to load your
 *	modules.
 *	
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/init.h>
#include <net/protocol.h>
#include <net/udp.h>

/* #define IP_MASQ_NDEBUG */
#include <net/ip_masq.h>

#pragma pack(1)
/* CU-SeeMe Data Header */
typedef struct {
	u_short 	dest_family;
	u_short 	dest_port;
	u_long  	dest_addr;
	short 		family;
	u_short 	port;
	u_long 		addr;
	u_long 		seq;
	u_short 	msg;
	u_short		data_type;
	u_short		packet_len;
} cu_header;

/* Open Continue Header */
typedef struct	{
	cu_header	cu_head;
	u_short 	client_count; /* Number of client info structs */
	u_long		seq_no;
	char		user_name[20];
	char		stuff[4]; /* flags,  version stuff,  etc */
}oc_header;

/* client info structures */
typedef struct {
	u_long		address; /* Client address */
	char	       	stuff[8]; /* Flags, pruning bitfield,  packet counts etc */
} client_info;
#pragma pack()

/*
 * List of ports (up to MAX_MASQ_APP_PORTS) to be handled by helper
 * First port is set to the default port.
 */
static int ports[MAX_MASQ_APP_PORTS] = {7648}; /* I rely on the trailing items being set to zero */
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
masq_cuseeme_init_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_INC_USE_COUNT;
        return 0;
}

static int
masq_cuseeme_done_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_DEC_USE_COUNT;
        return 0;
}

int
masq_cuseeme_out (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, __u32 maddr)
{
	struct sk_buff *skb = *skb_p;
	struct iphdr *iph = skb->nh.iph;
	struct udphdr *uh = (struct udphdr *)&(((char *)iph)[iph->ihl*4]);
	cu_header *cu_head;
	char *data=(char *)&uh[1];

	if (skb->len - ((unsigned char *) data - skb->h.raw) >= sizeof(cu_header))
	{
		cu_head         = (cu_header *) data;
		/* cu_head->port   = ms->mport; */
	        if( cu_head->addr )
		cu_head->addr = (u_long) maddr;
	        if(ntohs(cu_head->data_type) == 257)
		        IP_MASQ_DEBUG(1-debug, "Sending talk packet!\n");
	}
	return 0;
}

int
masq_cuseeme_in (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, __u32 maddr)
{
	struct sk_buff *skb = *skb_p;
	struct iphdr *iph = skb->nh.iph;
	struct udphdr *uh = (struct udphdr *)&(((char *)iph)[iph->ihl*4]);
	cu_header *cu_head;
	oc_header	*oc;
	client_info	*ci;
	char *data=(char *)&uh[1];
	u_short len = skb->len - ((unsigned char *) data - skb->h.raw);
	int		i, off;

	if (len >= sizeof(cu_header))
	{
		cu_head         = (cu_header *) data;
		if(cu_head->dest_addr) /* Correct destination address */
			cu_head->dest_addr = (u_long) ms->saddr;
		if(ntohs(cu_head->data_type)==101 && len > sizeof(oc_header))
		{
			oc = (oc_header * ) data;
			/* Spin (grovel) thru client_info structs till we find our own */
		        off=sizeof(oc_header);
			for(i=0;
			    (i < oc->client_count && off+sizeof(client_info) <= len);
			    i++)		    
			{
			        ci=(client_info *)(data+off);
				if(ci->address==(u_long) maddr)
				{
				        /* Update w/ our real ip address and exit */
					ci->address = (u_long) ms->saddr;
					break;
				}
			        else
				   off+=sizeof(client_info);
			}
		}
	}
	return 0;
}

struct ip_masq_app ip_masq_cuseeme = {
        NULL,			/* next */
        "cuseeme",
        0,                      /* type */
        0,                      /* n_attach */
        masq_cuseeme_init_1,	/* ip_masq_init_1 */
        masq_cuseeme_done_1,	/* ip_masq_done_1 */
        masq_cuseeme_out,	/* pkt_out */
        masq_cuseeme_in    	/* pkt_in */
};


/*
 * 	ip_masq_cuseeme initialization
 */

__initfunc(int ip_masq_cuseeme_init(void))
{
	int i, j;

	for (i=0; (i<MAX_MASQ_APP_PORTS); i++) {
		if (ports[i]) {
			if ((masq_incarnations[i] = kmalloc(sizeof(struct ip_masq_app),
							    GFP_KERNEL)) == NULL)
				return -ENOMEM;
			memcpy(masq_incarnations[i], &ip_masq_cuseeme, sizeof(struct ip_masq_app));
			if ((j = register_ip_masq_app(masq_incarnations[i], 
						      IPPROTO_UDP,
						      ports[i]))) {
				return j;
			}
#if DEBUG_CONFIG_IP_MASQ_CUSEEME
			IP_MASQ_DEBUG(1-debug, "CuSeeMe: loaded support on port[%d] = %d\n",
			       i, ports[i]);
#endif
		} else {
			/* To be safe, force the incarnation table entry to NULL */
			masq_incarnations[i] = NULL;
		}
	}
	return 0;
}

/*
 * 	ip_masq_cuseeme fin.
 */

int ip_masq_cuseeme_done(void)
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
				IP_MASQ_DEBUG(1-debug, "CuSeeMe: unloaded support on port[%d] = %d\n", i, ports[i]);
			}
		}
	}
	return k;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
        if (ip_masq_cuseeme_init() != 0)
                return -EIO;
        return 0;
}

void cleanup_module(void)
{
        if (ip_masq_cuseeme_done() != 0)
                IP_MASQ_DEBUG(1-debug, "ip_masq_cuseeme: can't remove module");
}

#endif /* MODULE */
