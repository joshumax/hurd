#ifndef _NET_INET_DATALINK_H_
#define _NET_INET_DATALINK_H_

struct datalink_proto {
	unsigned short	type_len;
	unsigned char	type[8];
	char		*string_name;
	unsigned short	header_length;
	int	(*rcvfunc)(struct sk_buff *, struct device *, 
				struct packet_type *);
	void	(*datalink_header)(struct datalink_proto *, struct sk_buff *,
					unsigned char *);
	struct datalink_proto	*next;
};

#endif

