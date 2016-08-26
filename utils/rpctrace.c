/* Trace RPCs sent to selected ports

   Copyright (C) 1998, 1999, 2001, 2002, 2003, 2005, 2006, 2009, 2011,
   2013 Free Software Foundation, Inc.

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
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA. */

#include <hurd.h>
#include <hurd/ports.h>
#include <hurd/ihash.h>
#include <mach/message.h>
#include <assert-backtrace.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <argp.h>
#include <error.h>
#include <string.h>
#include <version.h>
#include <sys/wait.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <argz.h>
#include <envz.h>

#include "msgids.h"

const char *argp_program_version = STANDARD_HURD_VERSION (rpctrace);

static unsigned strsize = 80;

static const struct argp_option options[] =
{
  {"output", 'o', "FILE", 0, "Send trace output to FILE instead of stderr."},
  {0, 's', "SIZE", 0, "Specify the maximum string size to print (the default is 80)."},
  {0, 'E', "var[=value]", 0,
   "Set/change (var=value) or remove (var) an environment variable among the "
   "ones inherited by the executed process."},
  {0}
};

#define UNKNOWN_NAME MACH_PORT_NULL

static const char args_doc[] = "COMMAND [ARG...]";
static const char doc[] = "Trace Mach Remote Procedure Calls.";

/* This structure stores the information of the traced task. */
struct task_info
{
  task_t task;
  boolean_t threads_wrapped;	/* All threads of the task has been wrapped? */
};

static struct hurd_ihash task_ihash
  = HURD_IHASH_INITIALIZER (HURD_IHASH_NO_LOCP);

task_t unknown_task;

void
add_task (task_t task)
{
  error_t err;
  struct task_info *info = malloc (sizeof *info);

  if (info == NULL)
    error (1, 0, "Fail to allocate memory.");

  info->task = task;
  info->threads_wrapped = FALSE;
  
  err = hurd_ihash_add (&task_ihash, task, info);
  if (err)
    error (1, err, "hurd_ihash_add");
}

void
remove_task (task_t task)
{
  hurd_ihash_remove (&task_ihash, task);
}

static const char *
msgid_name (mach_msg_id_t msgid)
{
  const struct msgid_info *info = msgid_info (msgid);
  return info ? info->name : 0;
}

/* Return true if this message's data should be printed out.
   For a request message, that means the in parameters.
   For a reply messages, that means the return code and out parameters.  */
static int
msgid_display (const struct msgid_info *info)
{
  return 1;
}

/* Return true if we should interpose on this RPC's reply port.  If this
   returns false, we will pass the caller's original reply port through so
   we never see the reply message at all.  */
static int
msgid_trace_replies (const struct msgid_info *info)
{
  return 1;
}


/* A common structure between sender_info and send_once_info */
struct traced_info
{
  struct port_info pi;
  mach_msg_type_name_t type;
  char *name;			/* null or a string describing this */
};

/* Each traced port has one receiver info and multiple send wrappers.
 * The receiver info records the information of the receive right to
 * the traced port, while send wrappers are created for each task
 * who has the send right to the traced port.
 */

struct receiver_info
{
  char *name;			/* null or a string describing this */
  hurd_ihash_locp_t locp;	/* position in the traced_names hash table */
  mach_port_t portname;		/* The port name in the owner task. */
  task_t task;			/* The task who has the right. */
  mach_port_t forward;		/* real port. */
  struct receiver_info *receive_right;	/* Link with other receive rights. */
  struct sender_info *next;	/* The head of the send right list */
};

struct sender_info
{
  struct traced_info pi;
  task_t task;			/* The task who has the right. */

  /* It is used to form the list of send rights for different tasks.
   * The head is the receive right. */
  struct sender_info *next;

  struct receiver_info *receive_right;	/* The corresponding receive right */
};

struct send_once_info
{
  struct traced_info pi;
  mach_port_t forward;		/* real port. */

  struct send_once_info *nextfree; /* Link when on free list.  */
};

#define INFO_SEND_ONCE(info) ((info)->type == MACH_MSG_TYPE_MOVE_SEND_ONCE)
#define TRACED_INFO(info) ((struct traced_info *) info)
#define SEND_INFO(info) ((struct sender_info *) info)
#define SEND_ONCE_INFO(info) ((struct send_once_info *) info)

/* This structure stores the information of the RPC requests. */
struct req_info
{
  boolean_t is_req;
  mach_msg_id_t req_id;
  mach_port_t reply_port;
  task_t from;
  task_t to;
  struct req_info *next;
};

static struct req_info *req_head = NULL;

static struct req_info *
add_request (mach_msg_id_t req_id, mach_port_t reply_port,
	     task_t from, task_t to)
{
  struct req_info *req = malloc (sizeof (*req));
  if (!req)
    error (1, 0, "cannot allocate memory");
  req->req_id = req_id;
  req->from = from;
  req->to = to;
  req->reply_port = reply_port;
  req->is_req = TRUE;

  req->next = req_head;
  req_head = req;

  return req;
}

static struct req_info *
remove_request (mach_msg_id_t req_id, mach_port_t reply_port)
{
  struct req_info **prev;
  struct req_info *req;

  prev = &req_head;
  while (*prev)
    {
      if ((*prev)->req_id == req_id && (*prev)->reply_port == reply_port)
	break;
      prev = &(*prev)->next;
    }
  if (*prev == NULL)
    return NULL;

  req = *prev;
  *prev = req->next;
  return req;
}

struct port_info *notify_pi;
/* The list of receiver infos, but only the ones for the traced tasks. */
struct receiver_info *receive_right_list;
static struct traced_info dummy_wrapper;
static struct send_once_info *freelist;

struct hurd_ihash traced_names
  = HURD_IHASH_INITIALIZER (offsetof (struct receiver_info, locp));
struct port_class *traced_class;
struct port_class *other_class;
struct port_bucket *traced_bucket;
FILE *ostream;

/* These are the calls made from the tracing engine into
   the output formatting code.  */

/* Called for a message that does not look like an RPC reply.
   The header has already been swapped into the sender's view
   with interposed ports.  */
static void print_request_header (struct sender_info *info,
				  mach_msg_header_t *header);

/* Called for a message that looks like an RPC reply.  */
static void print_reply_header (struct send_once_info *info,
				mig_reply_header_t *header,
				struct req_info *req);

/* Called for each data item (which might be an array).
   Always called after one of the above two.  */
static void print_data (mach_msg_type_name_t type,
			const void *data,
			mach_msg_type_number_t nelt,
			mach_msg_type_number_t eltsize);


/*** Mechanics of tracing messages and interposing on ports ***/

/* Create a new info for the receive right.
 * It lives until the traced receive right dies. */
static struct receiver_info *
new_receiver_info (mach_port_t right, mach_port_t owner)
{
  error_t err;
  struct receiver_info *info;
  mach_port_t foo;

  info = malloc (sizeof (*info));
  if (!info)
    error (1, 0, "cannot allocate memory");
  info->forward = right;
  info->task = owner;
  info->portname = UNKNOWN_NAME;
  info->receive_right = NULL;
  info->next = NULL;
  if (owner != unknown_task)
    {
      info->receive_right = receive_right_list;
      receive_right_list = info;
    }
  info->name = 0;

  /* Request the dead-name notification, so if the receive right is destroyed,
   * we can destroy the wrapper. */
  err = mach_port_request_notification (mach_task_self (), right,
					MACH_NOTIFY_DEAD_NAME, 1,
					notify_pi->port_right,
					MACH_MSG_TYPE_MAKE_SEND_ONCE, &foo);
  if (err)
    error (2, err, "mach_port_request_notification");
  if (MACH_PORT_VALID (foo))
    mach_port_deallocate (mach_task_self (), foo);

  err = hurd_ihash_add (&traced_names, info->forward, info);
  if (err)
    error (2, err, "hurd_ihash_add");
  return info;
}

static void
destroy_receiver_info (struct receiver_info *info)
{
  struct sender_info *send_wrapper;
  struct receiver_info **prev;

  mach_port_deallocate (mach_task_self (), info->forward);
  /* Remove it from the receive right list. */
  prev = &receive_right_list;
  while (*prev != info && *prev)
    prev = &((*prev)->receive_right);
  /* If we find the receiver info in the list. */
  if (*prev)
    *prev = info->receive_right;
  
  send_wrapper = info->next;
  while (send_wrapper)
    {
      struct sender_info *next = send_wrapper->next;
      assert_backtrace (
	refcounts_hard_references (&TRACED_INFO (send_wrapper)->pi.refcounts)
	== 1);
      /* Reset the receive_right of the send wrapper in advance to avoid
       * destroy_receiver_info is called when the port info is destroyed. */
      send_wrapper->receive_right = NULL;
      ports_destroy_right (send_wrapper);
      send_wrapper = next;
    }

  hurd_ihash_locp_remove (&traced_names, info->locp);
  free (info);
}

/* Create a new wrapper port and do `ports_get_right' on it.
 *
 * The wrapper lives until there is no send right to it,
 * or the corresponding receiver info is destroyed.
 */
static struct sender_info *
new_send_wrapper (struct receiver_info *receive, task_t task,
		  mach_port_t *wrapper_right)
{
  error_t err;
  struct sender_info *info;

  /* Create a new wrapper port that forwards to *RIGHT.  */
  err = ports_create_port (traced_class, traced_bucket,
			   sizeof *info, &info);
  assert_perror_backtrace (err);

  TRACED_INFO (info)->name = 0;
  asprintf (&TRACED_INFO (info)->name, "  %lu<--%lu(pid%d)", 
	    receive->forward, TRACED_INFO (info)->pi.port_right, task2pid (task));
  TRACED_INFO (info)->type = MACH_MSG_TYPE_MOVE_SEND;
  info->task = task;
  info->receive_right = receive;
  info->next = receive->next;
  receive->next = info;

  *wrapper_right = ports_get_right (info);
  ports_port_deref (info);

  return info;
}

/* Create a new wrapper port and do `ports_get_right' on it.  */
static struct send_once_info *
new_send_once_wrapper (mach_port_t right, mach_port_t *wrapper_right)
{
  error_t err;
  struct send_once_info *info;

  /* Use a free send-once wrapper port if we have one.  */
  if (freelist)
    {
      info = freelist;
      freelist = info->nextfree;
    }
  else
    {
      /* Create a new wrapper port that forwards to *RIGHT.  */
      err = ports_create_port (traced_class, traced_bucket,
			       sizeof *info, &info);
      assert_perror_backtrace (err);
      TRACED_INFO (info)->name = 0;
    }

  info->forward = right;
  TRACED_INFO (info)->type = MACH_MSG_TYPE_MOVE_SEND_ONCE;
  info->nextfree = NULL;

  /* Send-once rights never compare equal to any other right (even
     another send-once right), so there is no point in putting them
     in the reverse-lookup table.

     Since we never make send rights to this port, we don't want to
     use the normal libports mechanisms (ports_get_right) that are
     designed for send rights and no-senders notifications.
     Instead, we hold on to the initial hard ref to INFO until we
     receive a message on it.  The kernel automatically sends a
     MACH_NOTIFY_SEND_ONCE message if the send-once right dies.  */

  *wrapper_right = TRACED_INFO (info)->pi.port_right;

  return info;
}

/* Unlink the send wrapper from the list. */
static void
unlink_sender_info (void *pi)
{
  struct sender_info *info = pi;
  struct sender_info **prev;

  if (info->receive_right)
    {
      /* Remove it from the send right list. */
      prev = &info->receive_right->next;
      while (*prev != info && *prev)
	prev = &((*prev)->next);
      assert_backtrace (*prev);
      *prev = info->next;

      info->next = NULL;
    }
}

/* The function is called when the port_info is going to be destroyed.
 * If it's the last send wrapper for the traced port, the receiver info
 * will also be destroyed. */
static void
traced_clean (void *pi)
{
  struct sender_info *info = pi;

  assert_backtrace (TRACED_INFO (info)->type == MACH_MSG_TYPE_MOVE_SEND);
  free (TRACED_INFO (info)->name);

  if (info->receive_right)
    {
      unlink_sender_info (pi);

      /* If this is the last send wrapper, it means that our traced port won't
       * have any more send rights. We notify the owner of the receive right
       * of that by deallocating the forward port. */
      if (info->receive_right->next == NULL)
	destroy_receiver_info (info->receive_right);

      info->receive_right = NULL;
    }
}

/* Check if the receive right has been seen. */
boolean_t
seen_receive_right (task_t task, mach_port_t name)
{
  struct receiver_info *info = receive_right_list;
  while (info)
    {
      if (info->task == task && info->portname == name)
	return TRUE;
      info = info->receive_right;
    }
  return FALSE;
}

/* This function is to find the receive right for the send right 'send'
 * among traced tasks. I assume that all receive rights are moved
 * under the control of rpctrace.
 *
 * Note: 'send' shouldn't be the send right to the wrapper.
 *
 * Note: the receiver_info returned from the function
 * might not be the receive right in the traced tasks.
 * */
struct receiver_info *
discover_receive_right (mach_port_t send, task_t task)
{
  error_t err;
  struct receiver_info *info = NULL;

  info = hurd_ihash_find (&traced_names, send);
  /* If we have seen the send right or send once right. */
  if (info
      /* If the receive right is in one of traced tasks,
       * but we don't know its name 
       * (probably because the receive right has been moved),
       * we need to find it out. */
      && !(info->task != unknown_task
	  && info->portname == UNKNOWN_NAME))
    return info;

  {
      int j;
      mach_port_t *portnames = NULL;
      mach_msg_type_number_t nportnames = 0;
      mach_port_type_t *porttypes = NULL;
      mach_msg_type_number_t nporttypes = 0;
      struct receiver_info *receiver_info = NULL;

      err = mach_port_names (task, &portnames, &nportnames,
			     &porttypes, &nporttypes);
      if (err == MACH_SEND_INVALID_DEST)
	{
	  remove_task (task);
	  return 0;
	}
      if (err)
	error (2, err, "mach_port_names");

      for (j = 0; j < nportnames; j++)
	{
	  mach_port_status_t port_status;
	  mach_port_t send_right;
	  mach_msg_type_name_t type;

	  if (!(porttypes[j] & MACH_PORT_TYPE_RECEIVE) /* not a receive right */
	      || seen_receive_right (task, portnames[j]))
	    continue;

	  err = mach_port_get_receive_status (task, portnames[j],
					      &port_status);
	  if (err)
	    error (2, err, "mach_port_get_receive_status");
	  /* If the port doesn't have the send right, skip it. */
	  if (!port_status.mps_srights)
	    continue;

	  err = mach_port_extract_right (task, portnames[j],
					 MACH_MSG_TYPE_MAKE_SEND,
					 &send_right, &type);
	  if (err)
	    error (2, err, "mach_port_extract_right");

	  if (/* We have seen this send right before. */
	      hurd_ihash_find (&traced_names, send_right)
	      || send_right != send	/* It's not the port we want. */)
	    {
	      mach_port_deallocate (mach_task_self (), send_right);
	      continue;
	    }

	  /* We have found the receive right we want. */
	  receiver_info = new_receiver_info (send_right, task);
	  receiver_info->portname = portnames[j];
	  break;
	}
      if (portnames)
	vm_deallocate (mach_task_self (), (vm_address_t) portnames,
		       nportnames * sizeof (*portnames));
      if (porttypes)
	vm_deallocate (mach_task_self (), (vm_address_t) porttypes,
		       nporttypes * sizeof (*porttypes));

      if (receiver_info)
	return receiver_info;
  }
  return NULL;
}

/* get_send_wrapper searches for the send wrapper for the target task.
   If it doesn't exist, create a new one. */
struct sender_info *
get_send_wrapper (struct receiver_info *receiver_info,
		  mach_port_t task, mach_port_t *right)
{
  struct sender_info *info = receiver_info->next;
  
  while (info)
    {
      if (info->task == task)
	{
	  *right = ports_get_right (info);
	  return info;
	}
      info = info->next;
    }
  /* No send wrapper is found. */
  return new_send_wrapper (receiver_info, task, right);
}

/* Rewrite a port right in a message with an appropriate wrapper port.  */
static char *
rewrite_right (mach_port_t *right, mach_msg_type_name_t *type,
	       struct req_info *req)
{
  error_t err;
  struct receiver_info *receiver_info;
  struct sender_info *send_wrapper;
  task_t dest = unknown_task;
  task_t source = unknown_task;

  /* We can never do anything special with a null or dead port right.  */
  if (!MACH_PORT_VALID (*right))
    return 0;

  if (req)
    {
      if (req->is_req)    /* It's a RPC request. */
	{
	  source = req->from;
	  dest = req->to;
	}
      else
	{
	  source = req->to;
	  dest = req->from;
	}
    }

  switch (*type)
    {
    case MACH_MSG_TYPE_PORT_SEND:
      /* The strategy for moving the send right is: if the destination task
       * has the receive right, we move the send right of the traced port to
       * the destination; otherwise, we move the one of the send wrapper.
       */
      /* See if this is already one of our own wrapper ports.  */
      send_wrapper = ports_lookup_port (traced_bucket, *right, 0);
      if (send_wrapper)
	{
	  /* This is a send right to one of our own wrapper ports. */
	  mach_port_deallocate (mach_task_self (), *right); /* eat msg ref */

	  /* If the send right is moved to the task with the receive right,
	   * copy the send right in 'forward' of receiver info to the destination.
	   * Otherwise, copy the send right to the send wrapper. */
	  assert_backtrace (send_wrapper->receive_right);
	  if (dest == send_wrapper->receive_right->task)
	    {
	      *right = send_wrapper->receive_right->forward;
	      err = mach_port_mod_refs (mach_task_self (), *right,
					MACH_PORT_RIGHT_SEND, +1);
	      if (err)
		error (2, err, "mach_port_mod_refs");
	      ports_port_deref (send_wrapper);
	    }
	  else
	    {
	      struct sender_info *send_wrapper2
		= get_send_wrapper (send_wrapper->receive_right, dest, right);
	      ports_port_deref (send_wrapper);
	      *type = MACH_MSG_TYPE_MAKE_SEND;
	      send_wrapper = send_wrapper2;
	    }
	  return TRACED_INFO (send_wrapper)->name;
	}

      if (req && req->req_id == 3216)	    /* mach_port_extract_right */
	receiver_info = discover_receive_right (*right, dest);
      else
	receiver_info = discover_receive_right (*right, source);
      if (receiver_info == NULL)
	{
	  /* It's unusual to see an unknown send right from a traced task.
	   * We ignore it. */
	  if (source != unknown_task)
	    {
	      /* TODO: this happens on fork() when the new process does not
	         have the send right yet (it is about to get inserted).  */
	      error (0, 0, "get an unknown send right from process %d",
		     task2pid (source));
	      return dummy_wrapper.name;
	    }
	  /* The receive right is owned by an unknown task. */
	  receiver_info = new_receiver_info (*right, unknown_task);
	  mach_port_mod_refs (mach_task_self (), *right,
			      MACH_PORT_RIGHT_SEND, 1);
	}
      /* If the send right is moved to the task with the receive right,
       * don't do anything. 
       * Otherwise, we translate it into the one to the send wrapper. */
      if (dest == receiver_info->task)
	return receiver_info->name;
      else
	{
	  assert_backtrace (*right == receiver_info->forward);
	  mach_port_deallocate (mach_task_self (), *right);
	  send_wrapper = get_send_wrapper (receiver_info, dest, right);
	  *type = MACH_MSG_TYPE_MAKE_SEND;
	  return TRACED_INFO (send_wrapper)->name;
	}

    case MACH_MSG_TYPE_PORT_SEND_ONCE:
      /* There is no way to know if this send-once right is to the same
	 receive right as any other send-once or send right we have seen.
	 Fortunately, it doesn't matter, since the recipient of the
	 send-once right we pass along can't tell either.  We always just
	 make a new send-once wrapper object, that will trace the one
	 message it receives, and then die.  */
      *type = MACH_MSG_TYPE_MAKE_SEND_ONCE;
      return TRACED_INFO (new_send_once_wrapper (*right, right))->name;

    case MACH_MSG_TYPE_PORT_RECEIVE:
      /* We have got a receive right, call it A and the send wrapper for
       * the destination task is denoted as B (if the destination task
       * doesn't have the send wrapper, we create it before moving receive
       * right).
       * We wrap the receive right A in the send wrapper and move the receive
       * right B to the destination task.  */
      {
	assert_backtrace (req);
	receiver_info = hurd_ihash_find (&traced_names, *right);
	if (receiver_info)
	  {
	    struct sender_info *send_wrapper2;
	    char *name;
	    mach_port_t rr;

	    /* The port A has at least one send right - the one in
	     * receiver_info->forward. If the source task doesn't have
	     * the send right, the port A will be destroyed after we
	     * deallocate the only send right. */

	    /* We have to deallocate the send right in
	     * receiver_info->forward before we import the port to port_info.
	     * So the reference count in the imported port info will be 1,
	     * if it doesn't have any other send rights. */
	    mach_port_deallocate (mach_task_self (), receiver_info->forward);
	    err = ports_import_port (traced_class, traced_bucket,
				     *right, sizeof *send_wrapper,
				     &send_wrapper);
	    if (err)
	      error (2, err, "ports_import_port");

	    TRACED_INFO (send_wrapper)->type = MACH_MSG_TYPE_MOVE_SEND;
	    send_wrapper->task = source;
	    TRACED_INFO (send_wrapper)->name = receiver_info->name;
	    /* Initialize them in case that the source task doesn't
	     * have the send right to the port, and the port will
	     * be destroyed immediately. */
	    send_wrapper->receive_right = NULL;
	    send_wrapper->next = NULL;
	    ports_port_deref (send_wrapper);

	    hurd_ihash_locp_remove (&traced_names, receiver_info->locp);

	    send_wrapper2 = get_send_wrapper (receiver_info, dest, &rr);
	    assert_backtrace (
	      refcounts_hard_references (
		&TRACED_INFO (send_wrapper2)->pi.refcounts)
	      == 1);

	    name = TRACED_INFO (send_wrapper2)->name;
	    TRACED_INFO (send_wrapper2)->name = NULL;
	    /* send_wrapper2 isn't destroyed normally, so we need to unlink
	     * it from the send wrapper list before calling ports_claim_right */
	    unlink_sender_info (send_wrapper2);
	    send_wrapper2->receive_right = NULL;
	    rr = ports_claim_right (send_wrapper2);
	    /* Get us a send right that we will forward on.  */
	    err = mach_port_insert_right (mach_task_self (), rr, rr,
					  MACH_MSG_TYPE_MAKE_SEND);
	    if (err)
	      error (2, err, "mach_port_insert_right");
	    receiver_info->forward = rr;
	    receiver_info->task = dest;
	    if (dest != unknown_task)
	      {
		receiver_info->receive_right = receive_right_list;
		receive_right_list = receiver_info;
	      }
	    /* The port name will be discovered
	     * when we search for this receive right. */
	    receiver_info->portname = UNKNOWN_NAME;
	    receiver_info->name = name;

	    send_wrapper->receive_right = receiver_info;
	    send_wrapper->next = receiver_info->next;
	    receiver_info->next = send_wrapper;

	    err = hurd_ihash_add (&traced_names, receiver_info->forward,
				  receiver_info);
	    if (err)
	      error (2, err, "hurd_ihash_add");
	    *right = rr;
	  }
	else
	  {
	    /* Weird? no send right for the port. */
	    err = mach_port_insert_right (mach_task_self (), *right, *right,
					  MACH_MSG_TYPE_MAKE_SEND);
	    if (err)
	      error (2, err, "mach_port_insert_right");
	    receiver_info = new_receiver_info (*right, dest);
	  }

	return receiver_info->name;
      }

    default:
      assert_backtrace (!"??? bogus port type from kernel!");
    }
  return 0;
}

static void
print_contents (mach_msg_header_t *inp,
		void *msg_buf_ptr, struct req_info *req)
{
  error_t err;

  int first = 1;

  /* Process the message data, wrapping ports and printing data.  */
  while (msg_buf_ptr < (void *) inp + inp->msgh_size)
    {
      mach_msg_type_t *const type = msg_buf_ptr;
      mach_msg_type_long_t *const lt = (void *) type;
      void *data;
      mach_msg_type_number_t nelt; /* Number of data items.  */
      mach_msg_type_size_t eltsize; /* Bytes per item.  */
      mach_msg_type_name_t name; /* MACH_MSG_TYPE_* code */

      if (!type->msgt_longform)
	{
	  name = type->msgt_name;
	  nelt = type->msgt_number;
	  eltsize = type->msgt_size / 8;
	  data = msg_buf_ptr = type + 1;
	}
      else
	{
	  name = lt->msgtl_name;
	  nelt = lt->msgtl_number;
	  eltsize = lt->msgtl_size / 8;
	  data = msg_buf_ptr = lt + 1;
	}

      if (!type->msgt_inline)
	{
	  /* This datum is out-of-line, meaning the message actually
	     contains a pointer to a vm_allocate'd region of data.  */
	  data = *(void **) data;
	  msg_buf_ptr += sizeof (void *);
	}
      else
	msg_buf_ptr += ((nelt * eltsize + sizeof(natural_t) - 1)
			& ~(sizeof(natural_t) - 1));

      if (first)
	first = 0;
      else
	putc (' ', ostream);

      /* Note that MACH_MSG_TYPE_PORT_NAME does not indicate a port right.
	 It indicates a port name, i.e. just an integer--and we don't know
	 what task that port name is meaningful in.  If it's meaningful in
	 a traced task, then it refers to our intercepting port rather than
	 the original port anyway.  */
      if (MACH_MSG_TYPE_PORT_ANY_RIGHT (name))
	{
	  /* These are port rights.  Translate them into wrappers.  */
	  mach_port_t *const portnames = data;
	  mach_msg_type_number_t i;
	  mach_msg_type_name_t newtypes[nelt];
	  int poly;

	  assert_backtrace (inp->msgh_bits & MACH_MSGH_BITS_COMPLEX);
	  assert_backtrace (eltsize == sizeof (mach_port_t));

	  poly = 0;
	  for (i = 0; i < nelt; ++i)
	    {
	      char *str;

	      newtypes[i] = name;

	      str = rewrite_right (&portnames[i], &newtypes[i], req);

	      putc ((i == 0 && nelt > 1) ? '{' : ' ', ostream);

	      if (portnames[i] == MACH_PORT_NULL)
		fprintf (ostream, "(null)");
	      else if (portnames[i] == MACH_PORT_DEAD)
		fprintf (ostream, "(dead)");
	      else
		{
		  if (str != 0)
		    fprintf (ostream, "%s", str);
		  else
		    fprintf (ostream, "%3u", (unsigned int) portnames[i]);
		}
	      if (i > 0 && newtypes[i] != newtypes[0])
		poly = 1;
	    }
	  if (nelt > 1)
	    putc ('}', ostream);

	  if (poly)
	    {
	      if (name == MACH_MSG_TYPE_MOVE_SEND_ONCE)
		{
		  /* Some of the new rights are MAKE_SEND_ONCE.
		     Turn them all into MOVE_SEND_ONCE.  */
		  for (i = 0; i < nelt; ++i)
		    if (newtypes[i] == MACH_MSG_TYPE_MAKE_SEND_ONCE)
		      {
			err = mach_port_insert_right (mach_task_self (),
						      portnames[i],
						      portnames[i],
						      newtypes[i]);
			assert_perror_backtrace (err);
		      }
		    else
		      assert_backtrace (newtypes[i] == MACH_MSG_TYPE_MOVE_SEND_ONCE);
		}
	      else
		{
		  for (i = 0; i < nelt; ++i)
		    switch (newtypes[i])
		      {
		      case MACH_MSG_TYPE_COPY_SEND:
			err = mach_port_mod_refs (mach_task_self (),
						  portnames[i],
						  MACH_PORT_RIGHT_SEND, +1);
			assert_perror_backtrace (err);
			break;
		      case MACH_MSG_TYPE_MAKE_SEND:
			err = mach_port_insert_right (mach_task_self (),
						      portnames[i],
						      portnames[i],
						      newtypes[i]);
			assert_perror_backtrace (err);
			break;
		      default:
			assert_backtrace (newtypes[i] == MACH_MSG_TYPE_MOVE_SEND);
			break;
		      }

		  name = MACH_MSG_TYPE_MOVE_SEND;
		}
	      if (type->msgt_longform)
		lt->msgtl_name = name;
	      else
		type->msgt_name = name;
	    }
	  else if (nelt > 0 && newtypes[0] != name)
	    {
	      if (type->msgt_longform)
		lt->msgtl_name = newtypes[0];
	      else
		type->msgt_name = newtypes[0];
	    }
	}
      else
	print_data (name, data, nelt, eltsize);
    }
}

/* Wrap all thread ports in the task */
static void
wrap_all_threads (task_t task)
{
  struct sender_info *thread_send_wrapper;
  struct receiver_info *thread_receiver_info;
  thread_t *threads;
  size_t nthreads;
  error_t err;

  err = task_threads (task, &threads, &nthreads);
  if (err)
    error (2, err, "task_threads");

  for (int i = 0; i < nthreads; ++i)
    {
      thread_receiver_info = hurd_ihash_find (&traced_names, threads[i]);
      /* We haven't seen the port. */
      if (thread_receiver_info == NULL)
	{
	  mach_port_t new_thread_port;

	  thread_receiver_info = new_receiver_info (threads[i], unknown_task);
	  thread_send_wrapper = new_send_wrapper (thread_receiver_info,
						  task, &new_thread_port);
	  free (TRACED_INFO (thread_send_wrapper)->name);
	  asprintf (&TRACED_INFO (thread_send_wrapper)->name,
		    "thread%lu(pid%d)", threads[i], task2pid (task));

	  err = mach_port_insert_right (mach_task_self (),
					new_thread_port, new_thread_port,
					MACH_MSG_TYPE_MAKE_SEND);
	  if (err)
	    error (2, err, "mach_port_insert_right");

	  err = thread_set_kernel_port (threads[i], new_thread_port);
	  if (err)
	    error (2, err, "thread_set_kernel_port");

	  mach_port_deallocate (mach_task_self (), new_thread_port);
	}
    }
  vm_deallocate (mach_task_self (), (vm_address_t) threads,
                 nthreads * sizeof (thread_t));
}

/* Wrap the new thread port that is in the message. */
static void
wrap_new_thread (mach_msg_header_t *inp, struct req_info *req)
{
  error_t err;
  mach_port_t thread_port;
  struct
    {
      mach_msg_header_t head;
      mach_msg_type_t retcode_type;
      kern_return_t retcode;
      mach_msg_type_t child_thread_type;
      mach_port_t child_thread;
    } *reply = (void *) inp;
  /* This function is called after rewrite_right,
   * so the wrapper for the thread port has been created. */
  struct sender_info *send_wrapper = ports_lookup_port (traced_bucket,
							reply->child_thread, 0);

  assert_backtrace (send_wrapper);
  assert_backtrace (send_wrapper->receive_right);
  thread_port = send_wrapper->receive_right->forward;

  err = mach_port_insert_right (mach_task_self (), reply->child_thread,
				reply->child_thread, MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    error (2, err, "mach_port_insert_right");
  err = thread_set_kernel_port (thread_port, reply->child_thread);
  if (err)
    error (2, err, "thread_set_kernel_port");
  mach_port_deallocate (mach_task_self (), reply->child_thread);

  free (TRACED_INFO (send_wrapper)->name);
  asprintf (&TRACED_INFO (send_wrapper)->name, "thread%lu(pid%d)",
	    thread_port, task2pid (req->from));
  ports_port_deref (send_wrapper);
}

/* Wrap the new task port that is in the message. */
static void
wrap_new_task (mach_msg_header_t *inp, struct req_info *req)
{
  error_t err;
  pid_t pid;
  task_t pseudo_task_port;
  task_t task_port;
  struct
    {
      mach_msg_header_t head;
      mach_msg_type_t retcode_type;
      kern_return_t retcode;
      mach_msg_type_t child_task_type;
      mach_port_t child_task;
    } *reply = (void *) inp;
  /* The send wrapper of the new task for the father task */
  struct sender_info *task_wrapper1 = ports_lookup_port (traced_bucket,
						       reply->child_task, 0);
  /* The send wrapper for the new task itself. */
  struct sender_info *task_wrapper2;

  assert_backtrace (task_wrapper1);
  assert_backtrace (task_wrapper1->receive_right);

  task_port = task_wrapper1->receive_right->forward;
  add_task (task_port);

  task_wrapper2 = new_send_wrapper (task_wrapper1->receive_right,
				    task_port, &pseudo_task_port);
  err = mach_port_insert_right (mach_task_self (),
				pseudo_task_port, pseudo_task_port,
				MACH_MSG_TYPE_MAKE_SEND);
  if (err)
    error (2, err, "mach_port_insert_right");
  err = task_set_kernel_port (task_port, pseudo_task_port);
  if (err)
    error (2, err, "task_set_kernel_port");
  mach_port_deallocate (mach_task_self (), pseudo_task_port);

  pid = task2pid (task_port);
  free (TRACED_INFO (task_wrapper1)->name);
  asprintf (&TRACED_INFO (task_wrapper1)->name, "task%lu(pid%d)",
	    task_port, task2pid (req->from));
  free (TRACED_INFO (task_wrapper2)->name);
  asprintf (&TRACED_INFO (task_wrapper2)->name, "task%lu(pid%d)",
	    task_port, pid);
  ports_port_deref (task_wrapper1);
}

/* Returns true if the given message is a Mach notification.  */
static inline int
is_notification (const mach_msg_header_t *InHeadP)
{
  int msgh_id = InHeadP->msgh_id - 64;
  if ((msgh_id > 8) || (msgh_id < 0))
    return 0;
  return 1;
}

int
trace_and_forward (mach_msg_header_t *inp, mach_msg_header_t *outp)
{
  mach_port_t reply_port;

  const mach_msg_type_t RetCodeType =
  {
    MACH_MSG_TYPE_INTEGER_32,	/* msgt_name = */
    32,				/* msgt_size = */
    1,				/* msgt_number = */
    TRUE,			/* msgt_inline = */
    FALSE,			/* msgt_longform = */
    FALSE,			/* msgt_deallocate = */
    0				/* msgt_unused = */
  };

  error_t err;
  const struct msgid_info *msgid;
  struct traced_info *info;
  mach_msg_bits_t complex;

  /* Look up our record for the receiving port.  There is no need to check
     the class, because our port bucket only ever contains one class of
     ports (traced_class).  */

  if (MACH_MSGH_BITS_LOCAL (inp->msgh_bits) == MACH_MSG_TYPE_PROTECTED_PAYLOAD)
    {
      info = ports_lookup_payload (traced_bucket, inp->msgh_protected_payload,
				   NULL);
      if (info)
	{
	  /* Undo the protected payload optimization.  */
	  inp->msgh_bits = MACH_MSGH_BITS (
	    MACH_MSGH_BITS_REMOTE (inp->msgh_bits),
	    is_notification (inp)? MACH_MSG_TYPE_MOVE_SEND_ONCE: info->type)
	    | MACH_MSGH_BITS_OTHER (inp->msgh_bits);
	  inp->msgh_local_port = ports_payload_get_name ((unsigned int) info);
	}
    }
  else
    info = ports_lookup_port (traced_bucket, inp->msgh_local_port, NULL);

  assert_backtrace (info);

  /* A notification message from the kernel appears to have been sent
     with a send-once right, even if there have never really been any.  */
  if (MACH_MSGH_BITS_LOCAL (inp->msgh_bits) == MACH_MSG_TYPE_MOVE_SEND_ONCE)
    {
      if (inp->msgh_id == MACH_NOTIFY_DEAD_NAME && info == (void *) notify_pi)
	{
	  struct receiver_info *receiver_info;
	  const mach_dead_name_notification_t *const n = (void *) inp;

	  /* Deallocate extra ref allocated by the notification.  */
	  mach_port_deallocate (mach_task_self (), n->not_port);
	  receiver_info = hurd_ihash_find (&traced_names, n->not_port);
	  /* The receiver info might have been destroyed.
	   * If not, we destroy it here. */
	  if (receiver_info)
	    {
	      assert_backtrace (n->not_port == receiver_info->forward);
	      destroy_receiver_info (receiver_info);
	    }

	  ((mig_reply_header_t *) outp)->RetCode = MIG_NO_REPLY;
	  ports_port_deref (info);
	  
	  /* It might be a task port. Remove the dead task from the list. */
	  remove_task (n->not_port);

	  return 1;
	}
      else if (inp->msgh_id == MACH_NOTIFY_NO_SENDERS
	       && !INFO_SEND_ONCE (info))
	{
	  /* No more senders for a send right we are tracing.  Now INFO
	     will die, and we will release the tracee send right so it too
	     can see a no-senders notification.  */
	  mach_no_senders_notification_t *n = (void *) inp;
	  ports_no_senders (info, n->not_count);
	  ports_port_deref (info);
	  ((mig_reply_header_t *) outp)->RetCode = MIG_NO_REPLY;
	  return 1;
	}
      /* Get some unexpected notification for rpctrace itself,
       * TODO ignore them for now. */
      else if (info == (void *) notify_pi)
	{
	  ports_port_deref (info);
	  ((mig_reply_header_t *) outp)->RetCode = MIG_NO_REPLY;
	  return 1;
	}
    }

  assert_backtrace (info != (void *) notify_pi);
  assert_backtrace (MACH_MSGH_BITS_LOCAL (inp->msgh_bits) == info->type);

  complex = inp->msgh_bits & MACH_MSGH_BITS_COMPLEX;

  msgid = msgid_info (inp->msgh_id);

  /* Swap the header data like a crossover cable. */
  {
    mach_msg_type_name_t this_type = MACH_MSGH_BITS_LOCAL (inp->msgh_bits);
    mach_msg_type_name_t reply_type = MACH_MSGH_BITS_REMOTE (inp->msgh_bits);
    
    /* Save the original reply port in the RPC request. */
    reply_port = inp->msgh_remote_port;

    inp->msgh_local_port = inp->msgh_remote_port;
    if (reply_type && msgid_trace_replies (msgid)
	/* The reply port might be dead, e.g., the traced task has died. */
	&& MACH_PORT_VALID (inp->msgh_local_port))
      {
	switch (reply_type)
	  {
	  case MACH_MSG_TYPE_PORT_SEND:
	    rewrite_right (&inp->msgh_local_port, &reply_type, NULL);
	    break;

	  case MACH_MSG_TYPE_PORT_SEND_ONCE:;
	    struct send_once_info *info;
	    info = new_send_once_wrapper (inp->msgh_local_port,
					  &inp->msgh_local_port);
	    reply_type = MACH_MSG_TYPE_MAKE_SEND_ONCE;
	    assert_backtrace (inp->msgh_local_port);

	    if (TRACED_INFO (info)->name == 0)
	      {
		if (msgid == 0)
		  asprintf (&TRACED_INFO (info)->name, "reply(%u:%u)",
			    (unsigned int) TRACED_INFO (info)->pi.port_right,
			    (unsigned int) inp->msgh_id);
		else
		  asprintf (&TRACED_INFO (info)->name, "reply(%u:%s)",
			    (unsigned int) TRACED_INFO (info)->pi.port_right,
			    msgid->name);
	      }
	    break;

	  default:
	    error (1, 0, "Reply type %i not handled", reply_type);
	  }
      }

    if (info->type == MACH_MSG_TYPE_MOVE_SEND_ONCE)
      inp->msgh_remote_port = SEND_ONCE_INFO (info)->forward;
    else
      {
	assert_backtrace (SEND_INFO (info)->receive_right);
	inp->msgh_remote_port = SEND_INFO (info)->receive_right->forward;
      }
    if (this_type == MACH_MSG_TYPE_MOVE_SEND_ONCE)
      {
	/* We have a message to forward for a send-once wrapper object.
	   Since each wrapper object only lives for a single message, this
	   one can be reclaimed now.  We continue to hold a hard ref to the
	   ports object, but we know that nothing else refers to it now, and
	   we are consuming its `forward' right in the message we send.  */
	free (info->name);
	info->name = 0;
	SEND_ONCE_INFO (info)->forward = 0;
	SEND_ONCE_INFO (info)->nextfree = freelist;
	freelist = SEND_ONCE_INFO (info);
      }
    else
      this_type = MACH_MSG_TYPE_COPY_SEND;

    inp->msgh_bits = complex | MACH_MSGH_BITS (this_type, reply_type);
  }

  /* The message now appears as it would if we were the sender.
     It is ready to be resent.  */

  if (msgid_display (msgid))
    {
      if (inp->msgh_local_port == MACH_PORT_NULL
	  && info->type == MACH_MSG_TYPE_MOVE_SEND_ONCE
	  && inp->msgh_size >= sizeof (mig_reply_header_t)
	  /* The notification message is considered as a request. */
	  && (inp->msgh_id > 72 || inp->msgh_id < 64)
          && !memcmp(&((mig_reply_header_t *) inp)->RetCodeType,
                     &RetCodeType, sizeof (RetCodeType)))
	{
	  struct req_info *req = remove_request (inp->msgh_id - 100,
						 inp->msgh_remote_port);
	  assert_backtrace (req);
	  req->is_req = FALSE;
	  /* This sure looks like an RPC reply message.  */
	  mig_reply_header_t *rh = (void *) inp;
	  print_reply_header ((struct send_once_info *) info, rh, req);
	  putc (' ', ostream);
	  fflush (ostream);
	  print_contents (&rh->Head, rh + 1, req);
	  putc ('\n', ostream);

	  if (inp->msgh_id == 2161)/* the reply message for thread_create */
	    wrap_new_thread (inp, req);
	  else if (inp->msgh_id == 2107) /* for task_create */
	    wrap_new_task (inp, req);

	  free (req);
	}
      else
	{
	  struct task_info *task_info;
	  task_t to = 0;
	  struct req_info *req = NULL;

	  /* Print something about the message header.  */
	  print_request_header ((struct sender_info *) info, inp);
	  /* It's a notification message. */
	  if (inp->msgh_id <= 72 && inp->msgh_id >= 64)
	    {
	      assert_backtrace (info->type == MACH_MSG_TYPE_MOVE_SEND_ONCE);
	      /* mach_notify_port_destroyed message has a port,
	       * TODO how do I handle it? */
	      assert_backtrace (inp->msgh_id != 69);
	    }

	  /* If it's mach_port RPC,
	   * the port rights in the message will be moved to the target task. */
	  else if (inp->msgh_id >= 3200 && inp->msgh_id <= 3218)
	    to = SEND_INFO (info)->receive_right->forward;
	  else
	    to = SEND_INFO (info)->receive_right->task;
	  if (info->type == MACH_MSG_TYPE_MOVE_SEND)
	    req = add_request (inp->msgh_id, reply_port,
			       SEND_INFO (info)->task, to);

	  /* If it's the notification message, req is NULL.
	   * TODO again, it's difficult to handle mach_notify_port_destroyed */
	  print_contents (inp, inp + 1, req);
	  if (inp->msgh_local_port == MACH_PORT_NULL) /* simpleroutine */
	    {
	      /* If it's a simpleroutine,
	       * we don't need the request information any more. */
	      req = remove_request (inp->msgh_id, reply_port);
	      free (req);
	      fprintf (ostream, ");\n");
	    }
	  else
	    /* Leave a partial line that will be finished later.  */
	    fprintf (ostream, ")");
	  fflush (ostream);

	  /* If it's the first request from the traced task,
	   * wrap the all threads in the task. */
	  task_info = hurd_ihash_find (&task_ihash, SEND_INFO (info)->task);
	  if (task_info && !task_info->threads_wrapped)
	    {
	      wrap_all_threads (SEND_INFO (info)->task);
	      task_info->threads_wrapped = TRUE;
	    }
	}
    }

  /* Resend the message to the tracee.  */
  err = mach_msg (inp, MACH_SEND_MSG, inp->msgh_size, 0,
		  MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
  if (err == MACH_SEND_INVALID_DEST)
    {
      /* The tracee port died.  No doubt we are about to receive the dead-name
	 notification.  */
      /* XXX MAKE_SEND, MAKE_SEND_ONCE rights in msg not handled */
      mach_msg_destroy (inp);
    }
  else
    assert_perror_backtrace (err);

  ports_port_deref (info);

  /* We already sent the message, so the server loop shouldn't do it again.  */
  ((mig_reply_header_t *) outp)->RetCode = MIG_NO_REPLY;

  return 1;
}

/* This function runs in the tracing thread and drives all the tracing.  */
static void *
trace_thread_function (void *arg)
{
  struct port_bucket *const bucket = arg;
  ports_manage_port_operations_one_thread (bucket, trace_and_forward, 0);
  return 0;
}

/*** Output formatting ***/

#if 0
struct msg_type
{
  const char *name;
  const char *letter;
};

static const char *const msg_types[] =
{
  [MACH_MSG_TYPE_BIT]		= {"bool", "b"},
  [MACH_MSG_TYPE_INTEGER_16]	= {"int16", "h"},
  [MACH_MSG_TYPE_INTEGER_32]	= {"int32", "i"},
  [MACH_MSG_TYPE_CHAR]		= {"char", "c"},
  [MACH_MSG_TYPE_INTEGER_8]	= {"int8", "B"},
  [MACH_MSG_TYPE_REAL]		= {"float", "f"},
  [MACH_MSG_TYPE_INTEGER_64]	= {"int64", "q"},
  [MACH_MSG_TYPE_STRING]	= {"string", "s"},
  [MACH_MSG_TYPE_MOVE_RECEIVE]	= {"move-receive", "R"},
  [MACH_MSG_TYPE_MOVE_SEND]	= {"move-send", "S"},
  [MACH_MSG_TYPE_MOVE_SEND_ONCE]= {"move-send-once", "O"},
  [MACH_MSG_TYPE_COPY_SEND]	= {"copy-send", "s"},
  [MACH_MSG_TYPE_MAKE_SEND]	= {"make-send", ""},
  [MACH_MSG_TYPE_MAKE_SEND_ONCE]= {"make-send-once", ""},
  [MACH_MSG_TYPE_PORT_NAME]	= {"port-name", "n"},
};
#endif

/* We keep track of the last reply port used in a request we print to
   ostream.  This way we can end incomplete requests with an ellipsis
   and the name of the reply port.  When the reply finally arrives, we
   start a new line with that port name and an ellipsis, making it
   easy to match it to the associated request.  */
static mach_port_t last_reply_port;

/* Print an ellipsis if necessary.  */
static void
print_ellipsis (void)
{
  if (MACH_PORT_VALID (last_reply_port))
    fprintf (ostream, " ...%u\n", (unsigned int) last_reply_port);
}

static void
print_request_header (struct sender_info *receiver, mach_msg_header_t *msg)
{
  const char *msgname = msgid_name (msg->msgh_id);
  print_ellipsis ();
  last_reply_port = msg->msgh_local_port;

  if (TRACED_INFO (receiver)->name != 0)
    fprintf (ostream, "%4s->", TRACED_INFO (receiver)->name);
  else
    fprintf (ostream, "%4u->",
	     (unsigned int) TRACED_INFO (receiver)->pi.port_right);

  if (msgname != 0)
    fprintf (ostream, "%5s (", msgname);
  else
    fprintf (ostream, "%5u (", (unsigned int) msg->msgh_id);
}

static void
print_reply_header (struct send_once_info *info, mig_reply_header_t *reply,
		    struct req_info *req)
{
  if (last_reply_port != info->pi.pi.port_right)
    {
      print_ellipsis ();
      fprintf (ostream, "%u...", (unsigned int) info->pi.pi.port_right);
    }
  last_reply_port = MACH_PORT_NULL;

  /* We have printed a partial line for the request message,
     and now we have the corresponding reply.  */
  if (reply->Head.msgh_id == req->req_id + 100)
    fprintf (ostream, " = "); /* normal case */
  else
    /* This is not the proper reply message ID.  */
    fprintf (ostream, " =(%u != %u) ",
	     reply->Head.msgh_id, req->req_id + 100);

  if (reply->RetCode == 0)
    fprintf (ostream, "0");
  else
    {
      const char *str = strerror (reply->RetCode);
      if (str == 0)
	fprintf (ostream, "%#x", reply->RetCode);
      else
	fprintf (ostream, "%#x (%s)", reply->RetCode, str);
    }
}

static char escape_sequences[0x100] =
  {
    ['\0'] = '0',
    ['\a'] = 'a',
    ['\b'] = 'b',
    ['\f'] = 'f',
    ['\n'] = 'n',
    ['\r'] = 'r',
    ['\t'] = 't',
    ['\v'] = 'v',
    ['\\'] = '\\',
    ['\''] = '\'',
    ['"'] = '"',
  };

static void
print_data (mach_msg_type_name_t type,
	    const void *data,
	    mach_msg_type_number_t nelt,
	    mach_msg_type_number_t eltsize)
{
  switch (type)
    {
    case MACH_MSG_TYPE_PORT_NAME:
      assert_backtrace (eltsize == sizeof (mach_port_t));
      {
	mach_msg_type_number_t i;
	fprintf (ostream, "pn{");
	for (i = 0; i < nelt; ++i)
	  {
	    fprintf (ostream, "%*u", (i > 0) ? 4 : 3,
		     (unsigned int) ((mach_port_t *) data)[i]);
	  }
	fprintf (ostream, "}");
	return;
      }

    case MACH_MSG_TYPE_STRING:
    case MACH_MSG_TYPE_CHAR:
      if (nelt > strsize)
	nelt = strsize;
      fprintf (ostream, "\"");
      /* Scan data for non-printable characters.  p always points to
	 the first character that has not yet been printed.  */
      const char *p, *q;
      p = q = (const char *) data;
      while (q && q - (const char *) data < (int) (nelt * eltsize)
	     && (*q || type == MACH_MSG_TYPE_CHAR))
	{
	  if (isgraph (*q) || *q == ' ')
	    {
	      q += 1;
	      continue;
	    }

	  /* We encountered a non-printable character.  Print anything
	     that has not been printed so far.  */
	  if (p < q)
	    fprintf (ostream, "%.*s", q - p, p);

	  char c = escape_sequences[*((const unsigned char *) q)];
	  if (c)
	    fprintf (ostream, "\\%c", c);
	  else
	    fprintf (ostream, "\\x%02x", *((const unsigned char *) q));

	  q += 1;
	  p = q;
	}

      /* Print anything that has not been printed so far.  */
      if (p < q)
	fprintf (ostream, "%.*s", q - p, p);
      fprintf (ostream, "\"");
      return;

#if 0
    case MACH_MSG_TYPE_CHAR:
      if (eltsize == 1)
	FMT ("'%c'", unsigned char);
      break;
#endif

#define FMT(fmt, ctype) do {						      \
	mach_msg_type_number_t i;					      \
	for (i = 0; i < nelt; ++i)					      \
	  {								      \
	    fprintf (ostream, "%s" fmt,					      \
		     (i == 0 && nelt > 1) ? "{" : i > 0 ? " " : "",	      \
		     *(const ctype *) data);				      \
	    data += eltsize;						      \
	  }								      \
	if (nelt > 1)							      \
	  putc ('}', ostream);						      \
        return;								      \
      } while (0)

    case MACH_MSG_TYPE_BIT:
    case MACH_MSG_TYPE_INTEGER_8:
    case MACH_MSG_TYPE_INTEGER_16:
    case MACH_MSG_TYPE_INTEGER_32:
    case MACH_MSG_TYPE_INTEGER_64:
      switch (eltsize)
	{
	case 1:				FMT ("%"PRId8, int8_t);
	case 2:				FMT ("%"PRId16, int16_t);
	case 4:				FMT ("%"PRId32, int32_t);
	case 8:				FMT ("%"PRId64, int64_t);
	}
      break;

    case MACH_MSG_TYPE_REAL:
      if (eltsize == sizeof (float))
	FMT ("%g", float);
      else if (eltsize == sizeof (double))
	FMT ("%g", double);
      else if (eltsize == sizeof (long double))
	FMT ("%Lg", long double);
      else
	abort ();
      break;
    }

  /* XXX */
  fprintf (ostream, "\t%#x (type %d, %d*%d)\n", *(const int *)data, type,
	   nelt, eltsize);
}


/*** Main program and child startup ***/


/* Run a child and have it do more or else `execvpe (argv, envp);'.  */
pid_t
traced_spawn (char **argv, char **envp)
{
  error_t err;
  pid_t pid;
  mach_port_t task_wrapper;
  task_t traced_task;
  struct sender_info *ti;
  struct receiver_info *receive_ti;
  char *prefixed_name;
  file_t file = file_name_path_lookup (argv[0], getenv ("PATH"),
				       O_EXEC, 0, &prefixed_name);

  if (file == MACH_PORT_NULL)
    error (1, errno, "command not found: %s", argv[0]);

  err = task_create (mach_task_self (),
#ifdef KERN_INVALID_LEDGER
		     NULL, 0,	/* OSF Mach */
#endif
		     0, &traced_task);
  assert_perror_backtrace (err);

  add_task (traced_task);
  /* Declare the new task to be our child.  This is what a fork does.  */
  err = proc_child (getproc (), traced_task);
  if (err)
    error (2, err, "proc_child");
  pid = task2pid (traced_task);
  if (pid < 0)
    error (2, errno, "task2pid");

  receive_ti = new_receiver_info (traced_task, unknown_task);
  /* Create a trace wrapper for the task port.  */
  ti = new_send_wrapper (receive_ti, traced_task, &task_wrapper);
  ti->task = traced_task;
  free (TRACED_INFO (ti)->name);
  asprintf (&TRACED_INFO (ti)->name, "task%lu(pid%d)", traced_task, pid);

  /* Replace the task's kernel port with the wrapper.  When this task calls
     `mach_task_self ()', it will get our wrapper send right instead of its
     own real task port.  */
  err = mach_port_insert_right (mach_task_self (), task_wrapper,
				task_wrapper, MACH_MSG_TYPE_MAKE_SEND);
  assert_perror_backtrace (err);
  err = task_set_special_port (traced_task, TASK_KERNEL_PORT, task_wrapper);
  assert_perror_backtrace (err);

  /* Now actually run the command they told us to trace.  We do the exec on
     the actual task, so the RPCs to map in the program itself do not get
     traced.  Could have an option to use TASK_WRAPPER here instead.  */
#ifdef HAVE__HURD_EXEC_PATHS
  err = _hurd_exec_paths (traced_task, file, prefixed_name ?: *argv,
			  prefixed_name ?: *argv, argv, envp);
#else
  err = _hurd_exec (traced_task, file, argv, envp);
#endif
  if (err)
    error (2, err, "cannot exec `%s'", argv[0]);

  /* We were keeping this send right alive so that the wrapper object
     cannot die and hence our TRACED_TASK ref cannot have been released.  */
  mach_port_deallocate (mach_task_self (), task_wrapper);

  free (prefixed_name);
  return pid;
}

int
main (int argc, char **argv, char **envp)
{
  const char *outfile = 0;
  char **cmd_argv = 0;
  pthread_t thread;
  error_t err;
  char **cmd_envp = NULL;
  char *envz = NULL;
  size_t envz_len = 0;

  /* Parse our options...  */
  error_t parse_opt (int key, char *arg, struct argp_state *state)
    {
      switch (key)
	{
	case 'o':
	  outfile = arg;
	  break;

	case 's':
	  strsize = atoi (arg);
	  break;

	case 'E':
	  if (envz == NULL)
	    {
	      if (argz_create (envp, &envz, &envz_len))
		error (1, errno, "argz_create");
	    }
	  if (envz != NULL)
	    {
	      char *equal = strchr (arg, '=');
	      char *name;
	      char *newval;
	      if (equal != NULL)
		{
		  name = strndupa (arg, equal - arg);
		  if (name == NULL)
		    error (1, errno, "strndupa");
		  newval = equal + 1;
		}
	      else
		{
		  name = arg;
		  newval = NULL;
		}
	      if (envz_add (&envz, &envz_len, name, newval))
		error (1, errno, "envz_add");
	    }
	  break;

	case ARGP_KEY_NO_ARGS:
	  argp_usage (state);
	  return EINVAL;

	case ARGP_KEY_ARG:
	  cmd_argv = &state->argv[state->next - 1];
	  state->next = state->argc;
	  break;

	default:
	  return ARGP_ERR_UNKNOWN;
	}
      return 0;
    }
  const struct argp_child children[] =
    {
      { .argp=&msgid_argp, },
      { 0 }
    };
  const struct argp argp = { options, parse_opt, args_doc, doc, children };

  /* Parse our arguments.  */
  argp_parse (&argp, argc, argv, ARGP_IN_ORDER, 0, 0);

  err = mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_DEAD_NAME,
			    &unknown_task);
  assert_perror_backtrace (err);

  if (outfile)
    {
      ostream = fopen (outfile, "w");
      if (!ostream)
	error (1, errno, "%s", outfile);
    }
  else
    ostream = stderr;
  setlinebuf (ostream);

  traced_bucket = ports_create_bucket ();
  traced_class = ports_create_class (&traced_clean, NULL);
  other_class = ports_create_class (0, 0);
  err = ports_create_port (other_class, traced_bucket,
			   sizeof (*notify_pi), &notify_pi);
  assert_perror_backtrace (err);

  /* Spawn a single thread that will receive intercepted messages, print
     them, and interpose on the ports they carry.  The access to the
     `traced_info' and ihash data structures is all single-threaded,
     happening only in this new thread.  */
  err = pthread_create (&thread, NULL, trace_thread_function, traced_bucket);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }

  if (envz != NULL)
    {
      envz_strip (&envz, &envz_len);
      cmd_envp = alloca ((argz_count (envz, envz_len) + 1) * sizeof (char *));
      if (cmd_envp == NULL)
	error (1, errno, "alloca");
      else
	argz_extract (envz, envz_len, cmd_envp);
    }
  if (cmd_envp == NULL)
    cmd_envp = envp;

  /* Run the program on the command line and wait for it to die.
     The other thread does all the tracing and interposing.  */
  {
    pid_t child, pid;
    int status;
    child = traced_spawn (cmd_argv, cmd_envp);
    pid = waitpid (child, &status, 0);
    sleep (1);			/* XXX gives other thread time to print */
    if (pid != child)
      error (1, errno, "waitpid");
    if (WIFEXITED (status))
      fprintf (ostream, "Child %d exited with %d\n",
	       pid, WEXITSTATUS (status));
    else
      fprintf (ostream, "Child %d %s\n", pid, strsignal (WTERMSIG (status)));
  }
  
  ports_destroy_right (notify_pi);
  free (envz);

  return 0;
}
