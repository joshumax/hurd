#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include "datalink.h"
#include <linux/mm.h>
#include <linux/in.h>

static struct datalink_proto *p8022_list = NULL;

static struct datalink_proto *
find_8022_client(unsigned char type)
{
	struct datalink_proto	*proto;

	for (proto = p8022_list;
		((proto != NULL) && (*(proto->type) != type));
		proto = proto->next)
		;

	return proto;
}

int
p8022_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct datalink_proto	*proto;

	proto = find_8022_client(*(skb->h.raw));
	if (proto != NULL) {
		skb->h.raw += 3;
		skb->len -= 3;
		return proto->rcvfunc(skb, dev, pt);
	}

	skb->sk = NULL;
	kfree_skb(skb, FREE_READ);
	return 0;
}

static void
p8022_datalink_header(struct datalink_proto *dl, 
		struct sk_buff *skb, unsigned char *dest_node)
{
	struct device	*dev = skb->dev;
	unsigned long	len = skb->len;
	unsigned long	hard_len = dev->hard_header_len;
	unsigned char	*rawp;

	dev->hard_header(skb->data, dev, len - hard_len,
			dest_node, NULL, len - hard_len, skb);
	rawp = skb->data + hard_len;
	*rawp = dl->type[0];
	rawp++;
	*rawp = dl->type[0];
	rawp++;
	*rawp = 0x03;	/* UI */
	rawp++;
	skb->h.raw = rawp;
}

static struct packet_type p8022_packet_type = 
{
	0,	/* MUTTER ntohs(ETH_P_IPX),*/
	NULL,		/* All devices */
	p8022_rcv,
	NULL,
	NULL,
};
 

void p8022_proto_init(struct net_proto *pro)
{
	p8022_packet_type.type=htons(ETH_P_802_2);
	dev_add_pack(&p8022_packet_type);
}
	
struct datalink_proto *
register_8022_client(unsigned char type, int (*rcvfunc)(struct sk_buff *, struct device *, struct packet_type *))
{
	struct datalink_proto	*proto;

	if (find_8022_client(type) != NULL)
		return NULL;

	proto = (struct datalink_proto *) kmalloc(sizeof(*proto), GFP_ATOMIC);
	if (proto != NULL) {
		proto->type[0] = type;
		proto->type_len = 1;
		proto->rcvfunc = rcvfunc;
		proto->header_length = 3;
		proto->datalink_header = p8022_datalink_header;
		proto->string_name = "802.2";
		proto->next = p8022_list;
		p8022_list = proto;
	}

	return proto;
}

