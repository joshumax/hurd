/*
 *	Generic loadable firewalls. At the moment only IP will actually
 *	use these, but people can add the others as they are needed.
 *
 *	Authors:	Dave Bonn (for IP)
 *	much hacked by:	Alan Cox
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/firewall.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <asm/semaphore.h>

struct semaphore firewall_sem = MUTEX; 
static int firewall_policy[NPROTO];
static struct firewall_ops *firewall_chain[NPROTO];

/*
 *	Register a firewall
 */

int register_firewall(int pf, struct firewall_ops *fw)
{
	struct firewall_ops **p;

	if(pf<0||pf>=NPROTO)
		return -EINVAL;

	/*
	 *	Don't allow two people to adjust at once.
	 */

	down(&firewall_sem); 

	p=&firewall_chain[pf];

	while(*p)
	{
		if(fw->fw_priority > (*p)->fw_priority)
			break;
		p=&((*p)->next);
	}

	/*
	 * We need to use a memory barrier to make sure that this
	 * works correctly even in SMP with weakly ordered writes.
	 *
	 * This is atomic wrt interrupts (and generally walking the
	 * chain), but not wrt itself (so you can't call this from
	 * an interrupt. Not that you'd want to).
	 */

	fw->next=*p;
	mb();
	*p = fw;

	/*
	 *	And release the sleep lock
	 */

 	up(&firewall_sem); 
	return 0;
}

/*
 *	Unregister a firewall
 */

int unregister_firewall(int pf, struct firewall_ops *fw)
{
	struct firewall_ops **nl;

	if(pf<0||pf>=NPROTO)
		return -EINVAL;

	/*
	 *	Don't allow two people to adjust at once.
	 */

	down(&firewall_sem); 

	nl=&firewall_chain[pf];

	while(*nl!=NULL)
	{
		if(*nl==fw)
		{
			struct firewall_ops *f=fw->next;
			*nl = f;
			up(&firewall_sem); 
			synchronize_bh();
			return 0;
		}
		nl=&((*nl)->next);
	}
	up(&firewall_sem);
	return -ENOENT;
}

int call_fw_firewall(int pf, struct device *dev, void *phdr, void *arg, struct sk_buff **skb)
{
	struct firewall_ops *fw=firewall_chain[pf];

	while(fw!=NULL)
	{
		int rc=fw->fw_forward(fw,pf,dev,phdr,arg,skb);
		if(rc!=FW_SKIP)
			return rc;
		fw=fw->next;
	}
	return firewall_policy[pf];
}

/*
 *	Actual invocation of the chains
 */

int call_in_firewall(int pf, struct device *dev, void *phdr, void *arg, struct sk_buff **skb)
{
	struct firewall_ops *fw=firewall_chain[pf];

	while(fw!=NULL)
	{
		int rc=fw->fw_input(fw,pf,dev,phdr,arg,skb);
		if(rc!=FW_SKIP)
			return rc;
		fw=fw->next;
	}
	return firewall_policy[pf];
}

int call_out_firewall(int pf, struct device *dev, void *phdr, void *arg, struct sk_buff **skb)
{
	struct firewall_ops *fw=firewall_chain[pf];

	while(fw!=NULL)
	{
		int rc=fw->fw_output(fw,pf,dev,phdr,arg,skb);
		if(rc!=FW_SKIP)
			return rc;
		fw=fw->next;
	}
	/* alan, is this right? */
	return firewall_policy[pf];
}

EXPORT_SYMBOL(register_firewall);
EXPORT_SYMBOL(unregister_firewall);
EXPORT_SYMBOL(call_in_firewall);
EXPORT_SYMBOL(call_out_firewall);
EXPORT_SYMBOL(call_fw_firewall);

__initfunc(void fwchain_init(void))
{
	int i;
	for(i=0;i<NPROTO;i++)
		firewall_policy[i]=FW_ACCEPT;
}
