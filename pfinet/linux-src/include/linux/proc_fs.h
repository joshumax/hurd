#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H

#include <linux/config.h>
#include <linux/malloc.h>

/*
 * The proc filesystem constants/structures
 */

/*
 * We always define these enumerators
 */

enum root_directory_inos {
	PROC_ROOT_INO = 1,
	PROC_LOADAVG,
	PROC_UPTIME,
	PROC_MEMINFO,
	PROC_KMSG,
	PROC_VERSION,
	PROC_CPUINFO,
	PROC_PCI,
	PROC_MCA,
	PROC_NUBUS,
	PROC_SELF,	/* will change inode # */
	PROC_NET,
        PROC_SCSI,
	PROC_MALLOC,
	PROC_KCORE,
	PROC_MODULES,
	PROC_STAT,
	PROC_DEVICES,
	PROC_PARTITIONS,
	PROC_INTERRUPTS,
	PROC_FILESYSTEMS,
	PROC_KSYMS,
	PROC_DMA,	
	PROC_IOPORTS,
	PROC_PROFILE, /* whether enabled or not */
	PROC_CMDLINE,
	PROC_SYS,
	PROC_MTAB,
	PROC_SWAP,
	PROC_MD,
	PROC_RTC,
	PROC_LOCKS,
	PROC_HARDWARE,
	PROC_SLABINFO,
	PROC_PARPORT,
	PROC_PPC_HTAB,
	PROC_STRAM,
	PROC_SOUND,
	PROC_MTRR, /* whether enabled or not */
	PROC_FS
};

enum pid_directory_inos {
	PROC_PID_INO = 2,
	PROC_PID_STATUS,
	PROC_PID_MEM,
	PROC_PID_CWD,
	PROC_PID_ROOT,
	PROC_PID_EXE,
	PROC_PID_FD,
	PROC_PID_ENVIRON,
	PROC_PID_CMDLINE,
	PROC_PID_STAT,
	PROC_PID_STATM,
	PROC_PID_MAPS,
#if CONFIG_AP1000
	PROC_PID_RINGBUF,
#endif
	PROC_PID_CPU,
};

enum pid_subdirectory_inos {
	PROC_PID_FD_DIR = 0x8000,	/* 0x8000-0xffff */
};

enum net_directory_inos {
	PROC_NET_UNIX = 128,
	PROC_NET_ARP,
	PROC_NET_ROUTE,
	PROC_NET_DEV,
	PROC_NET_RAW,
	PROC_NET_RAW6,
	PROC_NET_TCP,
	PROC_NET_TCP6,
	PROC_NET_UDP,
	PROC_NET_UDP6,
	PROC_NET_SNMP,
	PROC_NET_RARP,
	PROC_NET_IGMP,
	PROC_NET_IPMR_VIF,
	PROC_NET_IPMR_MFC,
	PROC_NET_IPFWFWD,
	PROC_NET_IPFWIN,
	PROC_NET_IPFWOUT,
	PROC_NET_IPACCT,
	PROC_NET_IPMSQHST,
	PROC_NET_WIRELESS,
	PROC_NET_IPX_INTERFACE,
	PROC_NET_IPX_ROUTE,
	PROC_NET_IPX,
	PROC_NET_ATALK,
	PROC_NET_AT_ROUTE,
	PROC_NET_ATIF,
	PROC_NET_AX25_ROUTE,
	PROC_NET_AX25,
	PROC_NET_AX25_CALLS,
	PROC_NET_BMAC,
	PROC_NET_NR_NODES,
	PROC_NET_NR_NEIGH,
	PROC_NET_NR,
	PROC_NET_SOCKSTAT,
	PROC_NET_SOCKSTAT6,
	PROC_NET_RTCACHE,
	PROC_NET_AX25_BPQETHER,
	PROC_NET_IP_MASQ_APP,
	PROC_NET_RT6,
	PROC_NET_SNMP6,
	PROC_NET_RT6_STATS,
	PROC_NET_NDISC,
	PROC_NET_STRIP_STATUS,
	PROC_NET_STRIP_TRACE,
	PROC_NET_Z8530,
	PROC_NET_RS_NODES,
	PROC_NET_RS_NEIGH,
	PROC_NET_RS_ROUTES,
	PROC_NET_RS,
	PROC_NET_CL2LLC,
	PROC_NET_X25_ROUTES,
	PROC_NET_X25,
	PROC_NET_TR_RIF,
	PROC_NET_DN_DEV,
	PROC_NET_DN_ADJ,
	PROC_NET_DN_L1,
	PROC_NET_DN_L2,
	PROC_NET_DN_CACHE,
	PROC_NET_DN_SKT,
	PROC_NET_DN_FW,
	PROC_NET_DN_RAW,
	PROC_NET_NETSTAT,
	PROC_NET_IPFW_CHAINS,
	PROC_NET_IPFW_CHAIN_NAMES,
	PROC_NET_AT_AARP,
	PROC_NET_BRIDGE,
	PROC_NET_LAST
};

enum scsi_directory_inos {
	PROC_SCSI_SCSI = 256,
	PROC_SCSI_ADVANSYS,
	PROC_SCSI_PCI2000,
	PROC_SCSI_PCI2220I,
	PROC_SCSI_PSI240I,
	PROC_SCSI_EATA,
	PROC_SCSI_EATA_PIO,
	PROC_SCSI_AHA152X,
	PROC_SCSI_AHA1542,
	PROC_SCSI_AHA1740,
	PROC_SCSI_AIC7XXX,
	PROC_SCSI_BUSLOGIC,
	PROC_SCSI_U14_34F,
	PROC_SCSI_FDOMAIN,
	PROC_SCSI_GDTH,
	PROC_SCSI_GENERIC_NCR5380,
	PROC_SCSI_IN2000,
	PROC_SCSI_PAS16,
	PROC_SCSI_QLOGICFAS,
	PROC_SCSI_QLOGICISP,
	PROC_SCSI_QLOGICFC,
	PROC_SCSI_SEAGATE,
	PROC_SCSI_T128,
	PROC_SCSI_NCR53C7xx,
	PROC_SCSI_SYM53C8XX,
	PROC_SCSI_NCR53C8XX,
	PROC_SCSI_ULTRASTOR,
	PROC_SCSI_7000FASST,
	PROC_SCSI_IBMMCA,
	PROC_SCSI_FD_MCS,
	PROC_SCSI_EATA2X,
	PROC_SCSI_DC390T,
	PROC_SCSI_AM53C974,
	PROC_SCSI_SSC,
	PROC_SCSI_NCR53C406A,
	PROC_SCSI_SYM53C416,
	PROC_SCSI_MEGARAID,
	PROC_SCSI_PPA,
	PROC_SCSI_ATP870U,
	PROC_SCSI_ESP,
	PROC_SCSI_QLOGICPTI,
	PROC_SCSI_AMIGA7XX,
	PROC_SCSI_MVME16x,
	PROC_SCSI_BVME6000,
	PROC_SCSI_SIM710,
	PROC_SCSI_A3000,
	PROC_SCSI_A2091,
	PROC_SCSI_GVP11,
	PROC_SCSI_ATARI,
	PROC_SCSI_MAC,
	PROC_SCSI_IDESCSI,
	PROC_SCSI_SGIWD93,
	PROC_SCSI_MESH,
	PROC_SCSI_53C94,
	PROC_SCSI_PLUTO,
	PROC_SCSI_INI9100U,
	PROC_SCSI_INIA100,
 	PROC_SCSI_IPH5526_FC,
	PROC_SCSI_FCAL,
	PROC_SCSI_I2O,
	PROC_SCSI_USB_SCSI,
	PROC_SCSI_SCSI_DEBUG,	
	PROC_SCSI_NOT_PRESENT,
	PROC_SCSI_FILE,                        /* I'm assuming here that we */
	PROC_SCSI_LAST = (PROC_SCSI_FILE + 16) /* won't ever see more than */
};                                             /* 16 HBAs in one machine   */

enum mca_directory_inos {
	PROC_MCA_MACHINE = (PROC_SCSI_LAST+1),
	PROC_MCA_REGISTERS,
	PROC_MCA_VIDEO,
	PROC_MCA_SCSI,
	PROC_MCA_SLOT,	/* the 8 adapter slots */
	PROC_MCA_LAST = (PROC_MCA_SLOT + 8)
};

enum bus_directory_inos {
	PROC_BUS_PCI = PROC_MCA_LAST,
	PROC_BUS_PCI_DEVICES,
	PROC_BUS_ZORRO,
	PROC_BUS_ZORRO_DEVICES,
	PROC_BUS_LAST
};

enum fs_directory_inos {
	PROC_FS_CODA = PROC_BUS_LAST,
	PROC_FS_LAST
};

enum fs_coda_directory_inos {
	PROC_VFS_STATS = PROC_FS_LAST,
	PROC_UPCALL_STATS,
	PROC_PERMISSION_STATS,
	PROC_CACHE_INV_STATS,
	PROC_CODA_FS_LAST
};

/* Finally, the dynamically allocatable proc entries are reserved: */

#define PROC_DYNAMIC_FIRST 4096
#define PROC_NDYNAMIC      4096
#define PROC_OPENPROM_FIRST (PROC_DYNAMIC_FIRST+PROC_NDYNAMIC)
#define PROC_OPENPROM	   PROC_OPENPROM_FIRST
#define PROC_NOPENPROM	   4096
#define PROC_OPENPROMD_FIRST (PROC_OPENPROM_FIRST+PROC_NOPENPROM)
#define PROC_NOPENPROMD	   4096

#define PROC_SUPER_MAGIC 0x9fa0

/*
 * This is not completely implemented yet. The idea is to
 * create an in-memory tree (like the actual /proc filesystem
 * tree) of these proc_dir_entries, so that we can dynamically
 * add new files to /proc.
 *
 * The "next" pointer creates a linked list of one /proc directory,
 * while parent/subdir create the directory structure (every
 * /proc file has a parent, but "subdir" is NULL for all
 * non-directory entries).
 *
 * "get_info" is called at "read", while "fill_inode" is used to
 * fill in file type/protection/owner information specific to the
 * particular /proc file.
 */
struct proc_dir_entry {
	unsigned short low_ino;
	unsigned short namelen;
	const char *name;
	mode_t mode;
	nlink_t nlink;
	uid_t uid;
	gid_t gid;
	unsigned long size;
	struct inode_operations * ops;
	int (*get_info)(char *, char **, off_t, int, int);
	void (*fill_inode)(struct inode *, int);
	struct proc_dir_entry *next, *parent, *subdir;
	void *data;
	int (*read_proc)(char *page, char **start, off_t off,
			 int count, int *eof, void *data);
	int (*write_proc)(struct file *file, const char *buffer,
			  unsigned long count, void *data);
	int (*readlink_proc)(struct proc_dir_entry *de, char *page);
	unsigned int count;	/* use count */
	int deleted;		/* delete flag */
};

typedef	int (read_proc_t)(char *page, char **start, off_t off,
			  int count, int *eof, void *data);
typedef	int (write_proc_t)(struct file *file, const char *buffer,
			   unsigned long count, void *data);

extern int (* dispatch_scsi_info_ptr) (int ino, char *buffer, char **start,
				off_t offset, int length, int inout);

#ifdef CONFIG_PROC_FS

extern struct proc_dir_entry proc_root;
extern struct proc_dir_entry proc_root_fs;
extern struct proc_dir_entry *proc_net;
extern struct proc_dir_entry *proc_scsi;
extern struct proc_dir_entry proc_sys;
extern struct proc_dir_entry proc_openprom;
extern struct proc_dir_entry proc_pid;
extern struct proc_dir_entry proc_pid_fd;
extern struct proc_dir_entry proc_mca;
extern struct proc_dir_entry *proc_bus;

extern struct inode_operations proc_scsi_inode_operations;

extern void proc_root_init(void);
extern void proc_base_init(void);

extern int proc_register(struct proc_dir_entry *, struct proc_dir_entry *);
extern int proc_unregister(struct proc_dir_entry *, int);

static inline int proc_net_register(struct proc_dir_entry * x)
{
	return proc_register(proc_net, x);
}

static inline int proc_net_unregister(int x)
{
	return proc_unregister(proc_net, x);
}

static inline int proc_scsi_register(struct proc_dir_entry *driver, 
				     struct proc_dir_entry *x)
{
    x->ops = &proc_scsi_inode_operations;
    if(x->low_ino < PROC_SCSI_FILE){
	return(proc_register(proc_scsi, x));
    }else{
	return(proc_register(driver, x));
    }
}

static inline int proc_scsi_unregister(struct proc_dir_entry *driver, int x)
{
    extern void scsi_init_free(char *ptr, unsigned int size);

    if(x < PROC_SCSI_FILE)
	return(proc_unregister(proc_scsi, x));
    else {
	struct proc_dir_entry **p = &driver->subdir, *dp;
	int ret;

	while ((dp = *p) != NULL) {
		if (dp->low_ino == x) 
		    break;
		p = &dp->next;
	}
	ret = proc_unregister(driver, x);
	scsi_init_free((char *) dp, sizeof(struct proc_dir_entry) + 4);
	return(ret);
    }
}

extern struct dentry_operations proc_dentry_operations;
extern struct super_block *proc_read_super(struct super_block *,void *,int);
extern int init_proc_fs(void);
extern struct inode * proc_get_inode(struct super_block *, int, struct proc_dir_entry *);
extern int proc_statfs(struct super_block *, struct statfs *, int);
extern void proc_read_inode(struct inode *);
extern void proc_write_inode(struct inode *);
extern int proc_permission(struct inode *, int);

extern int proc_match(int, const char *,struct proc_dir_entry *);

/*
 * These are generic /proc routines that use the internal
 * "struct proc_dir_entry" tree to traverse the filesystem.
 *
 * The /proc root directory has extended versions to take care
 * of the /proc/<pid> subdirectories.
 */
extern int proc_readdir(struct file *, void *, filldir_t);
extern struct dentry *proc_lookup(struct inode *, struct dentry *);

struct openpromfs_dev {
 	struct openpromfs_dev *next;
 	u32 node;
 	ino_t inode;
 	kdev_t rdev;
 	mode_t mode;
 	char name[32];
};
extern struct inode_operations *
proc_openprom_register(int (*readdir)(struct file *, void *, filldir_t),
		       struct dentry * (*lookup)(struct inode *, struct dentry *),
		       void (*use)(struct inode *, int),
		       struct openpromfs_dev ***);
extern void proc_openprom_deregister(void);
extern void (*proc_openprom_use)(struct inode *,int);
extern int proc_openprom_regdev(struct openpromfs_dev *);
extern int proc_openprom_unregdev(struct openpromfs_dev *);
  
extern struct inode_operations proc_dir_inode_operations;
extern struct inode_operations proc_file_inode_operations;
extern struct inode_operations proc_net_inode_operations;
extern struct inode_operations proc_netdir_inode_operations;
extern struct inode_operations proc_openprom_inode_operations;
extern struct inode_operations proc_mem_inode_operations;
extern struct inode_operations proc_sys_inode_operations;
extern struct inode_operations proc_array_inode_operations;
extern struct inode_operations proc_arraylong_inode_operations;
extern struct inode_operations proc_kcore_inode_operations;
extern struct inode_operations proc_profile_inode_operations;
extern struct inode_operations proc_kmsg_inode_operations;
extern struct inode_operations proc_link_inode_operations;
extern struct inode_operations proc_fd_inode_operations;
#if CONFIG_AP1000
extern struct inode_operations proc_ringbuf_inode_operations;
#endif
extern struct inode_operations proc_omirr_inode_operations;
extern struct inode_operations proc_ppc_htab_inode_operations;

/*
 * generic.c
 */
struct proc_dir_entry *create_proc_entry(const char *name, mode_t mode,
					 struct proc_dir_entry *parent);
void remove_proc_entry(const char *name, struct proc_dir_entry *parent);

/*
 * proc_tty.c
 */
extern void proc_tty_init(void);
extern void proc_tty_register_driver(struct tty_driver *driver);
extern void proc_tty_unregister_driver(struct tty_driver *driver);

/*
 * proc_devtree.c
 */
extern void proc_device_tree_init(void);

#else

extern inline int proc_register(struct proc_dir_entry *a, struct proc_dir_entry *b) { return 0; };
extern inline int proc_unregister(struct proc_dir_entry *a, int b) { return 0; };
extern inline int proc_net_register(struct proc_dir_entry *a) { return 0; };
extern inline int proc_net_unregister(int x) { return 0; };
extern inline int proc_scsi_register(struct proc_dir_entry *b, struct proc_dir_entry *c) { return 0; };
extern inline int proc_scsi_unregister(struct proc_dir_entry *a, int x) { return 0; };

extern inline struct proc_dir_entry *create_proc_entry(const char *name, mode_t mode,
					 struct proc_dir_entry *parent)
{
	return NULL;
}

extern inline void remove_proc_entry(const char *name, struct proc_dir_entry *parent) {};

extern inline void proc_tty_register_driver(struct tty_driver *driver) {};
extern inline void proc_tty_unregister_driver(struct tty_driver *driver) {};

extern struct proc_dir_entry proc_root;

#endif /* CONFIG_PROC_FS */
#endif /* _LINUX_PROC_FS_H */
