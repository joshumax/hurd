#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include "datalink.h"
#include <linux/mm.h>
#include <linux/in.h>

static void
pEII_datalink_header(struct datalink_proto *dl, 
		struct sk_buff *skb, unsigned char *dest_node)
{
	struct device	*dev = skb->dev;
	unsigned long	len = skb->len;
	unsigned long	hard_len = dev->hard_header_len;

	dev->hard_header(skb->data, dev, ETH_P_IPX,
			dest_node, NULL, len - hard_len, skb);
	skb->h.raw = skb->data + hard_len;
}

struct datalink_proto *
make_EII_client(void)
{
	struct datalink_proto	*proto;

	proto = (struct datalink_proto *) kmalloc(sizeof(*proto), GFP_ATOMIC);
	if (proto != NULL) {
		proto->type_len = 0;
		proto->header_length = 0;
		proto->datalink_header = pEII_datalink_header;
		proto->string_name = "EtherII";
	}

	return proto;
}

