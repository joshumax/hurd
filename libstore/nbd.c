/* "Network Block Device" store backend compatible with Linux `nbd' driver
   Copyright (C) 2001 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA. */

#include <hurd.h>
#include <hurd/store.h>
#include <hurd/io.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>


// Avoid dragging in the resolver when linking statically.
#pragma weak gethostbyname


/* The nbd protocol, such as it is, is not really specified anywhere.
   These message layouts and constants were culled from the nbd-server and
   Linux kernel nbd module sources.  */

#define NBD_INIT_MAGIC		"NBDMAGIC\x00\x00\x42\x02\x81\x86\x12\x53"

#define NBD_REQUEST_MAGIC	(htonl (0x25609513))
#define NBD_REPLY_MAGIC		(htonl (0x67446698))

struct nbd_startup
{
  char magic[16];		/* NBD_INIT_MAGIC */
  uint64_t size;		/* size in bytes, 64 bits in net order */
  char reserved[128];		/* zeros, we don't check it */
};

struct nbd_request
{
  uint32_t magic;		/* NBD_REQUEST_MAGIC */
  uint32_t type;		/* 0 read, 1 write, 2 disconnect */
  uint64_t handle;		/* returned in reply */
  uint64_t from;
  uint32_t len;
} __attribute__ ((packed));

struct nbd_reply
{
  uint32_t magic;		/* NBD_REPLY_MAGIC */
  uint32_t error;
  uint64_t handle;		/* value from request */
} __attribute__ ((packed));


/* i/o functions.  */

static inline uint64_t
htonll (uint64_t x)
{
#if BYTE_ORDER == BIG_ENDIAN
  return x;
#elif BYTE_ORDER == LITTLE_ENDIAN
  union { uint64_t ll; uint32_t l[2]; } u;
  u.l[0] = htonl ((uint32_t) (x >> 32));
  u.l[1] = htonl ((uint32_t) x);
  return u.ll;
#else
# error what endian?
#endif
}
#define ntohll htonll


static inline error_t
read_reply (struct store *store, uint64_t handle)
{
  struct nbd_reply reply;
  char *buf = (void *) &reply;
  mach_msg_type_number_t cc = sizeof reply;
  error_t err = io_read (store->port, &buf, &cc, -1, cc);
  if (err)
    return err;
  if (buf != (void *) &reply)
    {
      memcpy (&reply, buf, sizeof reply);
      munmap (buf, cc);
    }
  if (cc != sizeof reply)
    return EIO;
  if (reply.magic != NBD_REPLY_MAGIC)
    return EIO;
  if (reply.handle != handle)
    return EIO;
  if (reply.error != 0)
    return EIO;
  return 0;
}

static error_t
nbd_write (struct store *store,
	   store_offset_t addr, size_t index, const void *buf, size_t len,
	   size_t *amount)
{
  struct nbd_request req =
  {
    magic: NBD_REQUEST_MAGIC,
    type: htonl (1),		/* WRITE */
    from: htonll (addr << store->log2_block_size),
    len: htonl (len)
  };
  error_t err;
  mach_msg_type_number_t cc;

  err = io_write (store->port, (char *) &req, sizeof req, -1, &cc);
  if (err)
    return err;
  if (cc != sizeof req)
    return EIO;

  *amount = 0;
  do
    {
      err = io_write (store->port, (char *) buf, len - *amount, -1, &cc);
      if (err)
	return err;
      buf += cc;
      *amount += cc;
    } while (*amount < len);

  return read_reply (store, req.handle);
}

static error_t
nbd_read (struct store *store,
	  store_offset_t addr, size_t index, size_t amount, void **buf,
	  size_t *len)
{
  struct nbd_request req =
  {
    magic: NBD_REQUEST_MAGIC,
    type: htonl (0),		/* READ */
    from: htonll (addr << store->log2_block_size),
    len: htonl (amount),
  };
  error_t err;
  mach_msg_type_number_t cc;

  err = io_write (store->port, (char *) &req, sizeof req, -1, &cc);
  if (err)
    return err;
  if (cc != sizeof req)
    return EIO;

  err = read_reply (store, req.handle);
  if (err == 0)
    err = io_read (store->port, (char **) buf, len, (off_t) -1, amount);
  return err;
}



/* Setup hooks.  */

static error_t
nbd_decode (struct store_enc *enc, const struct store_class *const *classes,
	    struct store **store)
{
  return store_std_leaf_decode (enc, _store_nbd_create, store);
}

static error_t
nbd_open (const char *name, int flags,
	  const struct store_class *const *classes,
	  struct store **store)
{
  return store_nbd_open (name, flags, store);
}

/* Valid name syntax is HOSTNAME:PORT[/BLOCKSIZE].
   If "/BLOCKSIZE" is omitted, the block size is 1.  */
static error_t
nbd_validate_name (const char *name,
		   const struct store_class *const *classes)
{
  char *p = strchr (name, ':'), *endp;
  if (p == 0)
    return EINVAL;
  endp = 0;
  strtoul (p, &endp, 0);
  if (endp == 0 || endp == p)
    return EINVAL;
  switch (*endp)
    {
    default:
      return EINVAL;
    case '\0':
      break;
    case '/':
      p = endp + 1;
      strtoul (p, &endp, 0);
      if (endp == 0 || endp == p)
	return EINVAL;
      if (*endp != '\0')
	return EINVAL;
    }
  return 0;
}

static error_t
nbdopen (const char *name, int *mod_flags,
	 socket_t *sockport, size_t *blocksize, store_offset_t *size)
{
  int sock;
  struct sockaddr_in sin;
  const struct hostent *he;
  char **ap;
  struct nbd_startup ns;
  ssize_t cc;

  /* First we have to parse the store name to get the host name and TCP
     port number to connect to and the block size to use.  */

  unsigned long int port;
  char *hostname = strdupa (name);
  char *p = strchr (hostname, ':'), *endp;

  if (p == 0)
    return EINVAL;
  *p++ = '\0';
  port = strtoul (p, &endp, 0);
  if (endp == 0 || endp == p || port > 0xffffUL)
    return EINVAL;
  switch (*endp)
    {
    default:
      return EINVAL;
    case '\0':
      *blocksize = 1;
      break;
    case '/':
      p = endp + 1;
      *blocksize = strtoul (p, &endp, 0);
      if (endp == 0 || endp == p)
	return EINVAL;
      if (*endp != '\0')
	return EINVAL;
    }

  /* Now look up the host name and get a TCP connection.  */

  he = gethostbyname (hostname);
  if (he == 0)			/* XXX emit an error message? */
    return errno;		/* XXX what value will this have? */

  sock = socket (PF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return errno;

  sin.sin_family = AF_INET;
  sin.sin_port = htons (port);
  for (ap = he->h_addr_list; *ap != 0; ++ap)
    {
      sin.sin_addr = *(const struct in_addr *) *ap;
      if (connect (sock, &sin, sizeof sin) == 0 || errno == ECONNREFUSED)
	break;
    }
  if (*ap != 0)			/* last connect failed */
    {
      error_t err = errno;
      close (sock);
      return err;
    }

  /* Read the startup packet, which tells us the size of the store.  */

  cc = read (sock, &ns, sizeof ns);
  if (cc < 0)
    {
      error_t err = errno;
      close (sock);
      return err;
    }
  if (cc != sizeof ns
      || memcmp (ns.magic, NBD_INIT_MAGIC, sizeof ns.magic) != 0)
    {
      close (sock);
      return EGRATUITOUS;	/* ? */
    }

  *size = ntohll (ns.size);
  *sockport = getdport (sock);
  close (sock);

  return 0;
}

static void
nbdclose (struct store *store)
{
  if (store->port != MACH_PORT_NULL)
    {
      /* Send a disconnect message, but don't wait for a reply.  */
      struct nbd_request req =
      {
	magic: NBD_REQUEST_MAGIC,
	type: htonl (2),	/* disconnect */
      };
      mach_msg_type_number_t cc;
      (void) io_write (store->port, (char *) &req, sizeof req, -1, &cc);

      /* Close the socket.  */
      mach_port_deallocate (mach_task_self (), store->port);
      store->port = MACH_PORT_NULL;
    }
}

static error_t
nbd_set_flags (struct store *store, int flags)
{
  if ((flags & ~STORE_INACTIVE) != 0)
    /* Trying to set flags we don't support.  */
    return EINVAL;

  nbdclose (store);
  store->flags |= STORE_INACTIVE;

  return 0;
}

static error_t
nbd_clear_flags (struct store *store, int flags)
{
  error_t err = 0;
  if ((flags & ~STORE_INACTIVE) != 0)
    err = EINVAL;
  err = store->name
    ? nbdopen (store->name, &store->flags,
	       &store->port, &store->block_size, &store->size)
    : ENOENT;
  if (! err)
    store->flags &= ~STORE_INACTIVE;
  return err;
}

struct store_class store_nbd_class =
{
  STORAGE_NETWORK, "nbd",
  open: nbd_open,
  validate_name: nbd_validate_name,
  read: nbd_read,
  write: nbd_write,
  allocate_encoding: store_std_leaf_allocate_encoding,
  encode: store_std_leaf_encode,
  decode: nbd_decode,
  set_flags: nbd_set_flags, clear_flags: nbd_clear_flags,
};

/* Create a store from an existing socket to an nbd server.
   The initial handshake has already been done.  */
error_t
_store_nbd_create (mach_port_t port, int flags, size_t block_size,
		   const struct store_run *runs, size_t num_runs,
		   struct store **store)
{
  return _store_create (&store_nbd_class,
			port, flags, block_size, runs, num_runs, 0, store);
}

/* Open a new store backed by the named nbd server.  */
error_t
store_nbd_open (const char *name, int flags, struct store **store)
{
  error_t err;
  socket_t sock;
  struct store_run run;
  size_t blocksize;

  run.start = 0;
  err = nbdopen (name, &flags, &sock, &blocksize, &run.length);
  if (!err)
    {
      run.length /= blocksize;
      err = _store_nbd_create (sock, flags, blocksize, &run, 1, store);
      if (! err)
	{
	  err = store_set_name (*store, name);
	  if (err)
	    store_free (*store);
	}
      if (err)
	mach_port_deallocate (mach_task_self (), sock);
    }
  return err;
}
