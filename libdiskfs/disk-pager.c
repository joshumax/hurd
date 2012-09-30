/* Map the disk image and handle faults accessing it.
   Copyright (C) 1996,97,99,2001,02 Free Software Foundation, Inc.
   Written by Roland McGrath.

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

#include "priv.h"
#include "diskfs-pager.h"
#include <hurd/sigpreempt.h>
#include <error.h>

struct pager *diskfs_disk_pager;

static void fault_handler (int sig, long int sigcode, struct sigcontext *scp);
static struct hurd_signal_preemptor preemptor =
  {
  signals: sigmask (SIGSEGV) | sigmask (SIGBUS),
  preemptor: NULL,
  handler: (sighandler_t) &fault_handler,
  };

/* A top-level function for the paging thread that just services paging
   requests.  */
static void
service_paging_requests (any_t arg)
{
  struct port_bucket *pager_bucket = arg;
  for (;;)
    ports_manage_port_operations_multithread (pager_bucket,
					      pager_demuxer,
					      1000 * 60 * 2,
					      1000 * 60 * 10, 0);
}

void
diskfs_start_disk_pager (struct pager_ops *ops, size_t upi_size,
			 struct port_bucket *pager_bucket, int may_cache,
			 size_t size, void **image)
{
  error_t err;
  mach_port_t disk_pager_port;

  /* Make a thread to service paging requests.  */
  cthread_detach (cthread_fork ((cthread_fn_t) service_paging_requests,
				(any_t)pager_bucket));

  /* Create the pager.  */
  diskfs_disk_pager = pager_create (ops, upi_size, pager_bucket,
				    may_cache, MEMORY_OBJECT_COPY_NONE);
  assert (diskfs_disk_pager);

  /* Get a port to the disk pager.  */
  disk_pager_port = pager_get_port (diskfs_disk_pager);
  mach_port_insert_right (mach_task_self (), disk_pager_port, disk_pager_port,
			  MACH_MSG_TYPE_MAKE_SEND);

  /* Now map the disk image.  */
  err = vm_map (mach_task_self (), (vm_address_t *)image, size,
		0, 1, disk_pager_port, 0, 0,
		VM_PROT_READ | (diskfs_readonly ? 0 : VM_PROT_WRITE),
		VM_PROT_READ | VM_PROT_WRITE,
		VM_INHERIT_NONE);
  if (err)
    error (2, err, "cannot vm_map whole disk");

  /* Set up the signal preemptor to catch faults on the disk image.  */
  preemptor.first = (vm_address_t) *image;
  preemptor.last = ((vm_address_t) *image + size);
  hurd_preempt_signals (&preemptor);

  /* We have the mapping; we no longer need the send right.  */
  mach_port_deallocate (mach_task_self (), disk_pager_port);
}

static void
fault_handler (int sig, long int sigcode, struct sigcontext *scp)
{
  jmp_buf *env = cthread_data (cthread_self ());
  error_t err;

#ifndef NDEBUG
  if (!env)
    {
      error (0, 0,
	     "BUG: unexpected fault on disk image (%d, %#lx) in [%#lx,%#lx)"
	     " eip %#zx err %#x",
	     sig, sigcode,
	     preemptor.first, preemptor.last,
	     scp->sc_pc, scp->sc_error);
      assert (scp->sc_error == EKERN_MEMORY_ERROR);
      err = pager_get_error (diskfs_disk_pager, sigcode);
      assert (err);
      assert_perror (err);
    }
#endif

  /* Clear the record, since the faulting thread will not.  */
  cthread_set_data (cthread_self (), 0);

  /* Fetch the error code from the pager.  */
  assert (scp->sc_error == EKERN_MEMORY_ERROR);
  err = pager_get_error (diskfs_disk_pager, sigcode);
  assert (err);

  /* Make `diskfault_catch' return the error code.  */
  longjmp (*env, err);
}
