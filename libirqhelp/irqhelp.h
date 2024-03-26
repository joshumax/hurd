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

#ifndef _HURD_IRQHELP_
#define _HURD_IRQHELP_

#include <mach.h>
#include <hurd/hurd_types.h>
#include <pthread.h>
#include <stdlib.h>

struct irq;

/* Call once before using other functions */
error_t irqhelp_init(void);

/* Accepts gsi or bus/dev/fun or both, but cant be all -1.
   If gsi is -1, will lookup the gsi via ACPI.
   If bus/dev/fun are -1, must pass in gsi.
   Returns a pointer to be used with other functions. */
struct irq * irqhelp_install_interrupt_handler(int gsi, int bus, int dev, int fun,
					       void (*handler)(void *), void *context);
/* Install as pthread */
void * irqhelp_server_loop(void *arg);

/* Enable an irq */
void irqhelp_enable_irq(struct irq *irq);

/* Disable an irq */
void irqhelp_disable_irq(struct irq *irq);

/* Call to deregister a handler */
error_t irqhelp_remove_interrupt_handler(struct irq *irq);

#endif
