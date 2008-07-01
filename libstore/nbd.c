/* "Network Block Device" store backend compatible with Linux `nbd' driver
   Copyright (C) 2001, 2002, 2008 Free Software Foundation, Inc.

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

#include "store.h"
#include <hurd.h>
#include <hurd/io.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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

#define NBD_IO_MAX		10240

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

#if BYTE_ORDER == BIG_ENDIAN
# define htonll(x)	(x)
#elif BYTE_ORDER == LITTLE_ENDIAN
# include <byteswap.h>
# define htonll(x)	(bswap_64 (x))
#else
# error what endian?
#endif
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
  };
  error_t err;
  mach_msg_type_number_t cc;

  addr <<= store->log2_block_size;
  *amount = 0;

  do
    {
      size_t chunk = len < NBD_IO_MAX ? len : NBD_IO_MAX, nwrote;
      req.from = htonll (addr);
      req.len = htonl (chunk);

      err = io_write (store->port, (char *) &req, sizeof req, -1, &cc);
      if (err)
	return err;
      if (cc != sizeof req)
	return EIO;

      nwrote = 0;
      do
	{
	  err = io_write (store->port, (char *) buf, chunk - nwrote, -1, &cc);
	  if (err)
	    return err;
	  buf += cc;
	  nwrote += cc;
	} while (nwrote < chunk);

      err = read_reply (store, req.handle);
      if (err)
	return err;

      addr += chunk;
      *amount += chunk;
      len -= chunk;
    } while (len > 0);

  return 0;
}

static error_t
nbd_read (struct store *store,
	  store_offset_t addr, size_t index, size_t amount,
	  void **buf, size_t *len)
{
  struct nbd_request req =
  {
    magic: NBD_REQUEST_MAGIC,
    type: htonl (0),		/* READ */
  };
  error_t err;
  size_t ofs, chunk;
  char *databuf, *piecebuf;
  size_t databuflen, piecelen;

  /* Send a request for the largest possible piece of remaining data and
     read the first piece of its reply into PIECEBUF, PIECELEN.  The amount
     requested can be found in CHUNK.  */
  inline error_t request_chunk (char **buf, size_t *len)
    {
      mach_msg_type_number_t cc;

      chunk = (amount - ofs) < NBD_IO_MAX ? (amount - ofs) : NBD_IO_MAX;

      req.from = htonll (addr);
      req.len = htonl (chunk);

      /* Advance ADDR immediately, so it always points past what we've
	 already requested.  */
      addr += chunk;

      return (io_write (store->port, (char *) &req, sizeof req, -1, &cc) ?
	      : cc != sizeof req ? EIO
	      : read_reply (store, req.handle) ?
	      : io_read (store->port, buf, len, (off_t) -1, chunk));
    }

  addr <<= store->log2_block_size;

  /* Read the first piece, which can go directly into the caller's buffer.  */
  databuf = *buf;
  piecelen = databuflen = *len;
  err = request_chunk (&databuf, &piecelen);
  if (err)
    return err;
  if (databuflen >= amount)
    {
      /* That got it all.  We're done.  */
      *buf = databuf;
      *len = piecelen;
      return 0;
    }

  /* We haven't read the entire amount yet.  */
  ofs = 0;
  do
    {
      /* Account for what we just read.  */
      ofs += piecelen;
      chunk -= piecelen;
      if (ofs == amount)
	{
	  /* That got it all.  We're done.  */
	  *buf = databuf;
	  *len = ofs;
	  return 0;
	}

      /* Now we'll read another piece of the data, hopefully
	 into the latter part of the existing buffer.  */
      piecebuf = databuf + ofs;
      piecelen = databuflen - ofs;

      if (chunk > 0)
	/* We haven't finishing reading the last chunk we requested.  */
	err = io_read (store->port, &piecebuf, &piecelen,
		       (off_t) -1, chunk);
      else
	/* Request the next chunk from the server.  */
	err = request_chunk (&piecebuf, &piecelen);

      if (!err && piecebuf != databuf + ofs)
	{
	  /* Now we have two discontiguous pieces of the buffer.  */
	  size_t newbuflen = round_page (databuflen + piecelen);
	  char *newbuf = mmap (0, newbuflen,
			       PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  if (newbuf == MAP_FAILED)
	    {
	      err = errno;
	      break;
	    }
	  memcpy (newbuf, databuf, ofs);
	  memcpy (newbuf + ofs, piecebuf, piecelen);
	  if (databuf != *buf)
	    munmap (databuf, databuflen);
	  databuf = newbuf;
	  databuflen = newbuflen;
	}
    } while (! err);

  if (databuf != *buf)
    munmap (databuf, databuflen);
  return err;
}

static error_t
nbd_set_size (struct store *store, size_t newsize)
{
  return EOPNOTSUPP;
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

static const char url_prefix[] = "nbd://";

/* Valid name syntax is [nbd://]HOSTNAME:PORT[/BLOCKSIZE].
   If "/BLOCKSIZE" is omitted, the block size is 1.  */
static error_t
nbd_validate_name (const char *name,
		   const struct store_class *const *classes)
{
  char *p, *endp;

  if (!strncmp (name, url_prefix, sizeof url_prefix - 1))
    name += sizeof url_prefix - 1;

  p = strchr (name, ':');
  if (p == 0)
    return EINVAL;
  endp = 0;
  strtoul (++p, &endp, 0);
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
  size_t ofs;
  unsigned long int port;
  char *hostname, *p, *endp;

  if (!strncmp (name, url_prefix, sizeof url_prefix - 1))
    name += sizeof url_prefix - 1;

  /* First we have to parse the store name to get the host name and TCP
     port number to connect to and the block size to use.  */

  hostname = strdupa (name);
  p = strchr (hostname, ':');

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
      errno = 0;
      if (connect (sock, &sin, sizeof sin) == 0 || errno == ECONNREFUSED)
	break;
    }
  if (errno != 0)		/* last connect failed */
    {
      error_t err = errno;
      close (sock);
      return err;
    }

  /* Read the startup packet, which tells us the size of the store.  */
  ofs = 0;
  do {
    cc = read (sock, (char *) &ns + ofs, sizeof ns - ofs);
    if (cc < 0)
      {
	error_t err = errno;
	close (sock);
	return err;
      }
    ofs += cc;
  } while (cc > 0 && ofs < sizeof ns);

  if (ofs != sizeof ns
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

const struct store_class store_nbd_class =
{
  STORAGE_NETWORK, "nbd",
  open: nbd_open,
  validate_name: nbd_validate_name,
  read: nbd_read,
  write: nbd_write,
  set_size: nbd_set_size,
  allocate_encoding: store_std_leaf_allocate_encoding,
  encode: store_std_leaf_encode,
  decode: nbd_decode,
  set_flags: nbd_set_flags, clear_flags: nbd_clear_flags,
};
STORE_STD_CLASS (nbd);

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
	  if (!strncmp (name, url_prefix, sizeof url_prefix - 1))
	    err = store_set_name (*store, name);
	  else
	    asprintf (&(*store)->name, "%s%s", url_prefix, name);
	  if (err)
	    store_free (*store);
	}
      if (err)
	mach_port_deallocate (mach_task_self (), sock);
    }
  return err;
}
