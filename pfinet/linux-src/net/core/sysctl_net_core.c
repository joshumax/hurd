/* -*- linux-c -*-
 * sysctl_net_core.c: sysctl interface to net core subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/core directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/config.h>

#ifdef CONFIG_SYSCTL

extern int netdev_max_backlog;
extern int netdev_fastroute;
extern int net_msg_cost;
extern int net_msg_burst;

extern __u32 sysctl_wmem_max;
extern __u32 sysctl_rmem_max;
extern __u32 sysctl_wmem_default;
extern __u32 sysctl_rmem_default;

extern int sysctl_core_destroy_delay;
extern int sysctl_optmem_max;

ctl_table core_table[] = {
#ifdef CONFIG_NET
	{NET_CORE_WMEM_MAX, "wmem_max",
	 &sysctl_wmem_max, sizeof(int), 0644, NULL,
	 &proc_dointvec},
	{NET_CORE_RMEM_MAX, "rmem_max",
	 &sysctl_rmem_max, sizeof(int), 0644, NULL,
	 &proc_dointvec},
	{NET_CORE_WMEM_DEFAULT, "wmem_default",
	 &sysctl_wmem_default, sizeof(int), 0644, NULL,
	 &proc_dointvec},
	{NET_CORE_RMEM_DEFAULT, "rmem_default",
	 &sysctl_rmem_default, sizeof(int), 0644, NULL,
	 &proc_dointvec},
	{NET_CORE_MAX_BACKLOG, "netdev_max_backlog",
	 &netdev_max_backlog, sizeof(int), 0644, NULL,
	 &proc_dointvec},
#ifdef CONFIG_NET_FASTROUTE
	{NET_CORE_FASTROUTE, "netdev_fastroute",
	 &netdev_fastroute, sizeof(int), 0644, NULL,
	 &proc_dointvec},
#endif
	{NET_CORE_MSG_COST, "message_cost",
	 &net_msg_cost, sizeof(int), 0644, NULL,
	 &proc_dointvec_jiffies},
	{NET_CORE_MSG_BURST, "message_burst",
	 &net_msg_burst, sizeof(int), 0644, NULL,
	 &proc_dointvec_jiffies},
	{NET_CORE_OPTMEM_MAX, "optmem_max",
	 &sysctl_optmem_max, sizeof(int), 0644, NULL,
	 &proc_dointvec},
#endif /* CONFIG_NET */
	{ 0 }
};
#endif
