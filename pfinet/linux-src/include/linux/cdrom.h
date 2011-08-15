/*
 * -- <linux/cdrom.h>
 * General header file for linux CD-ROM drivers 
 * Copyright (C) 1992         David Giller, rafetmad@oxy.edu
 *               1994, 1995   Eberhard Moenkeberg, emoenke@gwdg.de
 *               1996         David van Leeuwen, david@tm.tno.nl
 *               1997, 1998   Erik Andersen, andersee@debian.org
 *               1998, 1999   Jens Axboe, axboe@image.dk
 */
 
#ifndef	_LINUX_CDROM_H
#define	_LINUX_CDROM_H

/*******************************************************
 * As of Linux 2.1.x, all Linux CD-ROM application programs will use this 
 * (and only this) include file.  It is my hope to provide Linux with
 * a uniform interface between software accessing CD-ROMs and the various 
 * device drivers that actually talk to the drives.  There may still be
 * 23 different kinds of strange CD-ROM drives, but at least there will 
 * now be one, and only one, Linux CD-ROM interface.
 *
 * Additionally, as of Linux 2.1.x, all Linux application programs 
 * should use the O_NONBLOCK option when opening a CD-ROM device 
 * for subsequent ioctl commands.  This allows for neat system errors 
 * like "No medium found" or "Wrong medium type" upon attempting to 
 * mount or play an empty slot, mount an audio disc, or play a data disc.
 * Generally, changing an application program to support O_NONBLOCK
 * is as easy as the following:
 *       -    drive = open("/dev/cdrom", O_RDONLY);
 *       +    drive = open("/dev/cdrom", O_RDONLY | O_NONBLOCK);
 * It is worth the small change.
 *
 *  Patches for many common CD programs (provided by David A. van Leeuwen)
 *  can be found at:  ftp://ftp.gwdg.de/pub/linux/cdrom/drivers/cm206/
 * 
 *******************************************************/

/* When a driver supports a certain function, but the cdrom drive we are 
 * using doesn't, we will return the error EDRIVE_CANT_DO_THIS.  We will 
 * borrow the "Operation not supported" error from the network folks to 
 * accomplish this.  Maybe someday we will get a more targeted error code, 
 * but this will do for now... */
#define EDRIVE_CANT_DO_THIS  EOPNOTSUPP

/*******************************************************
 * The CD-ROM IOCTL commands  -- these should be supported by 
 * all the various cdrom drivers.  For the CD-ROM ioctls, we 
 * will commandeer byte 0x53, or 'S'.
 *******************************************************/
#define CDROMPAUSE		0x5301 /* Pause Audio Operation */ 
#define CDROMRESUME		0x5302 /* Resume paused Audio Operation */
#define CDROMPLAYMSF		0x5303 /* Play Audio MSF (struct cdrom_msf) */
#define CDROMPLAYTRKIND		0x5304 /* Play Audio Track/index 
                                           (struct cdrom_ti) */
#define CDROMREADTOCHDR		0x5305 /* Read TOC header 
                                           (struct cdrom_tochdr) */
#define CDROMREADTOCENTRY	0x5306 /* Read TOC entry 
                                           (struct cdrom_tocentry) */
#define CDROMSTOP		0x5307 /* Stop the cdrom drive */
#define CDROMSTART		0x5308 /* Start the cdrom drive */
#define CDROMEJECT		0x5309 /* Ejects the cdrom media */
#define CDROMVOLCTRL		0x530a /* Control output volume 
                                           (struct cdrom_volctrl) */
#define CDROMSUBCHNL		0x530b /* Read subchannel data 
                                           (struct cdrom_subchnl) */
#define CDROMREADMODE2		0x530c /* Read CDROM mode 2 data (2336 Bytes) 
                                           (struct cdrom_read) */
#define CDROMREADMODE1		0x530d /* Read CDROM mode 1 data (2048 Bytes)
                                           (struct cdrom_read) */
#define CDROMREADAUDIO		0x530e /* (struct cdrom_read_audio) */
#define CDROMEJECT_SW		0x530f /* enable(1)/disable(0) auto-ejecting */
#define CDROMMULTISESSION	0x5310 /* Obtain the start-of-last-session 
                                           address of multi session disks 
                                           (struct cdrom_multisession) */
#define CDROM_GET_MCN		0x5311 /* Obtain the "Universal Product Code" 
                                           if available (struct cdrom_mcn) */
#define CDROM_GET_UPC		CDROM_GET_MCN  /* This one is depricated, 
                                          but here anyway for compatibility */
#define CDROMRESET		0x5312 /* hard-reset the drive */
#define CDROMVOLREAD		0x5313 /* Get the drive's volume setting 
                                          (struct cdrom_volctrl) */
#define CDROMREADRAW		0x5314	/* read data in raw mode (2352 Bytes)
                                           (struct cdrom_read) */
/* 
 * These ioctls are used only used in aztcd.c and optcd.c
 */
#define CDROMREADCOOKED		0x5315	/* read data in cooked mode */
#define CDROMSEEK		0x5316  /* seek msf address */
  
/*
 * This ioctl is only used by the scsi-cd driver.  
   It is for playing audio in logical block addressing mode.
 */
#define CDROMPLAYBLK		0x5317	/* (struct cdrom_blk) */

/* 
 * These ioctls are used only used in optcd.c
 */
#define CDROMREADALL		0x5318	/* read all 2646 bytes */
#define CDROMCLOSETRAY		0x5319	/* pendant of CDROMEJECT */

/* 
 * These ioctls are implemented through the uniform CD-ROM driver
 * They _will_ be adopted by all CD-ROM drivers, when all the CD-ROM
 * drivers are eventually ported to the uniform CD-ROM driver interface.
 */
#define CDROM_SET_OPTIONS	0x5320  /* Set behavior options */
#define CDROM_CLEAR_OPTIONS	0x5321  /* Clear behavior options */
#define CDROM_SELECT_SPEED	0x5322  /* Set the CD-ROM speed */
#define CDROM_SELECT_DISC	0x5323  /* Select disc (for juke-boxes) */
#define CDROM_MEDIA_CHANGED	0x5325  /* Check is media changed  */
#define CDROM_DRIVE_STATUS	0x5326  /* Get tray position, etc. */
#define CDROM_DISC_STATUS	0x5327  /* Get disc type, etc. */
#define CDROM_CHANGER_NSLOTS    0x5328  /* Get number of slots */
#define CDROM_LOCKDOOR		0x5329  /* lock or unlock door */
#define CDROM_DEBUG		0x5330	/* Turn debug messages on/off */
#define CDROM_GET_CAPABILITY	0x5331	/* get capabilities */

/* This ioctl is only used by sbpcd at the moment */
#define CDROMAUDIOBUFSIZ        0x5382	/* set the audio buffer size */

/*******************************************************
 * CDROM IOCTL structures
 *******************************************************/

/* Address in MSF format */
struct cdrom_msf0		
{
	u_char	minute;
	u_char	second;
	u_char	frame;
};

/* Address in either MSF or logical format */
union cdrom_addr		
{
	struct cdrom_msf0	msf;
	int			lba;
};

/* This struct is used by the CDROMPLAYMSF ioctl */ 
struct cdrom_msf 
{
	u_char	cdmsf_min0;	/* start minute */
	u_char	cdmsf_sec0;	/* start second */
	u_char	cdmsf_frame0;	/* start frame */
	u_char	cdmsf_min1;	/* end minute */
	u_char	cdmsf_sec1;	/* end second */
	u_char	cdmsf_frame1;	/* end frame */
};

/* This struct is used by the CDROMPLAYTRKIND ioctl */
struct cdrom_ti 
{
	u_char	cdti_trk0;	/* start track */
	u_char	cdti_ind0;	/* start index */
	u_char	cdti_trk1;	/* end track */
	u_char	cdti_ind1;	/* end index */
};

/* This struct is used by the CDROMREADTOCHDR ioctl */
struct cdrom_tochdr 	
{
	u_char	cdth_trk0;	/* start track */
	u_char	cdth_trk1;	/* end track */
};

/* This struct is used by the CDROMVOLCTRL and CDROMVOLREAD ioctls */
struct cdrom_volctrl
{
	u_char	channel0;
	u_char	channel1;
	u_char	channel2;
	u_char	channel3;
};

/* This struct is used by the CDROMSUBCHNL ioctl */
struct cdrom_subchnl 
{
	u_char	cdsc_format;
	u_char	cdsc_audiostatus;
	u_char	cdsc_adr:	4;
	u_char	cdsc_ctrl:	4;
	u_char	cdsc_trk;
	u_char	cdsc_ind;
	union cdrom_addr cdsc_absaddr;
	union cdrom_addr cdsc_reladdr;
};


/* This struct is used by the CDROMREADTOCENTRY ioctl */
struct cdrom_tocentry 
{
	u_char	cdte_track;
	u_char	cdte_adr	:4;
	u_char	cdte_ctrl	:4;
	u_char	cdte_format;
	union cdrom_addr cdte_addr;
	u_char	cdte_datamode;
};

/* This struct is used by the CDROMREADMODE1, and CDROMREADMODE2 ioctls */
struct cdrom_read      
{
	int	cdread_lba;
	caddr_t	cdread_bufaddr;
	int	cdread_buflen;
};

/* This struct is used by the CDROMREADAUDIO ioctl */
struct cdrom_read_audio
{
	union cdrom_addr addr; /* frame address */
	u_char addr_format;    /* CDROM_LBA or CDROM_MSF */
	int nframes;           /* number of 2352-byte-frames to read at once */
	u_char *buf;           /* frame buffer (size: nframes*2352 bytes) */
};

/* This struct is used with the CDROMMULTISESSION ioctl */
struct cdrom_multisession
{
	union cdrom_addr addr; /* frame address: start-of-last-session 
	                           (not the new "frame 16"!).  Only valid
	                           if the "xa_flag" is true. */
	u_char xa_flag;        /* 1: "is XA disk" */
	u_char addr_format;    /* CDROM_LBA or CDROM_MSF */
};

/* This struct is used with the CDROM_GET_MCN ioctl.  
 * Very few audio discs actually have Universal Product Code information, 
 * which should just be the Medium Catalog Number on the box.  Also note 
 * that the way the codeis written on CD is _not_ uniform across all discs!
 */  
struct cdrom_mcn 
{
  u_char medium_catalog_number[14]; /* 13 ASCII digits, null-terminated */
};

/* This is used by the CDROMPLAYBLK ioctl */
struct cdrom_blk 
{
	unsigned from;
	unsigned short len;
};


/*
 * A CD-ROM physical sector size is 2048, 2052, 2056, 2324, 2332, 2336, 
 * 2340, or 2352 bytes long.  

*         Sector types of the standard CD-ROM data formats:
 *
 * format   sector type               user data size (bytes)
 * -----------------------------------------------------------------------------
 *   1     (Red Book)    CD-DA          2352    (CD_FRAMESIZE_RAW)
 *   2     (Yellow Book) Mode1 Form1    2048    (CD_FRAMESIZE)
 *   3     (Yellow Book) Mode1 Form2    2336    (CD_FRAMESIZE_RAW0)
 *   4     (Green Book)  Mode2 Form1    2048    (CD_FRAMESIZE)
 *   5     (Green Book)  Mode2 Form2    2328    (2324+4 spare bytes)
 *
 *
 *       The layout of the standard CD-ROM data formats:
 * -----------------------------------------------------------------------------
 * - audio (red):                  | audio_sample_bytes |
 *                                 |        2352        |
 *
 * - data (yellow, mode1):         | sync - head - data - EDC - zero - ECC |
 *                                 |  12  -   4  - 2048 -  4  -   8  - 276 |
 *
 * - data (yellow, mode2):         | sync - head - data |
 *                                 |  12  -   4  - 2336 |
 *
 * - XA data (green, mode2 form1): | sync - head - sub - data - EDC - ECC |
 *                                 |  12  -   4  -  8  - 2048 -  4  - 276 |
 *
 * - XA data (green, mode2 form2): | sync - head - sub - data - Spare |
 *                                 |  12  -   4  -  8  - 2324 -  4    |
 *
 */

/* Some generally useful CD-ROM information -- mostly based on the above */
#define CD_MINS              74 /* max. minutes per CD, not really a limit */
#define CD_SECS              60 /* seconds per minute */
#define CD_FRAMES            75 /* frames per second */
#define CD_SYNC_SIZE         12 /* 12 sync bytes per raw data frame */
#define CD_MSF_OFFSET       150 /* MSF numbering offset of first frame */
#define CD_CHUNK_SIZE        24 /* lowest-level "data bytes piece" */
#define CD_NUM_OF_CHUNKS     98 /* chunks per frame */
#define CD_FRAMESIZE_SUB     96 /* subchannel data "frame" size */
#define CD_HEAD_SIZE          4 /* header (address) bytes per raw data frame */
#define CD_SUBHEAD_SIZE       8 /* subheader bytes per raw XA data frame */
#define CD_EDC_SIZE           4 /* bytes EDC per most raw data frame types */
#define CD_ZERO_SIZE          8 /* bytes zero per yellow book mode 1 frame */
#define CD_ECC_SIZE         276 /* bytes ECC per most raw data frame types */
#define CD_FRAMESIZE       2048 /* bytes per frame, "cooked" mode */
#define CD_FRAMESIZE_RAW   2352 /* bytes per frame, "raw" mode */
#define CD_FRAMESIZE_RAWER 2646 /* The maximum possible returned bytes */ 
/* most drives don't deliver everything: */
#define CD_FRAMESIZE_RAW1 (CD_FRAMESIZE_RAW-CD_SYNC_SIZE) /*2340*/
#define CD_FRAMESIZE_RAW0 (CD_FRAMESIZE_RAW-CD_SYNC_SIZE-CD_HEAD_SIZE) /*2336*/

#define CD_XA_HEAD        (CD_HEAD_SIZE+CD_SUBHEAD_SIZE) /* "before data" part of raw XA frame */
#define CD_XA_TAIL        (CD_EDC_SIZE+CD_ECC_SIZE) /* "after data" part of raw XA frame */
#define CD_XA_SYNC_HEAD   (CD_SYNC_SIZE+CD_XA_HEAD) /* sync bytes + header of XA frame */

/* CD-ROM address types (cdrom_tocentry.cdte_format) */
#define	CDROM_LBA 0x01 /* "logical block": first frame is #0 */
#define	CDROM_MSF 0x02 /* "minute-second-frame": binary, not bcd here! */

/* bit to tell whether track is data or audio (cdrom_tocentry.cdte_ctrl) */
#define	CDROM_DATA_TRACK	0x04

/* The leadout track is always 0xAA, regardless of # of tracks on disc */
#define	CDROM_LEADOUT		0xAA

/* audio states (from SCSI-2, but seen with other drives, too) */
#define	CDROM_AUDIO_INVALID	0x00	/* audio status not supported */
#define	CDROM_AUDIO_PLAY	0x11	/* audio play operation in progress */
#define	CDROM_AUDIO_PAUSED	0x12	/* audio play operation paused */
#define	CDROM_AUDIO_COMPLETED	0x13	/* audio play successfully completed */
#define	CDROM_AUDIO_ERROR	0x14	/* audio play stopped due to error */
#define	CDROM_AUDIO_NO_STATUS	0x15	/* no current audio status to return */

/* CD-ROM-specific SCSI command opcodes */
#define SCMD_READ_TOC		0x43	/* read table of contents */
#define SCMD_PLAYAUDIO_MSF	0x47	/* play data at time offset */
#define SCMD_PLAYAUDIO_TI	0x48	/* play data at track/index */
#define SCMD_PAUSE_RESUME	0x4B	/* pause/resume audio */
#define SCMD_READ_SUBCHANNEL	0x42	/* read SC info on playing disc */
#define SCMD_PLAYAUDIO10	0x45	/* play data at logical block */

/* capability flags used with the uniform CD-ROM driver */ 
#define CDC_CLOSE_TRAY		0x1     /* caddy systems _can't_ close */
#define CDC_OPEN_TRAY		0x2     /* but _can_ eject.  */
#define CDC_LOCK		0x4     /* disable manual eject */
#define CDC_SELECT_SPEED 	0x8     /* programmable speed */
#define CDC_SELECT_DISC		0x10    /* select disc from juke-box */
#define CDC_MULTI_SESSION 	0x20    /* read sessions>1 */
#define CDC_MCN			0x40    /* Medium Catalog Number */
#define CDC_MEDIA_CHANGED 	0x80    /* media changed */
#define CDC_PLAY_AUDIO		0x100   /* audio functions */
#define CDC_RESET               0x200   /* hard reset device */
#define CDC_IOCTLS              0x400   /* driver has non-standard ioctls */
#define CDC_DRIVE_STATUS        0x800   /* driver implements drive status */

/* drive status possibilities returned by CDROM_DRIVE_STATUS ioctl */
#define CDS_NO_INFO		0	/* if not implemented */
#define CDS_NO_DISC		1
#define CDS_TRAY_OPEN		2
#define CDS_DRIVE_NOT_READY	3
#define CDS_DISC_OK		4

/* return values for the CDROM_DISC_STATUS ioctl */
/* can also return CDS_NO_[INFO|DISC], from above */
#define CDS_AUDIO		100
#define CDS_DATA_1		101
#define CDS_DATA_2		102
#define CDS_XA_2_1		103
#define CDS_XA_2_2		104
#define CDS_MIXED		105

/* User-configurable behavior options for the uniform CD-ROM driver */
#define CDO_AUTO_CLOSE		0x1     /* close tray on first open() */
#define CDO_AUTO_EJECT		0x2     /* open tray on last release() */
#define CDO_USE_FFLAGS		0x4     /* use O_NONBLOCK information on open */
#define CDO_LOCK		0x8     /* lock tray on open files */
#define CDO_CHECK_TYPE		0x10    /* check type on open for data */

/* Special codes used when specifying changer slots. */
#define CDSL_NONE       	((int) (~0U>>1)-1)
#define CDSL_CURRENT    	((int) (~0U>>1))

#ifdef __KERNEL__
/* Uniform cdrom data structures for cdrom.c */
struct cdrom_device_info {
	struct cdrom_device_ops  *ops;  /* link to device_ops */
	struct cdrom_device_info *next; /* next device_info for this major */
	void *handle;		        /* driver-dependent data */
/* specifications */
        kdev_t dev;	                /* device number */
	int mask;                       /* mask of capability: disables them */
	int speed;			/* maximum speed for reading data */
	int capacity;			/* number of discs in jukebox */
/* device-related storage */
	int options : 30;               /* options flags */
	unsigned mc_flags : 2;          /* media change buffer flags */
    	int use_count;                  /* number of times device opened */
    	char name[20];                  /* name of the device type */

};

struct cdrom_device_ops {
/* routines */
	int (*open) (struct cdrom_device_info *, int);
	void (*release) (struct cdrom_device_info *);
	int (*drive_status) (struct cdrom_device_info *, int);
	int (*media_changed) (struct cdrom_device_info *, int);
	int (*tray_move) (struct cdrom_device_info *, int);
	int (*lock_door) (struct cdrom_device_info *, int);
	int (*select_speed) (struct cdrom_device_info *, int);
	int (*select_disc) (struct cdrom_device_info *, int);
	int (*get_last_session) (struct cdrom_device_info *,
				 struct cdrom_multisession *);
	int (*get_mcn) (struct cdrom_device_info *,
			struct cdrom_mcn *);
	/* hard reset device */
	int (*reset) (struct cdrom_device_info *);
	/* play stuff */
	int (*audio_ioctl) (struct cdrom_device_info *,unsigned int, void *);
	/* dev-specific */
 	int (*dev_ioctl) (struct cdrom_device_info *,
			  unsigned int, unsigned long);
/* driver specifications */
	const int capability;   /* capability flags */
	int n_minors;           /* number of active minor devices */
};

/* the general file operations structure: */
extern struct file_operations cdrom_fops;

extern int register_cdrom(struct cdrom_device_info *cdi);
extern int unregister_cdrom(struct cdrom_device_info *cdi);
typedef struct {
    int data;
    int audio;
    int cdi;
    int xa;
    long error;
} tracktype;
extern void cdrom_count_tracks(struct cdrom_device_info *cdi,tracktype* tracks);
#endif  /* End of kernel only stuff */ 

#endif  /* _LINUX_CDROM_H */
