#ifndef _LINUX_KERNEL_STAT_H
#define _LINUX_KERNEL_STAT_H

#include <asm/irq.h>
#include <linux/smp.h>
#include <linux/tasks.h>

/*
 * 'kernel_stat.h' contains the definitions needed for doing
 * some kernel statistics (CPU usage, context switches ...),
 * used by rstatd/perfmeter
 */

#define DK_NDRIVE 4

struct kernel_stat {
	unsigned int cpu_user, cpu_nice, cpu_system;	
	unsigned int per_cpu_user[NR_CPUS],
	             per_cpu_nice[NR_CPUS],
	             per_cpu_system[NR_CPUS];
	unsigned int dk_drive[DK_NDRIVE];
	unsigned int dk_drive_rio[DK_NDRIVE];
	unsigned int dk_drive_wio[DK_NDRIVE];
	unsigned int dk_drive_rblk[DK_NDRIVE];
	unsigned int dk_drive_wblk[DK_NDRIVE];
	unsigned int pgpgin, pgpgout;
	unsigned int pswpin, pswpout;
	unsigned int irqs[NR_CPUS][NR_IRQS];
	unsigned int ipackets, opackets;
	unsigned int ierrors, oerrors;
	unsigned int collisions;
	unsigned int context_swtch;
};

extern struct kernel_stat kstat;

/*
 * Number of interrupts per specific IRQ source, since bootup
 */
extern inline int kstat_irqs (int irq)
{
	int i, sum=0;

	for (i = 0 ; i < smp_num_cpus ; i++)
		sum += kstat.irqs[cpu_logical_map(i)][irq];

	return sum;
}

#endif /* _LINUX_KERNEL_STAT_H */
