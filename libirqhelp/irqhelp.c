/* Library providing helper functions for userspace irq handling.
   Copyright (C) 2022 Free Software Foundation, Inc.

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

#include "irqhelp.h"

#include <sys/types.h>
#include <sys/queue.h>

#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <hurd.h>
#include <hurd/paths.h>
#include <device/notify.h>
#include <device/device.h>
#include "acpi_U.h"
#include <mach.h>
#include <stdbool.h>

#define IRQ_THREAD_PRIORITY	2
#define log_error(fmt...)	fprintf(stderr, ## fmt)

struct irq {
  void (*handler)(void *);
  void *context;
  int gsi;
  mach_port_t port;
  bool enabled;
  bool shutdown;
  pthread_mutex_t irqlock;
  pthread_cond_t irqcond;
};

static mach_port_t irqdev = MACH_PORT_NULL;
static mach_port_t acpidev = MACH_PORT_NULL;

static error_t
get_acpi(void)
{
  error_t err = 0;
  mach_port_t tryacpi, device_master;

  err = get_privileged_ports (0, &device_master);
  if (!err)
    {
      err = device_open (device_master, D_READ, "acpi", &tryacpi);
      mach_port_deallocate (mach_task_self (), device_master);
      if (!err)
	{
	  acpidev = tryacpi;
	  return 0;
	}
    }

  tryacpi = file_name_lookup (_SERVERS_ACPI, O_RDONLY, 0);
  if (tryacpi == MACH_PORT_NULL)
    return ENODEV;

  acpidev = tryacpi;
  return 0;
}

static error_t
get_irq(void)
{
  error_t err = 0;
  mach_port_t tryirq, device_master;

  err = get_privileged_ports (0, &device_master);
  if (err)
    return err;

  err = device_open (device_master, D_READ|D_WRITE, "irq", &tryirq);
  mach_port_deallocate (mach_task_self (), device_master);
  if (err)
    return err;

  irqdev = tryirq;
  return err;
}

static void
toggle_irq(struct irq *irq, bool on)
{
  pthread_mutex_lock (&irq->irqlock);
  irq->enabled = on;
  pthread_mutex_unlock (&irq->irqlock);

  if (on)
    pthread_cond_signal (&irq->irqcond);
}

error_t
irqhelp_init(void)
{
  static bool inited = false;
  error_t err;

  if (inited)
    {
      log_error("already inited\n");
      return 0;
    }

  err = get_irq();
  if (err)
    {
      log_error("cannot grab irq device\n");
      return err;
    }

  err = get_acpi();
  if (err)
    {
      log_error("cannot grab acpi device\n");
      return err;
    }

  inited = true;
  return 0;
}

void
irqhelp_disable_irq(struct irq *irq)
{
  if (!irq)
    { 
      log_error("cannot disable this irq\n");
      return;
    }

  toggle_irq(irq, false);
}

void
irqhelp_enable_irq(struct irq *irq)
{
  if (!irq)
    {
      log_error("cannot enable this irq\n");
      return;
    }

  toggle_irq(irq, true);
}

void *
irqhelp_server_loop(void *arg)
{
  struct irq *irq = (struct irq *)arg;
  mach_port_t master_host;
  mach_port_t pset, psetcntl;
  error_t err;

  if (!irq)
    {
      log_error("cannot start this irq thread\n");
      return NULL;
    }

  err = get_privileged_ports (&master_host, 0);
  if (err)
    {
      log_error("cannot get master_host port\n");
      return NULL;
    }

  err = thread_get_assignment (mach_thread_self (), &pset);
  if (err)
    goto fail;

  err = host_processor_set_priv (master_host, pset, &psetcntl);
  if (err)
    goto fail;

  thread_max_priority (mach_thread_self (), psetcntl, 0);
  err = thread_priority (mach_thread_self (), IRQ_THREAD_PRIORITY, 0);
  if (err)
    goto fail;

  mach_port_deallocate (mach_task_self (), master_host);

  int interrupt_demuxer (mach_msg_header_t *inp,
			 mach_msg_header_t *outp)
  {
    device_intr_notification_t *n = (device_intr_notification_t *) inp;

    ((mig_reply_header_t *) outp)->RetCode = MIG_NO_REPLY;
    if (n->intr_header.msgh_id != DEVICE_INTR_NOTIFY)
      {
	static bool printed0 = false;
	if (!printed0)
	  {
	    log_error("msg received is not an interrupt\n");
	    printed0 = true;
	  }
	return 0;
      }

    /* FIXME: id <-> gsi now has an indirection, assuming 1:1 */
    if (n->id != irq->gsi)
      {
	static bool printed1 = false;
	if (!printed1)
	  {
	    log_error("interrupt expected on irq %d arrived on irq %d\n", irq->gsi, n->id);
	    printed1 = true;
	  }
	return 0;
      }

    /* wait if irq disabled */
    pthread_mutex_lock (&irq->irqlock);
    while (!irq->enabled)
      pthread_cond_wait (&irq->irqcond, &irq->irqlock);
    pthread_mutex_unlock (&irq->irqlock);

    /* handle interrupt when not shutting down */
    if (!irq->shutdown)
      {
        irq->handler(irq->context);
        device_intr_ack (irqdev, irq->port, MACH_MSG_TYPE_MAKE_SEND);
      }
    return 1;
  }

  /* Server loop */
  mach_msg_server (interrupt_demuxer, 0, irq->port);
  goto done;

fail:
  log_error("failed to register irq %d\n", irq->gsi);

done:
  pthread_cond_destroy(&irq->irqcond);
  pthread_mutex_destroy(&irq->irqlock);
  free(irq);
  return NULL;
}

static struct irq *
interrupt_register(int gsi,
		   void (*handler)(void *),
		   void *context)
{
  error_t err;
  struct irq *irq = NULL;
  mach_port_t delivery_port;

  irq = malloc(sizeof(struct irq));
  if (!irq)
    {
      log_error("cannot malloc irq\n");
      return NULL;
    }

  irq->handler = handler;
  irq->context = context;
  irq->gsi = gsi;
  irq->enabled = true;  /* avoid deadlock by not requiring initial explicit enable */
  irq->shutdown = false;
  pthread_mutex_init (&irq->irqlock, NULL);
  pthread_cond_init (&irq->irqcond, NULL);
  
  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &delivery_port);
  if (err)
    {
      log_error("cannot allocate mach port\n");
      return NULL;
    }

  irq->port = delivery_port;

  err = device_intr_register(irqdev, irq->gsi,
			     0, irq->port,
			     MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    {
      log_error("device_intr_register failed with %d\n", err);
      return NULL;
    }

  return irq;
}

struct irq *
irqhelp_install_interrupt_handler(int gsi,
				  int bus,
				  int dev,
				  int fun,
				  void (*handler)(void *),
				  void *context)
{
  error_t err;

  if (!handler)
    {
      log_error("no handler\n");
      return NULL;
    }

  if (gsi < 0)
    {
      if ((bus < 0) || (dev < 0) || (fun < 0))
	{
	  log_error("invalid b/d/f\n");
	  return NULL;
	}

      /* We need to call acpi translator to get gsi */
      err = acpi_get_pci_irq (acpidev, bus, dev, fun, &gsi);
      if (err)
	{
	  log_error("tried acpi to get pci gsi and failed for %02x:%02x.%d\n", bus, dev, fun);
	  return NULL;
	}
    }

  return interrupt_register(gsi, handler, context);
}

error_t
irqhelp_remove_interrupt_handler(struct irq *irq)
{
  if (!irq)
    {
      log_error("cannot deregister this irq\n");
      return ENODEV;
    }

  irq->shutdown = true;

  /* Turn port into dead name */
  mach_port_insert_right (mach_task_self (), irq->port, irq->port,
                          MACH_MSG_TYPE_MAKE_SEND);
  mach_port_mod_refs (mach_task_self (), irq->port,
                      MACH_PORT_RIGHT_RECEIVE, -1);
  return 0;
}
