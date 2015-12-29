/*
   Copyright (C) 1996,98,2002 Free Software Foundation, Inc.
   Written by Michael I. Bushnell, p/BSG.

   This file is part of the GNU Hurd.

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA. */

#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <rpc/types.h>
#include "../nfs/nfs-spec.h" /* XXX */
#include <hurd/fs.h>

/* These should be configuration options */
#define ID_KEEP_TIMEOUT 3600	/* one hour */
#define FH_KEEP_TIMEOUT 600	/* ten minutes */
#define REPLY_KEEP_TIMEOUT 120	/* two minutes */
#define MAXIOSIZE 10240

struct idspec
{
  struct idspec *next, **prevp;
  int nuids, ngids;
  uid_t *uids, *gids;
  time_t lastuse;
  int references;
};

union cache_handle_array {
  char array[NFS2_FHSIZE];
  int fs;
};

struct cache_handle
{
  struct cache_handle *next, **prevp;
  union cache_handle_array handle;
  struct idspec *ids;
  file_t port;
  time_t lastuse;
  int references;
};

struct cached_reply
{
  struct cached_reply *next, **prevp;
  pthread_mutex_t lock;
  struct sockaddr_in source;
  int xid;
  time_t lastuse;
  int references;
  size_t len;
  char *data;
};

struct procedure
{
  error_t (*func) (struct cache_handle *, int *, int **, int);
  size_t (*alloc_reply) (int *, int);
  int need_handle;
  int process_error;
};

struct proctable
{
  int min;
  int max;
  struct procedure procs[];
};

volatile struct mapped_time_value *mapped_time;

#define INTSIZE(n) (((n) + 3) >> 2)

/* We don't actually distinguish between these two sockets, but
   we have to listen on two different ports, so that's why they're here. */
extern int main_udp_socket, pmap_udp_socket;
extern struct sockaddr_in main_address, pmap_address;

/* Name of the file on disk containing the filesystem index table */
extern char *index_file_name;

/* Our auth server */
auth_t authserver;


/* cache.c */
int *process_cred (int *, struct idspec **);
void cred_rele (struct idspec *);
void cred_ref (struct idspec *);
void scan_creds (void);
int *lookup_cache_handle (int *, struct cache_handle **, struct idspec *);
void cache_handle_rele (struct cache_handle *);
void scan_fhs (void);
struct cache_handle *create_cached_handle (int, struct cache_handle *, file_t);
struct cached_reply *check_cached_replies (int, struct sockaddr_in *);
void release_cached_reply (struct cached_reply *cr);
void scan_replies (void);

/* loop.c */
void * server_loop (void *);

/* ops.c */
extern struct proctable nfs2table, mounttable, pmaptable;

/* xdr.c */
int nfs_error_trans (error_t, int);
int *encode_fattr (int *, struct stat *, int version);
int *decode_name (int *, char **);
int *encode_fhandle (int *, char *);
int *encode_string (int *, char *);
int *encode_data (int *, char *, size_t);
int *encode_statfs (int *, struct statfs *);

/* fsys.c */
fsys_t lookup_filesystem (int);
int enter_filesystem (char *, file_t);
void init_filesystems (void);
