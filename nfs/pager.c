/* 
   Copyright (C) 1995, 1996 Free Software Foundation, Inc.
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


#include "nfs.h"
#include <unistd.h>
#include <hurd/pager.h>
#include <netinet/in.h>
#include <string.h>

struct user_pager_info
{
  struct node *np;
  struct pager *p;
  int max_prot;
};

struct pager_cache_rec 
{
  struct pager_cache_rec *next;
  vm_offset_t offset;
  struct pager *p;
  time_t fetched;
};

static struct pager_cache_rec *pager_cache_recs;
static spin_lock_t pager_cache_rec_lock = SPIN_LOCK_INITIALIZER;
static spin_lock_t node2pagelock = SPIN_LOCK_INITIALIZER;
static struct port_bucket *pager_bucket;

void
register_new_page (struct pager *p, vm_offset_t offset)
{
  struct pager_cache_rec *pc;
  
  pc = malloc (sizeof (struct pager_cache_rec));
  pc->offset = offset;
  pc->p = p;
  ports_port_ref (p);
  pc->fetched = mapped_time->seconds;
  
  spin_lock (&pager_cache_rec_lock);
  pc->next = pager_cache_recs;
  pager_cache_recs = pc;
  spin_unlock (&pager_cache_rec_lock);
}

any_t
flush_pager_cache_thread (any_t foo2)
{
  struct pager_cache_rec *pc, *next, **ppc, *list;

  for (;;)
    {
      sleep (1);

      /* Dequeue from the main list and queue locally the recs
	 for expired pages. */
      list = 0;
      spin_lock (&pager_cache_rec_lock);
      for (pc = pager_cache_recs, ppc = &pager_cache_recs;
	   pc; 
	   ppc = &pc->next, pc = next)
	{
	  next = pc->next;
	  if (mapped_time->seconds - pc->fetched > cache_timeout)
	    {
	      *ppc = pc->next;
	      pc->next = list;
	      list = pc;
	    }
	}
      spin_unlock (&pager_cache_rec_lock);
      
      /* And now, one at a time, expire them */
      for (pc = list; pc; pc = next)
	{
	  pager_return_some (pc->p, pc->offset, vm_page_size, 0);
	  next = pc->next;
	  ports_port_deref (pc->p);
	  free (pc);
	}
    }
}	  

error_t
pager_read_page (struct user_pager_info *pager,
		 vm_offset_t page,
		 vm_address_t *buf,
		 int *writelock)
{
  error_t err;
  int *p;
  void *rpcbuf;
  struct node *np;
  size_t amt, thisamt, trans_len;
  void *data;
  off_t offset;

  np = pager->np;

  mutex_lock (&np->lock);

  vm_allocate (mach_task_self (), buf, vm_page_size, 1);
  data = (char *) *buf;
  amt = vm_page_size;
  offset = page;

  while (amt)
    {
      thisamt = amt;
      if (thisamt > read_size)
	thisamt = read_size;

      p = nfs_initialize_rpc (NFSPROC_READ, (struct netcred *)-1, 0, 
			      &rpcbuf, np, -1);
      p = xdr_encode_fhandle (p, &np->nn->handle);
      *p++ = htonl (offset);
      *p++ = htonl (vm_page_size);
      *p++ = 0;
  
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	err = nfs_error_trans (ntohl (*p++));
      if (err)
	{
	  mutex_unlock (&np->lock);
	  free (rpcbuf);
	  vm_deallocate (mach_task_self (), *buf, vm_page_size);
	  return err;
	}
  
      p = register_fresh_stat (np, p);
      trans_len = ntohl (*p++);
      if (trans_len > thisamt)
	trans_len = thisamt;	/* ??? */

      bcopy (p, data, trans_len);
      
      free (rpcbuf);

      data += trans_len;
      offset += trans_len;
      amt -= trans_len;
      
      /* If we got a short count, we're all done. */
      if (trans_len < thisamt)
	break;
    }

  register_new_page (pager->p, page);
  mutex_unlock (&np->lock);
  return 0;
}

	  
error_t
pager_write_page (struct user_pager_info *pager,
		  vm_offset_t page,
		  vm_address_t buf)
{
  int *p;
  void *rpcbuf;
  error_t err;
  size_t amt, thisamt;
  off_t offset;
  struct node *np;
  void *data;

  np = pager->np;
  mutex_lock (&np->lock);
  
  amt = vm_page_size;
  offset = page;
  data = (void *) buf;
  
  while (amt)
    {
      thisamt = amt;
      if (amt > write_size)
	amt = write_size;
      
      p = nfs_initialize_rpc (NFSPROC_WRITE, (struct netcred *) -1, 
			      amt, &rpcbuf, np, -1);
      p = xdr_encode_fhandle (p, &np->nn->handle);
      *p++ = 0;
      *p++ = htonl (offset);
      *p++ = 0;
      p = xdr_encode_data (p, data, thisamt);
      
      err = conduct_rpc (&rpcbuf, &p);
      if (!err)
	err = nfs_error_trans (ntohl (*p++));
      if (err)
	{
	  free (rpcbuf);
	  vm_deallocate (mach_task_self (), buf, vm_page_size);
	  return err;
	}
      register_fresh_stat (np, p);
      free (rpcbuf);
      amt -= thisamt;
      data += thisamt;
      offset += thisamt;
    }

  vm_deallocate (mach_task_self (), buf, vm_page_size);
  mutex_unlock (&np->lock);
  return 0;
}

error_t
pager_unlock_page (struct user_pager_info *pager,
		   vm_offset_t address)
{
  abort ();
}

error_t
pager_report_extent (struct user_pager_info *pager,
		     vm_address_t *offset,
		     vm_size_t *size)
{
  struct node *np;
  error_t err;

  np = pager->np;
  mutex_lock (&np->lock);

  err = netfs_validate_stat (np, 0);
  if (!err)
    *size = round_page (np->nn_stat.st_size);
  mutex_unlock (&np->lock);
  return err;
}

void
pager_clear_user_data (struct user_pager_info *upi)
{
  spin_lock (&node2pagelock);
  if (upi->np->nn->fileinfo == upi)
    upi->np->nn->fileinfo = 0;
  spin_unlock (&node2pagelock);
  netfs_nrele (upi->np);
  free (upi);
}

void
pager_dropweak (struct user_pager_info *upi)
{
  abort ();
}

mach_port_t
netfs_get_filemap (struct node *np, vm_prot_t prot)
{
  struct user_pager_info *upi;
  mach_port_t right;
  
  spin_lock (&node2pagelock);
  do 
    if (!np->nn->fileinfo)
      {
	upi = malloc (sizeof (struct user_pager_info));
	upi->np = np;
	netfs_nref (np);
	upi->max_prot = prot;
	upi->p = pager_create (upi, pager_bucket, 1, MEMORY_OBJECT_COPY_NONE);
	np->nn->fileinfo = upi;
	right = pager_get_port (np->nn->fileinfo->p);
	ports_port_deref (np->nn->fileinfo->p);
      }
    else
      {
	np->nn->fileinfo->max_prot |= prot;
	/* Because NP->dn->fileinfo->p is not a real reference,
	   this might be nearly deallocated.  If that's so, then
	   the port right will be null.  In that case, clear here
	   and loop.  The deallocation will complete separately. */
	right = pager_get_port (np->nn->fileinfo->p);
	if (right == MACH_PORT_NULL)
	  np->nn->fileinfo = 0;
      }
  while (right == MACH_PORT_NULL);
  
  spin_unlock (&node2pagelock);

  mach_port_insert_right (mach_task_self (), right, right,
			  MACH_MSG_TYPE_MAKE_SEND);
  return right;
}

void
drop_pager_softrefs (struct node *np)
{
  struct user_pager_info *upi;
  
  spin_lock (&node2pagelock);
  upi = np->nn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  spin_unlock (&node2pagelock);
  
  if (upi)
    {
      pager_change_attributes (upi->p, 0, MEMORY_OBJECT_COPY_NONE, 0);
      ports_port_deref (upi->p);
    }
}

void
allow_pager_softrefs (struct node *np)
{
  struct user_pager_info *upi;
  
  spin_lock (&node2pagelock);
  upi = np->nn->fileinfo;
  if (upi)
    ports_port_ref (upi->p);
  spin_unlock (&node2pagelock);
  
  if (upi)
    {
      pager_change_attributes (upi->p, 1, MEMORY_OBJECT_COPY_NONE, 0);
      ports_port_deref (upi->p);
    }
}

void
block_caching ()
{
  error_t block_cache (void *arg)
    {
      struct pager *p = arg;
      pager_change_attributes (p, 0, MEMORY_OBJECT_COPY_NONE, 1);
      return 0;
    }
  ports_bucket_iterate (pager_bucket, block_cache);
}

void
enable_caching ()
{
  error_t enable_cache (void *arg)
    {
      struct pager *p = arg;
      struct user_pager_info *upi = pager_get_upi (p);

      pager_change_attributes (p, 1, MEMORY_OBJECT_COPY_NONE, 0);
      return 0;
    }
  
  ports_bucket_iterate (pager_bucket, enable_cache);
}

int
netfs_pager_users ()
{
  int npagers = ports_count_bucket (pager_bucket);
  
  if (!npagers)
    return 0;
  
  block_caching ();
  /* Give it a sec; the kernel doesn't issue the shutdown right away */
  sleep (1);
  npagers = ports_count_bucket (pager_bucket);
  if (!npagers)
    return 0;
  
  enable_caching ();

  ports_enable_bucket (pager_bucket);
}

vm_prot_t
netfs_max_user_pager_prot ()
{
  vm_prot_t max_prot;
  int npagers = ports_count_bucket (pager_bucket);
  
  if (npagers)
    {
      error_t add_pager_max_prot (void *v_p)
	{
	  struct pager *p = v_p;
	  struct user_pager_info *upi = pager_get_upi (p);
	  max_prot |= upi->max_prot;
	  return max_prot == (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXECUTE);
	}
      
      block_caching ();
      sleep (1);
      
      ports_bucket_iterate (pager_bucket, add_pager_max_prot);
      enable_caching ();
    }
  
  ports_enable_bucket (pager_bucket);
  return max_prot;
}

void
netfs_shutdown_pager ()
{
  error_t shutdown_one (void *arg)
    {
      pager_shutdown ((struct pager *) arg);
      return 0;
    }
  
  ports_bucket_iterate (pager_bucket, shutdown_one);
}

void
netfs_sync_everything (int wait)
{
  error_t sync_one (void *arg)
    {
      pager_sync ((struct pager *) arg, wait);
      return 0;
    }
  ports_bucket_iterate (pager_bucket, sync_one);
}

void
pager_initialize (void)
{
  pager_bucket = ports_create_bucket ();
  cthread_detach (cthread_fork (flush_pager_cache_thread, 0));
  
