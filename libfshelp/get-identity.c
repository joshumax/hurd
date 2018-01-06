/* Helper function for io_identity
   Copyright (C) 1996, 1999 Free Software Foundation, Inc.
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


#include <fshelp.h>
#include <hurd/ports.h>
#include <hurd/ihash.h>
#include <stddef.h>
#include <assert-backtrace.h>

static struct port_class *idclass = 0;
static pthread_mutex_t idlock = PTHREAD_MUTEX_INITIALIZER;

struct idspec
{
  struct port_info pi;
  hurd_ihash_locp_t id_hashloc;
  ino_t cache_id;
};

/* The size of ino_t is larger than hurd_ihash_key_t on 32 bit
   platforms.  We therefore have to use libihashs generalized key
   interface.  */

/* This is the mix function of fasthash, see
   https://code.google.com/p/fast-hash/ for reference.  */
#define mix_fasthash(h) ({              \
        (h) ^= (h) >> 23;               \
        (h) *= 0x2127599bf4325c37ULL;   \
        (h) ^= (h) >> 47; })

static hurd_ihash_key_t
hash (const void *key)
{
  ino_t i;
  i = *(ino_t *) key;
  mix_fasthash (i);
  return (hurd_ihash_key_t) i;
}

static int
compare (const void *a, const void *b)
{
  return *(ino_t *) a == *(ino_t *) b;
}

static struct hurd_ihash idhash
  = HURD_IHASH_INITIALIZER_GKI (offsetof (struct idspec, id_hashloc),
                                NULL, NULL, hash, compare);

static void
id_clean (void *cookie)
{
  struct idspec *i = cookie;
  pthread_mutex_lock (&idlock);
  if (refcounts_hard_references(&i->pi.refcounts) == 0
      && i->id_hashloc != NULL)
    {
      /* Nobody got a send right in between, we can remove i from the hash.  */
      hurd_ihash_locp_remove (&idhash, i->id_hashloc);
      i->id_hashloc = NULL;
      ports_port_deref_weak (&i->pi);
    }
  pthread_mutex_unlock (&idlock);
}

static void
id_initialize ()
{
  assert_backtrace (!idclass);
  idclass = ports_create_class (NULL, id_clean);
}

error_t
fshelp_get_identity (struct port_bucket *bucket,
		     ino_t fileno,
		     mach_port_t *pt)
{
  struct idspec *i;
  error_t err = 0;

  pthread_mutex_lock (&idlock);
  if (!idclass)
    id_initialize ();

  i = hurd_ihash_find (&idhash, (hurd_ihash_key_t) &fileno);
  if (i == NULL)
    {
      err = ports_create_port (idclass, bucket, sizeof (struct idspec), &i);
      if (err)
        goto lose;
      i->cache_id = fileno;
      err = hurd_ihash_add (&idhash, (hurd_ihash_key_t) &i->cache_id, i);
      if (err)
        goto lose_port;

      /* Weak reference for the hash entry.  */
      ports_port_ref_weak(&i->pi);

      *pt = ports_get_right (i);
      ports_port_deref (i);
    }
  else
    *pt = ports_get_right (i);

  /* Success!  */
  goto lose;

 lose_port:
  ports_destroy_right (i);
 lose:
  pthread_mutex_unlock (&idlock);
  return err;
}
