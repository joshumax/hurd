/*
 *	IP Masquerading Modules Support
 *
 * Version:	@(#)ip_masq_mod.h  0.01      97/10/30
 *
 * Author:	Juan Jose Ciarlante, <jjciarla@raiz.uncu.edu.ar>
 *
 */


#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ip_fw.h>
#include <linux/proc_fs.h>
#include <net/ip_masq.h>

#define IP_MASQ_MOD_NOP 	0
#define IP_MASQ_MOD_ACCEPT	1
#define IP_MASQ_MOD_REJECT	-1

struct ip_masq_mod {
	struct ip_masq_mod *next;	/* next mod for addrs. lookups */
	struct ip_masq_mod *next_reg;	/* next mod for configuration ctls */
	char *mmod_name;
	atomic_t refcnt;
	atomic_t mmod_nent;		/* number of entries */
	struct proc_dir_entry *mmod_proc_ent;
	int (*mmod_ctl) (int optname, struct ip_masq_ctl *, int optlen);
	int (*mmod_init) (void);
	int (*mmod_done) (void);
	int (*mmod_in_rule)   (const struct sk_buff *, const struct iphdr *);
	int (*mmod_in_update) (const struct sk_buff *, const struct iphdr *, 
		struct ip_masq *);
	struct ip_masq * (*mmod_in_create) (const struct sk_buff *, const struct iphdr *, __u32);
	int (*mmod_out_rule)   (const struct sk_buff *, const struct iphdr *);
	int (*mmod_out_update) (const struct sk_buff *, const struct iphdr *,
		struct ip_masq *);
	struct ip_masq * (*mmod_out_create) (const struct sk_buff *, const struct iphdr *, __u32);
};

/*
 *	Service routines (called from ip_masq.c)
 */

int ip_masq_mod_out_rule(const struct sk_buff *, const struct iphdr *);
int ip_masq_mod_out_update(const struct sk_buff *, const struct iphdr *, struct ip_masq *ms);
struct ip_masq * ip_masq_mod_out_create(const struct sk_buff *, const struct iphdr *iph, __u32 maddr);

int ip_masq_mod_in_rule(const struct sk_buff *, const struct iphdr *iph);
int ip_masq_mod_in_update(const struct sk_buff *, const struct iphdr *iph, struct ip_masq *ms);
struct ip_masq * ip_masq_mod_in_create(const struct sk_buff *, const struct iphdr *iph, __u32 maddr);

extern int ip_masq_mod_ctl(int optname, struct ip_masq_ctl *, int len);

/*
 * 	ip_masq_mod registration functions 
 */
extern int register_ip_masq_mod(struct ip_masq_mod *mmod);
extern int unregister_ip_masq_mod(struct ip_masq_mod *mmod);
extern int ip_masq_mod_lkp_unlink(struct ip_masq_mod *mmod);
extern int ip_masq_mod_lkp_link(struct ip_masq_mod *mmod);

/*
 *	init functions protos
 */
extern int ip_portfw_init(void);
extern int ip_mfw_init(void);
extern int ip_autofw_init(void);

/*
 *	Utility ...
 */
static __inline__ void ip_masq_mod_dec_nent(struct ip_masq_mod *mmod)
{
	if (atomic_dec_and_test(&mmod->mmod_nent)) {
		ip_masq_mod_lkp_unlink(mmod);
	}
}
static __inline__ void ip_masq_mod_inc_nent(struct ip_masq_mod *mmod)
{
	atomic_inc(&mmod->mmod_nent);
	if (atomic_read(&mmod->mmod_nent)==1)
		ip_masq_mod_lkp_link(mmod);
}

#endif /* __KERNEL__ */
