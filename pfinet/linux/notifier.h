/*
 *	Routines to manage notifier chains for passing status changes to any
 *	interested routines. We need this instead of hard coded call lists so
 *	that modules can poke their nose into the innards. The network devices
 *	needed them so here they are for the rest of you.
 *
 *				Alan Cox <Alan.Cox@linux.org>
 */
 
#ifndef _LINUX_NOTIFIER_H
#define _LINUX_NOTIFIER_H
#include <linux/errno.h>

struct notifier_block
{
	int (*notifier_call)(unsigned long, void *);
	struct notifier_block *next;
	int priority;
};


#ifdef __KERNEL__

#define NOTIFY_DONE		0x0000		/* Don't care */
#define NOTIFY_OK		0x0001		/* Suits me */
#define NOTIFY_STOP_MASK	0x8000		/* Don't call further */
#define NOTIFY_BAD		(NOTIFY_STOP_MASK|0x0002)	/* Bad/Veto action	*/

extern __inline__ int notifier_chain_register(struct notifier_block **list, struct notifier_block *n)
{
	while(*list)
	{
		if(n->priority > (*list)->priority)
			break;
		list= &((*list)->next);
	}
	n->next = *list;
	*list=n;
	return 0;
}

/*
 *	Warning to any non GPL module writers out there.. these functions are
 *	GPL'd
 */
 
extern __inline__ int notifier_chain_unregister(struct notifier_block **nl, struct notifier_block *n)
{
	while((*nl)!=NULL)
	{
		if((*nl)==n)
		{
			*nl=n->next;
			return 0;
		}
		nl=&((*nl)->next);
	}
	return -ENOENT;
}

/*
 *	This is one of these things that is generally shorter inline
 */
 
extern __inline__ int notifier_call_chain(struct notifier_block **n, unsigned long val, void *v)
{
	int ret=NOTIFY_DONE;
	struct notifier_block *nb = *n;
	while(nb)
	{
		ret=nb->notifier_call(val,v);
		if(ret&NOTIFY_STOP_MASK)
			return ret;
		nb=nb->next;
	}
	return ret;
}


/*
 *	Declared notifiers so far. I can imagine quite a few more chains
 *	over time (eg laptop power reset chains, reboot chain (to clean 
 *	device units up), device [un]mount chain, module load/unload chain,
 *	low memory chain, screenblank chain (for plug in modular screenblankers) 
 *	VC switch chains (for loadable kernel svgalib VC switch helpers) etc...
 */
 
/* netdevice notifier chain */
#define NETDEV_UP	0x0001	/* For now you can't veto a device up/down */
#define NETDEV_DOWN	0x0002
#define NETDEV_REBOOT	0x0003	/* Tell a protocol stack a network interface
				   detected a hardware crash and restarted
				   - we can use this eg to kick tcp sessions
				   once done */
#endif
#endif
