struct datalink_proto *register_8022_client(unsigned char type, int (*rcvfunc)(struct sk_buff *, struct device *, struct packet_type *));

