#ifndef _LINUX_MSG_H
#define _LINUX_MSG_H

#include <linux/ipc.h>

/* ipcs ctl commands */
#define MSG_STAT 11
#define MSG_INFO 12

/* msgrcv options */
#define MSG_NOERROR     010000  /* no error if message is too big */
#define MSG_EXCEPT      020000  /* recv any msg except of specified type.*/

/* one msqid structure for each queue on the system */
struct msqid_ds {
	struct ipc_perm msg_perm;
	struct msg *msg_first;		/* first message on queue */
	struct msg *msg_last;		/* last message in queue */
	__kernel_time_t msg_stime;	/* last msgsnd time */
	__kernel_time_t msg_rtime;	/* last msgrcv time */
	__kernel_time_t msg_ctime;	/* last change time */
	struct wait_queue *wwait;
	struct wait_queue *rwait;
	unsigned short msg_cbytes;	/* current number of bytes on queue */
	unsigned short msg_qnum;	/* number of messages in queue */
	unsigned short msg_qbytes;	/* max number of bytes on queue */
	__kernel_ipc_pid_t msg_lspid;	/* pid of last msgsnd */
	__kernel_ipc_pid_t msg_lrpid;	/* last receive pid */
};

/* message buffer for msgsnd and msgrcv calls */
struct msgbuf {
	long mtype;         /* type of message */
	char mtext[1];      /* message text */
};

/* buffer for msgctl calls IPC_INFO, MSG_INFO */
struct msginfo {
	int msgpool;
	int msgmap; 
	int msgmax; 
	int msgmnb; 
	int msgmni; 
	int msgssz; 
	int msgtql; 
	unsigned short  msgseg; 
};

#define MSGMNI   128   /* <= 1K */     /* max # of msg queue identifiers */
#define MSGMAX  4056   /* <= 4056 */   /* max size of message (bytes) */
#define MSGMNB 16384   /* ? */        /* default max size of a message queue */
#define MSGQNUM 1024   /* <=65535 */	  /* Max messages in flight / queue */

/* unused */
#define MSGPOOL (MSGMNI*MSGMNB/1024)  /* size in kilobytes of message pool */
#define MSGTQL  MSGMNB            /* number of system message headers */
#define MSGMAP  MSGMNB            /* number of entries in message map */
#define MSGSSZ  16                /* message segment size */
#define __MSGSEG ((MSGPOOL*1024)/ MSGSSZ) /* max no. of segments */
#define MSGSEG (__MSGSEG <= 0xffff ? __MSGSEG : 0xffff)

#ifdef __KERNEL__

/* one msg structure for each message */
struct msg {
	struct msg *msg_next;   /* next message on queue */
	long  msg_type;          
	char *msg_spot;         /* message text address */
	time_t msg_stime;       /* msgsnd time */
	short msg_ts;           /* message text size */
};

asmlinkage int sys_msgget (key_t key, int msgflg);
asmlinkage int sys_msgsnd (int msqid, struct msgbuf *msgp, size_t msgsz, int msgflg);
asmlinkage int sys_msgrcv (int msqid, struct msgbuf *msgp, size_t msgsz, long msgtyp,
		       int msgflg);
asmlinkage int sys_msgctl (int msqid, int cmd, struct msqid_ds *buf);

#endif /* __KERNEL__ */

#endif /* _LINUX_MSG_H */
