
struct nubus_slot
{
	int slot_flags;
#define NUBUS_DEVICE_PRESENT	1
#define NUBUS_DEVICE_ACTIVE	2
#define NUBUS_DEVICE_IRQ	4
	__u32 slot_directory;
	__u32 slot_dlength;
	__u32 slot_crc;
	__u8  slot_rev;
	__u8  slot_format;
	__u8  slot_lanes;
	/*
	 *	Stuff we pulled from the directory
	 */
	__u32  slot_dirbase;
	__u32  slot_thisdir;
	char   slot_vendor[64];
	char   slot_cardname[64];
};

struct nbnamevec 
{
	char *name;
	int id;
};

struct nubus_dir
{
	unsigned char *base;
	int length;
	int count;
	int mask;
};

struct nubus_dirent
{
	unsigned char type;
	int value;	/* Actually 24bits used */
	int mask;
	int base;	/* For dirptr function */
};

struct nubus_type
{
	__u16 category;
	__u16 type;
	__u16 DrHW;
	__u16 DrSW;
};

#define NUBUS_CAT_BOARD			0x0001
#define NUBUS_CAT_DISPLAY		0x0003
#define NUBUS_CAT_NETWORK		0x0004
#define NUBUS_CAT_COMMUNICATIONS	0x0006
#define NUBUS_CAT_FONT			0x0009
#define NUBUS_CAT_CPU			0x000A

#define RES_ID_TYPE		0x0001
#define RES_ID_NAME		0x0002
#define RES_ID_BOARD_DIR	0x0001
#define RES_ID_FLAGS		0x0007

struct nubus_device_specifier
{
	int (*setup)(struct nubus_device_specifier *, int slot, struct nubus_type *);
	struct nubus_device_specifier *next;
};


extern void register_nubus_device(struct nubus_device_specifier *nb);
extern void unregister_nubus_device(struct nubus_device_specifier *nb);

extern struct nubus_dir *nubus_openrootdir(int slot);
extern struct nubus_dir *nubus_opensubdir(struct nubus_dirent *d);
extern void nubus_closedir(struct nubus_dir *);
extern struct nubus_dirent *nubus_readdir(struct nubus_dir *);
extern unsigned char *nubus_dirptr(struct nubus_dirent *d);
extern void nubus_strncpy(int slot, void *to, unsigned char *p, int len);
extern void nubus_memcpy(int slot, void *to, unsigned char *p, int len);
extern void nubus_init(void);
extern void nubus_sweep_video(void);
extern int nubus_ethernet_addr(int slot, unsigned char *addr);

extern __inline void *nubus_slot_addr(int slot)
{
	return (void *)(0xF0000000|(slot<<24));
}

extern int nubus_hwreg_present(volatile void *ptr);

extern void nubus_init_via(void);
extern int nubus_free_irq(int slot);
extern int nubus_request_irq(int slot, void *dev_id, void (*handler)(int,void *,struct pt_regs *));

