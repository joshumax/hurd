/* Ports library for server construction
   Copyright (C) 1993,94,95,96,97,99,2000 Free Software Foundation, Inc.
   Written by Michael I. Bushnell.

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef _HURD_PORTS_
#define _HURD_PORTS_

#include <mach.h>
#include <stdlib.h>
#include <hurd.h>
#include <hurd/ihash.h>
#include <mach/notify.h>
#include <pthread.h>
#include <refcount.h>

#include "port-deref-deferred.h"

#ifdef PORTS_DEFINE_EI
#define PORTS_EI
#else
#define PORTS_EI __extern_inline
#endif

/* These are global values for common flags used in the various structures.
   Not all of these are meaningful in all flag fields.  */
#define PORTS_INHIBITED		0x0100 /* block RPC's */
#define PORTS_BLOCKED		0x0200 /* if INHIBITED, someone is blocked */
#define PORTS_INHIBIT_WAIT	0x0400 /* someone wants to start inhibit */
#define PORTS_NO_ALLOC		0x0800 /* block allocation */
#define PORTS_ALLOC_WAIT	0x1000 /* someone wants to allocate */

struct port_info
{
#ifdef __cplusplus
  struct port_class *port_class;
#else
  struct port_class *class;
#endif
  refcounts_t refcounts;
  mach_port_mscount_t mscount;
  mach_msg_seqno_t cancel_threshold;	/* needs atomic operations */
  int flags;
  mach_port_t port_right;
  struct rpc_info *current_rpcs;
  struct port_bucket *bucket;
  hurd_ihash_locp_t hentry;
  hurd_ihash_locp_t ports_htable_entry;
};
typedef struct port_info *port_info_t;

/* FLAGS above are the following: */
#define PORT_HAS_SENDRIGHTS	0x0001 /* send rights extant */
#define PORT_INHIBITED		PORTS_INHIBITED
#define PORT_BLOCKED		PORTS_BLOCKED
#define PORT_INHIBIT_WAIT	PORTS_INHIBIT_WAIT

struct port_bucket
{
  mach_port_t portset;
  /* Per-bucket hash table used for fast iteration.  Access must be
     serialized using _ports_htable_lock.  */
  struct hurd_ihash htable;
  int rpcs;
  int flags;
  int count;
  struct ports_threadpool threadpool;
};
/* FLAGS above are the following: */
#define PORT_BUCKET_INHIBITED	PORTS_INHIBITED
#define PORT_BUCKET_BLOCKED	PORTS_BLOCKED
#define PORT_BUCKET_INHIBIT_WAIT PORTS_INHIBIT_WAIT
#define PORT_BUCKET_NO_ALLOC	PORTS_NO_ALLOC
#define PORT_BUCKET_ALLOC_WAIT	PORTS_ALLOC_WAIT

struct port_class
{
  int flags;
  int rpcs;
  int count;
  void (*clean_routine) (void *);
  void (*dropweak_routine) (void *);
  struct ports_msg_id_range *uninhibitable_rpcs;
};
/* FLAGS are the following: */
#define PORT_CLASS_INHIBITED	PORTS_INHIBITED
#define PORT_CLASS_BLOCKED	PORTS_BLOCKED
#define PORT_CLASS_INHIBIT_WAIT	PORTS_INHIBIT_WAIT
#define PORT_CLASS_NO_ALLOC	PORTS_NO_ALLOC
#define PORT_CLASS_ALLOC_WAIT	PORTS_ALLOC_WAIT

struct rpc_info
{
  thread_t thread;
  struct rpc_info *next, **prevp;
  struct rpc_notify *notifies;
  struct rpc_info *interrupted_next;
};

/* An rpc has requested interruption on a port notification.  */
struct rpc_notify
{
  struct rpc_info *rpc;		/* Which rpc this is for. */
  struct ports_notify *notify;	/* Which port/request this refers too. */

  struct rpc_notify *next;	/* Notify for this rpc. */
  unsigned pending;		/* Number of requests this represents.  */

  struct rpc_notify *next_req;	/* rpc for this notify.  */
  struct rpc_notify **prev_req_p; /* who points to this rpc_notify. */
};

/* A notification request on a (not necessarily registered) port.  */
struct ports_notify
{
  mach_port_t port;		/*  */
  mach_msg_id_t what;		/* MACH_NOTIFY_* */
  unsigned pending : 1;		/* There's a notification outstanding.  */
  pthread_mutex_t lock;

  struct rpc_notify *reqs;	/* Which rpcs are notified by this port. */
  struct ports_notify *next, **prevp; /* Linked list of all notified ports.  */
};

/* A linked list of ports that have had notification requested.  */
extern struct ports_notify *_ports_notifications;

/* Free lists for notify structures.  */
extern struct ports_notify *_ports_free_ports_notifies;
extern struct rpc_notify *_ports_free_rpc_notifies;

/* Remove RPC from the list of notified rpcs, cancelling any pending
   notifications.  _PORTS_LOCK should be held.  */
void _ports_remove_notified_rpc (struct rpc_info *rpc);

struct ports_msg_id_range
{
  mach_msg_id_t start, end;
  struct ports_msg_id_range *next;
};

/* This is the initial value for the uninhibitable_rpcs field in new
   port_class structures.  The user may define this variable; the default
   value contains only an entry for interrupt_operation.  */
extern struct ports_msg_id_range *ports_default_uninhibitable_rpcs;

/* Port creation and port right frobbing */

/* Create and return a new bucket. */
struct port_bucket *ports_create_bucket (void);

/* Create and return a new port class.  If nonzero, CLEAN_ROUTINE will
   be called for each allocated port object in this class when it is
   being destroyed.   If nonzero, DROPWEAK_ROUTINE will be called
   to request weak references to be dropped.  (If DROPWEAK_ROUTINE is null,
   then normal references and hard references will be identical for
   ports of this class.)  */
struct port_class *ports_create_class (void (*clean_routine)(void *),
				       void (*dropweak_routine)(void *));

/* Create and return in RESULT a new port in CLASS and BUCKET; SIZE bytes
   will be allocated to hold the port structure and whatever private data the
   user desires.  */
error_t ports_create_port (struct port_class *port_class,
			   struct port_bucket *bucket,
			   size_t size,
			   void *result);

/* Just like ports_create_port, except don't actually put the port
   into the portset underlying BUCKET.  This is intended to be used
   for cases where the port right must be given out before the port is
   fully initialized; with this call you are guaranteed that no RPC
   service will occur on the port until you have finished initializing
   it and installed it into the portset yourself. */
error_t
ports_create_port_noinstall (struct port_class *port_class,
			     struct port_bucket *bucket,
			     size_t size,
			     void *result);

/* For an existing RECEIVE right, create and return in RESULT a new port
   structure; BUCKET, SIZE, and CLASS args are as for ports_create_port. */
error_t ports_import_port (struct port_class *port_class,
			   struct port_bucket *bucket,
			   mach_port_t port, size_t size,
			   void *result);

/* Destroy the receive right currently associated with PORT and allocate
   a new one. */
void ports_reallocate_port (void *port);

/* Destroy the receive right currently associated with PORT and designate
   RECEIVE as the new one. */
void ports_reallocate_from_external (void *port, mach_port_t receive);

/* Destroy the receive right currently associated with PORT.  After
   this call, ports_reallocate_port and ports_reallocate_from_external
   may not be used.  Always returns 0, for convenient use as an iterator.  */
error_t ports_destroy_right (void *port);

/* Return the receive right currently associated with PORT.  The effects
   on PORT are the same as in ports_destroy_right, except that the receive
   right itself is not affected.  Note that in multi-threaded servers,
   messages might already have been dequeued for this port before it gets
   removed from the portset; such messages will get EOPNOTSUPP errors.  */
mach_port_t ports_claim_right (void *port);

/* Transfer the receive right from FROMPT to TOPT.  FROMPT ends up
   with a destroyed right (as if ports_destroy_right were called) and
   TOPT's old right is destroyed (as if ports_reallocate_from_external
   were called. */
error_t ports_transfer_right (void *topt, void *frompt);

/* Return the name of the receive right associated with PORT.  This assumes
   that send rights will shortly be created, and arranges for notifications
   accordingly.  The user is responsible for creating an ordinary send
   right from this name.  */
mach_port_t ports_get_right (void *port);

/* This convenience function uses ports_get_right, and
   deals with the creation of a send right as well.  */
mach_port_t ports_get_send_right (void *port);


/* Reference counting */

/* Look up PORT and return the associated port structure, allocating a
   reference.  If the call fails, return 0.  If BUCKET is nonzero,
   then it specifies a bucket to search; otherwise all buckets will be
   searched.  If CLASS is nonzero, then the lookup will fail if PORT
   is not in CLASS. */
void *ports_lookup_port (struct port_bucket *bucket,
			 mach_port_t port, struct port_class *port_class);

/* Like ports_lookup_port, but uses PAYLOAD to look up the object.  If
   this function is used, PAYLOAD must be a pointer to the port
   structure.  */
extern void *ports_lookup_payload (struct port_bucket *bucket,
				   unsigned long payload,
				   struct port_class *port_class);

/* This returns the ports name.  This function can be used as
   intranpayload function turning payloads back into port names.  If
   this function is used, PAYLOAD must be a pointer to the port
   structure.  */
extern mach_port_t ports_payload_get_name (unsigned int payload);

#if (defined(__USE_EXTERN_INLINES) || defined(PORTS_DEFINE_EI)) && !defined(__cplusplus)

PORTS_EI void *
ports_lookup_payload (struct port_bucket *bucket,
		      unsigned long payload,
		      struct port_class *class)
{
  struct port_info *pi = (struct port_info *) payload;

  if (pi && ! MACH_PORT_VALID (pi->port_right))
    pi = NULL;

  if (pi && bucket && pi->bucket != bucket)
    pi = NULL;

  if (pi && class && pi->class != class)
    pi = NULL;

  if (pi)
    refcounts_unsafe_ref (&pi->refcounts, NULL);

  return pi;
}

PORTS_EI mach_port_t
ports_payload_get_name (unsigned int payload)
{
  struct port_info *pi = (struct port_info *) payload;

  if (pi)
    return pi->port_right;

  return MACH_PORT_NULL;
}

#endif /* Use extern inlines.  */

/* Allocate another reference to PORT. */
void ports_port_ref (void *port);

/* Allocate a weak reference to PORT. */
void ports_port_ref_weak (void *port);

/* Drop a reference to PORT. */
void ports_port_deref (void *port);

/* Drop a weak reference to PORT. */
void ports_port_deref_weak (void *port);

/* The user is responsible for listening for no senders notifications;
   when one arrives, call this routine for the PORT the message was
   sent to, providing the MSCOUNT from the notification. */
void ports_no_senders (void *port, mach_port_mscount_t mscount);
void ports_dead_name (void *notify, mach_port_t dead_name);

/* Block port creation of new ports in CLASS.  Return the number
   of ports currently in CLASS. */
int ports_count_class (struct port_class *port_class);

/* Block port creation of new ports in BUCKET.  Return the number
   of ports currently in BUCKET. */
int ports_count_bucket (struct port_bucket *bucket);

/* Permit suspended port creation (blocked by ports_count_class)
   to continue. */
void ports_enable_class (struct port_class *port_class);

/* Permit suspend port creation (blocked by ports_count_bucket)
   to continue. */
void ports_enable_bucket (struct port_bucket *bucket);

/* Call FUN once for each port in BUCKET. */
error_t ports_bucket_iterate (struct port_bucket *bucket,
			      error_t (*fun)(void *port));

/* Call FUN once for each port in CLASS. */
error_t ports_class_iterate (struct port_class *port_class,
			     error_t (*fun)(void *port));

/* Internal entrypoint for above two.  */
error_t _ports_bucket_class_iterate (struct hurd_ihash *ht,
				     struct port_class *port_class,
				     error_t (*fun)(void *port));

/* RPC management */

/* Type of MiG demuxer routines. */
typedef int (*ports_demuxer_type)(mach_msg_header_t *inp,
				  mach_msg_header_t *outp);

/* Call this when an RPC is beginning on PORT.  INFO should be
   allocated by the caller and will be used to hold dynamic state.
   If this RPC should be abandoned, return EDIED; otherwise we
   return zero. */
error_t ports_begin_rpc (void *port, mach_msg_id_t msg_id,
			 struct rpc_info *info);

/* Call this when an RPC is concluding.  Args must be as for the
   paired call to ports_begin_rpc. */
void ports_end_rpc (void *port, struct rpc_info *info);

/* Begin handling operations for the ports in BUCKET, calling DEMUXER
   for each incoming message.  Return if TIMEOUT is nonzero and no
   messages have been received for TIMEOUT milliseconds.  Use
   only one thread (the calling thread).  */
void ports_manage_port_operations_one_thread(struct port_bucket *bucket,
					     ports_demuxer_type demuxer,
					     int timeout);

/* Begin handling operations for the ports in BUCKET, calling DEMUXER
   for each incoming message.  Return if GLOBAL_TIMEOUT is nonzero and
   no messages have been receieved for GLOBAL_TIMEOUT milliseconds.
   Create threads as necessary to handle incoming messages so that no
   port is starved because of sluggishness on another port.  If
   LOCAL_TIMEOUT is non-zero, then individual threads will die off if
   they handle no incoming messages for LOCAL_TIMEOUT milliseconds.
   HOOK (if not null) will be called in each new thread immediately
   after it is created. */
void ports_manage_port_operations_multithread (struct port_bucket *bucket,
					       ports_demuxer_type demuxer,
					       int thread_timeout,
					       int global_timeout,
					       void (*hook)(void));

/* Interrupt any pending RPC on PORT.  Wait for all pending RPC's to
   finish, and then block any new RPC's starting on that port. */
error_t ports_inhibit_port_rpcs (void *port);

/* Similar to ports_inhibit_port_rpcs, but affects all ports in CLASS. */
error_t ports_inhibit_class_rpcs (struct port_class *port_class);

/* Similar to ports_inhibit_port_rpcs, but affects all ports in BUCKET. */
error_t ports_inhibit_bucket_rpcs (struct port_bucket *bucket);

/* Similar to ports_inhibit_port_rpcs, but affects all ports whatsoever. */
error_t ports_inhibit_all_rpcs (void);

/* Reverse the effect of a previous ports_inhibit_port_rpcs for this PORT,
   allowing blocked RPC's to continue. */
void ports_resume_port_rpcs (void *port);

/* Reverse the effect of a previous ports_inhibit_class_rpcs for CLASS. */
void ports_resume_class_rpcs (struct port_class *port_class);

/* Reverse the effect of a previous ports_inhibit_bucket_rpcs for BUCKET. */
void ports_resume_bucket_rpcs (struct port_bucket *bucket);

/* Reverse the effect of a previous ports_inhibit_all_rpcs. */
void ports_resume_all_rpcs (void);

/* Cancel (with thread_cancel) any RPC's in progress on PORT. */
void ports_interrupt_rpcs (void *port);

/* If the current thread's rpc has been interrupted with
   ports_interrupt_rpcs, return true (and clear the interrupted flag).  */
int ports_self_interrupted ();

/* Add RPC to the list of rpcs that have been interrupted.  */
void _ports_record_interruption (struct rpc_info *rpc);

/* Arrange for hurd_cancel to be called on RPC's thread if OBJECT gets notified
   that any of the things in COND have happened to PORT.  RPC should be an
   rpc on OBJECT.  */
error_t
ports_interrupt_rpc_on_notification (void *object,
				     struct rpc_info *rpc,
				     mach_port_t port, mach_msg_id_t what);

/* Arrange for hurd_cancel to be called on the current thread, which should
   be an rpc on OBJECT, if PORT gets notified with the condition WHAT.  */
error_t
ports_interrupt_self_on_notification (void *object,
				      mach_port_t port, mach_msg_id_t what);

/* Some handy aliases.  */
#define ports_interrupt_self_on_port_death(obj, port) \
  ports_interrupt_self_on_notification (obj, port, MACH_NOTIFY_DEAD_NAME)

/* Interrupt any rpcs on OBJECT that have requested such.  */
void ports_interrupt_notified_rpcs (void *object, mach_port_t port,
				    mach_msg_id_t what);

/* Default servers */

/* A notification server that calls the ports_do_mach_notify_* routines.  */
int ports_notify_server (mach_msg_header_t *, mach_msg_header_t *);

/* Notification server routines called by ports_notify_server.  */
extern kern_return_t
 ports_do_mach_notify_dead_name (struct port_info *pi, mach_port_t deadport);
extern kern_return_t
 ports_do_mach_notify_msg_accepted (struct port_info *pi, mach_port_t name);
extern kern_return_t
 ports_do_mach_notify_no_senders (struct port_info *pi,
				  mach_port_mscount_t count);
extern kern_return_t
 ports_do_mach_notify_port_deleted (struct port_info *pi, mach_port_t name);
extern kern_return_t
 ports_do_mach_notify_port_destroyed (struct port_info *pi, mach_port_t name);
extern kern_return_t
 ports_do_mach_notify_send_once (struct port_info *pi);

/* Private data */
extern pthread_mutex_t _ports_lock;
extern pthread_cond_t _ports_block;

/* A global hash table mapping port names to port_info objects.  This
   table is used for port lookups and to iterate over classes.

   A port in this hash table carries an implicit light reference.
   When the reference counts reach zero, we call
   _ports_complete_deallocate.  There we reacquire our lock
   momentarily to check whether someone else reacquired a reference
   through the hash table.  */
extern struct hurd_ihash _ports_htable;
/* Access to all hash tables is protected by this lock.  */
extern pthread_rwlock_t _ports_htable_lock;

extern int _ports_total_rpcs;
extern int _ports_flags;
#define _PORTS_INHIBITED	PORTS_INHIBITED
#define _PORTS_BLOCKED		PORTS_BLOCKED
#define _PORTS_INHIBIT_WAIT	PORTS_INHIBIT_WAIT
void _ports_complete_deallocate (struct port_info *);
error_t _ports_create_port_internal (struct port_class *, struct port_bucket *,
				     size_t, void *, int);

#endif
