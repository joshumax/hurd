/* Data structures and global variables for NFS client
   Copyright (C) 1994, 1995, 1996 Free Software Foundation

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */


/* Needed for the rpcsvc include files to work. */
typedef int bool_t;		/* Ick. */

#include <sys/stat.h>
#include <sys/types.h>
#include <rpcsvc/nfs_prot.h>
#include <hurd/netfs.h>

/* One of these exists for private data needed by the client for each
   node. */
struct netnode 
{
  char handle[NFS2_FHSIZE];
  time_t stat_updated;
  struct node *hnext, **hprevp;

  /* These two fields handle translators set internally but
     unknown to the server. */
  enum
    {
      NOT_POSSIBLE,
      POSSIBLE,
      SYMLINK,
      CHRDEV,
      BLKDEV,
      FIFO,
      SOCK,
    } dtrans;
  union
    {
      char *name;
      dev_t indexes;
    } transarg;
  
#ifdef notyet
  /* This indicates that the length of the file must be at 
     least this big because we've written this much locally,
     even if the server thinks we haven't gone this far. */
  off_t extend_len;
#endif

  struct user_pager_info *fileinfo;

  /* If this node has been renamed by "deletion" then
     this is the directory and name in that directory which
     is holding the node */
  struct node *dead_dir;
  char *dead_name;
};

/* Credential structure to identify a particular user. */
struct netcred
{
  uid_t *uids, *gids;
  int nuids, ngids;
  int refcnt;
};

/* Socket file descriptor for talking to RPC servers. */
int main_udp_socket;

/* Our hostname */
char *hostname;

/* The current time */ 
volatile struct mapped_time_value *mapped_time;

/* Some tunable parameters */

/* How long to keep around stat information */
extern int stat_timeout;

/* How long to keep around file contents caches */
extern int cache_timeout;

/* How long to wait for replies before re-sending RPC's. */
extern int initial_transmit_timeout;
extern int max_transmit_timeout;

/* How many attempts to send before giving up on soft mounts */
extern int soft_retries;

/* Whether we are a hard or soft mount */
extern int mounted_soft;

/* Maximum amount to read at once */
extern int read_size;

/* Maximum amout to write at once */
extern int write_size;

/* Service name for portmapper */
extern char *pmap_service_name;

/* If pmap_service is null, then this is the port to use for the portmapper;
   if the lookup of pmap_service_name fails, use this number. */
extern short pmap_service_number;

/* RPC program for the mount agent */
extern int mount_program;

/* RPC program version for the mount agent */
extern int mount_version;

/* If this is nonzero, it's the port to use for the mount agent if 
   the portmapper fails or can't be found. */
extern short mount_port;

/* If this is nonzero use mount_port without attempting to contact
   the portmapper */
extern int mount_port_override;

/* RPC program for the NFS server */
extern int nfs_program;

/* RPC program version for the NFS server */
extern int nfs_version;

/* If this is nonzero, it's the port to be used to find the nfs agent 
   if the portmapper fails or can't be found */
extern short nfs_port;

/* If this is nonzero use nfs_port without attepting to contact the
   portmapper. */
extern int nfs_port_override;

/* Which NFS protocol version we are using */
extern int protocol_version;


/* Count how many four-byte chunks it takss to hold LEN bytes. */
#define INTSIZE(len) (((len)+3)>>2)


/* cred.c */
int cred_has_uid (struct netcred *, uid_t);
int cred_has_gid (struct netcred *, gid_t);

/* nfs.c */
int *xdr_encode_fhandle (int *, void *);
int *xdr_encode_data (int *, char *, size_t);
int *xdr_encode_string (int *, char *);
int *xdr_encode_sattr_mode (int *, mode_t);
int *xdr_encode_sattr_ids (int *, u_int, u_int);
int *xdr_encode_sattr_size (int *, off_t);
int *xdr_encode_sattr_times (int *, struct timespec *, struct timespec *);
int *xdr_encode_sattr_stat (int *, struct stat *);
int *xdr_encode_create_state (int *, mode_t);
int *xdr_decode_fattr (int *, struct stat *);
int *xdr_decode_string (int *, char *);
int *nfs_initialize_rpc (int, struct netcred *, size_t, void **, 
			 struct node *, uid_t);
error_t nfs_error_trans (int);

/* mount.c */
struct node *mount_root (char *, char *);

/* ops.c */
int *register_fresh_stat (struct node *, int *);

/* rpc.c */
int *initialize_rpc (int, int, int, size_t, void **, uid_t, gid_t, gid_t);
error_t conduct_rpc (void **, int **);
void timeout_service_thread (void);
void rpc_receive_thread (void);

/* cache.c */
struct node *lookup_fhandle (void *);
void recache_handle (struct node *, void *);
