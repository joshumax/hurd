/*
 *		IP_MASQ_FTP ftp masquerading module
 *
 *
 * Version:	@(#)ip_masq_ftp.c 0.04   02/05/96
 *
 * Author:	Wouter Gadeyne
 *		
 *
 * Fixes:
 *	Wouter Gadeyne		:	Fixed masquerading support of ftp PORT commands
 * 	Juan Jose Ciarlante	:	Code moved and adapted from ip_fw.c
 * 	Keith Owens		:	Add keep alive for ftp control channel
 *	Nigel Metheringham	:	Added multiple port support
 * 	Juan Jose Ciarlante	:	Use control_add() for ftp control chan
 * 	Juan Jose Ciarlante	:	Litl bits for 2.1
 *	Juan Jose Ciarlante	:	use ip_masq_listen() 
 *	Juan Jose Ciarlante	: 	use private app_data for own flag(s)
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
#include <net/tcp.h>

/* #define IP_MASQ_NDEBUG */
#include <net/ip_masq.h>


/* 
 * List of ports (up to MAX_MASQ_APP_PORTS) to be handled by helper
 * First port is set to the default port.
 */
static int ports[MAX_MASQ_APP_PORTS] = {21}; /* I rely on the trailing items being set to zero */
struct ip_masq_app *masq_incarnations[MAX_MASQ_APP_PORTS];

/*
 *	Debug level
 */
#ifdef CONFIG_IP_MASQ_DEBUG
static int debug=0;
MODULE_PARM(debug, "i");
#endif

MODULE_PARM(ports, "1-" __MODULE_STRING(MAX_MASQ_APP_PORTS) "i");

/*	Dummy variable */
static int masq_ftp_pasv;

static int
masq_ftp_init_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_INC_USE_COUNT;
        return 0;
}

static int
masq_ftp_done_1 (struct ip_masq_app *mapp, struct ip_masq *ms)
{
        MOD_DEC_USE_COUNT;
        return 0;
}

int
masq_ftp_out (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, __u32 maddr)
{
        struct sk_buff *skb;
	struct iphdr *iph;
	struct tcphdr *th;
	char *p, *data, *data_limit;
	unsigned char p1,p2,p3,p4,p5,p6;
	__u32 from;
	__u16 port;
	struct ip_masq *n_ms;
	char buf[24];		/* xxx.xxx.xxx.xxx,ppp,ppp\000 */
        unsigned buf_len;
	int diff;

        skb = *skb_p;
	iph = skb->nh.iph;
        th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
        data = (char *)&th[1];

        data_limit = skb->h.raw + skb->len - 18;
        if (skb->len >= 6 && (memcmp(data, "PASV\r\n", 6) == 0 || memcmp(data, "pasv\r\n", 6) == 0))
		ms->app_data = &masq_ftp_pasv;

	while (data < data_limit)
	{
		if (memcmp(data,"PORT ",5) && memcmp(data,"port ",5))
		{
			data ++;
			continue;
		}
		p = data+5;
 		p1 = simple_strtoul(data+5,&data,10);
		if (*data!=',')
			continue;
		p2 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p3 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p4 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p5 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p6 = simple_strtoul(data+1,&data,10);
		if (*data!='\r' && *data!='\n')
			continue;

		from = (p1<<24) | (p2<<16) | (p3<<8) | p4;
		port = (p5<<8) | p6;

		IP_MASQ_DEBUG(1-debug, "PORT %X:%X detected\n",from,port);

		/*
		 * Now update or create an masquerade entry for it
		 */

		IP_MASQ_DEBUG(1-debug, "protocol %d %lX:%X %X:%X\n", iph->protocol, htonl(from), htons(port), iph->daddr, 0);

		n_ms = ip_masq_out_get(iph->protocol,
					 htonl(from), htons(port),
					 iph->daddr, 0);
		if (!n_ms) {
			n_ms = ip_masq_new(IPPROTO_TCP,
					   maddr, 0,
					   htonl(from), htons(port),
					   iph->daddr, 0,
					   IP_MASQ_F_NO_DPORT);

			if (n_ms==NULL)
				return 0;
			ip_masq_control_add(n_ms, ms);
		}

		/*
		 * Replace the old PORT with the new one
		 */
		from = ntohl(n_ms->maddr);
		port = ntohs(n_ms->mport);
		sprintf(buf,"%d,%d,%d,%d,%d,%d",
			from>>24&255,from>>16&255,from>>8&255,from&255,
			port>>8&255,port&255);
		buf_len = strlen(buf);

		IP_MASQ_DEBUG(1-debug, "new PORT %X:%X\n",from,port);

		/*
		 * Calculate required delta-offset to keep TCP happy
		 */
		
		diff = buf_len - (data-p);
		
		/*
		 *	No shift.
		 */
		
		if (diff==0) {
			/*
			 * simple case, just replace the old PORT cmd
 			 */
 			memcpy(p,buf,buf_len);
 		} else {

			*skb_p = ip_masq_skb_replace(skb, GFP_ATOMIC, p, data-p, buf, buf_len);
		}
                /*
                 * 	Move tunnel to listen state
                 */
		ip_masq_listen(n_ms);
		ip_masq_put(n_ms);

                return diff;

	}
	return 0;

}

/*
 * Look at incoming ftp packets to catch the response to a PASV command.  When
 * we see one we build a masquerading entry for the client address, client port
 * 0 (unknown at the moment), the server address and the server port.  Mark the
 * current masquerade entry as a control channel and point the new entry at the
 * control entry.  All this work just for ftp keepalive across masquerading.
 *
 * The incoming packet should be something like
 * "227 Entering Passive Mode (xxx,xxx,xxx,xxx,ppp,ppp)".
 * xxx,xxx,xxx,xxx is the server address, ppp,ppp is the server port number.
 * ncftp 2.3.0 cheats by skipping the leading number then going 22 bytes into
 * the data so we do the same.  If it's good enough for ncftp then it's good
 * enough for me.
 *
 * In this case, the client is the source machine being masqueraded, the server
 * is the destination for ftp requests.  It all depends on your point of view ...
 */

int
masq_ftp_in (struct ip_masq_app *mapp, struct ip_masq *ms, struct sk_buff **skb_p, __u32 maddr)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct tcphdr *th;
	char *data, *data_limit;
	unsigned char p1,p2,p3,p4,p5,p6;
	__u32 to;
	__u16 port;
	struct ip_masq *n_ms;

	if (ms->app_data != &masq_ftp_pasv)
		return 0;	/* quick exit if no outstanding PASV */

	skb = *skb_p;
	iph = skb->nh.iph;
	th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
	data = (char *)&th[1];
	data_limit = skb->h.raw + skb->len;

	while (data < data_limit && *data != ' ')
		++data;	
	while (data < data_limit && *data == ' ')
		++data;	
	data += 22;
	if (data >= data_limit || *data != '(')
		return 0;
	p1 = simple_strtoul(data+1, &data, 10);
	if (data >= data_limit || *data != ',')
		return 0;
	p2 = simple_strtoul(data+1, &data, 10);
	if (data >= data_limit || *data != ',')
		return 0;
	p3 = simple_strtoul(data+1, &data, 10);
	if (data >= data_limit || *data != ',')
		return 0;
	p4 = simple_strtoul(data+1, &data, 10);
	if (data >= data_limit || *data != ',')
		return 0;
	p5 = simple_strtoul(data+1, &data, 10);
	if (data >= data_limit || *data != ',')
		return 0;
	p6 = simple_strtoul(data+1, &data, 10);
	if (data >= data_limit || *data != ')')
		return 0;

	to = (p1<<24) | (p2<<16) | (p3<<8) | p4;
	port = (p5<<8) | p6;

	/*
	 * Now update or create an masquerade entry for it
	 */
	IP_MASQ_DEBUG(1-debug, "PASV response %lX:%X %X:%X detected\n", ntohl(ms->saddr), 0, to, port);

	n_ms = ip_masq_out_get(iph->protocol,
				 ms->saddr, 0,
				 htonl(to), htons(port));
	if (!n_ms) {
		n_ms = ip_masq_new(IPPROTO_TCP,
					maddr, 0,
					ms->saddr, 0,
					htonl(to), htons(port),
					IP_MASQ_F_NO_SPORT);

		if (n_ms==NULL)
			return 0;
		ip_masq_control_add(n_ms, ms);
	}

#if 0	/* v0.12 state processing */

	/*
	 * keep for a bit longer than tcp_fin, client may not issue open
	 * to server port before tcp_fin_timeout.
	 */
	n_ms->timeout = ip_masq_expire->tcp_fin_timeout*3;
#endif
	ms->app_data = NULL;
	ip_masq_put(n_ms);

	return 0;	/* no diff required for incoming packets, thank goodness */
}

struct ip_masq_app ip_masq_ftp = {
        NULL,			/* next */
	"ftp",			/* name */
        0,                      /* type */
        0,                      /* n_attach */
        masq_ftp_init_1,        /* ip_masq_init_1 */
        masq_ftp_done_1,        /* ip_masq_done_1 */
        masq_ftp_out,           /* pkt_out */
        masq_ftp_in,            /* pkt_in */
};

/*
 * 	ip_masq_ftp initialization
 */

__initfunc(int ip_masq_ftp_init(void))
{
	int i, j;

	for (i=0; (i<MAX_MASQ_APP_PORTS); i++) {
		if (ports[i]) {
			if ((masq_incarnations[i] = kmalloc(sizeof(struct ip_masq_app),
							    GFP_KERNEL)) == NULL)
				return -ENOMEM;
			memcpy(masq_incarnations[i], &ip_masq_ftp, sizeof(struct ip_masq_app));
			if ((j = register_ip_masq_app(masq_incarnations[i], 
						      IPPROTO_TCP, 
						      ports[i]))) {
				return j;
			}
			IP_MASQ_DEBUG(1-debug, "Ftp: loaded support on port[%d] = %d\n",
			       i, ports[i]);
		} else {
			/* To be safe, force the incarnation table entry to NULL */
			masq_incarnations[i] = NULL;
		}
	}
	return 0;
}

/*
 * 	ip_masq_ftp fin.
 */

int ip_masq_ftp_done(void)
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
				IP_MASQ_DEBUG(1-debug, "Ftp: unloaded support on port[%d] = %d\n",
				       i, ports[i]);
			}
		}
	}
	return k;
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void)
{
        if (ip_masq_ftp_init() != 0)
                return -EIO;
        return 0;
}

void cleanup_module(void)
{
        if (ip_masq_ftp_done() != 0)
                printk(KERN_INFO "ip_masq_ftp: can't remove module");
}

#endif /* MODULE */
