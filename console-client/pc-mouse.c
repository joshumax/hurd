/* pc-mouse.c - Mouse driver.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.
   Written by Marco Gerards.

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

#include <argp.h>
#include <hurd.h>
#include <hurd/ports.h>
#include <device/device.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include "driver.h"
#include "mach-inputdev.h"

static struct input_ops pc_mouse_ops;

/* Default to the protocol I use :).  */
static int majordev = IBM_MOUSE;
static int minordev = 0;

static device_t mousedev;


/* The default name of the node of the repeater.  */
#define DEFAULT_REPEATER_NODE	"mouse"

/* The amount of mouse events that can be stored in the event buffer.  */
#define MOUSEDEVTBUFSZ	256

/* The size of the event buffer in bytes.  */
#define MOUSEBUFSZ	(MOUSEDEVTBUFSZ * sizeof (kd_event))

/* Return the position of X in the buffer.  */
#define MOUSEBUF_POS(x)	((x) % MOUSEBUFSZ)

/* The mouse sensitivity.  */
#define STRINGIFY(x) STRINGIFY_1(x)
#define STRINGIFY_1(x) #x
#define DEFAULT_MOUSE_SENS 1.0
#define DEFAULT_MOUSE_SENS_STRING STRINGIFY(DEFAULT_MOUSE_SENS)

/* The mouse event buffer.  */
static struct mousebuf
{
  char evtbuffer[MOUSEBUFSZ];
  int pos;
  size_t size;
  pthread_cond_t readcond;
  pthread_cond_t writecond;
} mousebuf;

/* Wakeup for select */
static pthread_cond_t select_alert;

/* The global lock */
static pthread_mutex_t global_lock;

/* Amount of times the device was opened.  Normally this translator
   should be only opened once.  */ 
static int mouse_repeater_opened;

/* The name of the repeater node.  */
static char *repeater_node;

/* The repeater node.  */
static consnode_t cnode;

/* The mouse sensitivity.  */
float mouse_sens = DEFAULT_MOUSE_SENS;

/* Place the mouse event EVNT in the mouse event buffer.  */
static void
repeat_event (kd_event *evt)
{
  kd_event *ev;

  pthread_mutex_lock (&global_lock);
  while (mousebuf.size + sizeof (kd_event) > MOUSEBUFSZ)
    {
      /* The input buffer is full, wait until there is some space. If this call
       * is interrupted, silently continue */
      (void) pthread_hurd_cond_wait_np (&mousebuf.writecond, &global_lock);
    }
  ev = (kd_event *) &mousebuf.evtbuffer[MOUSEBUF_POS (mousebuf.pos 
						      + mousebuf.size)];
  mousebuf.size += sizeof (kd_event);
  memcpy (ev, evt, sizeof (kd_event));
  
  pthread_cond_broadcast (&mousebuf.readcond);
  pthread_cond_broadcast (&select_alert);
  pthread_mutex_unlock (&global_lock);
}


static error_t
repeater_select (struct protid *cred, mach_port_t reply,
		 mach_msg_type_name_t replytype,
		 struct timespec *tsp, int *type)
{
  error_t err;

  if (!cred)
    return EOPNOTSUPP;

  *type &= ~SELECT_URG;

  if (*type & ~SELECT_READ)
    /* Error immediately available...  */
    return 0;

  if (*type == 0)
    return 0;
  
  pthread_mutex_lock (&global_lock);
  while (1)
    {
      if (mousebuf.size > 0)
	{
	  *type = SELECT_READ;
	  pthread_mutex_unlock (&global_lock);

	  return 0;
	}

      ports_interrupt_self_on_port_death (cred, reply);
      err = pthread_hurd_cond_timedwait_np (&select_alert, &global_lock, tsp);
      if (err)
	{
	  *type = 0;
	  pthread_mutex_unlock (&global_lock);

	  if (err == ETIMEDOUT)
	    err = 0;

	  return err;
	}
    }
}


static void
repeater_open (void)
{
  mouse_repeater_opened++;
}


static void
repeater_close (void)
{
  mouse_repeater_opened--;
  if (!mouse_repeater_opened)
    {
      mousebuf.pos = 0;
      mousebuf.size = 0;
    }
}


static error_t
repeater_read (struct protid *cred, char **data,
	       mach_msg_type_number_t *datalen, off_t offset,
	       mach_msg_type_number_t amount)
{
  /* Deny access if they have bad credentials. */
  if (! cred)
    return EOPNOTSUPP;
  else if (! (cred->po->openstat & O_READ))
    return EBADF;
  
  pthread_mutex_lock (&global_lock);
  while (!mousebuf.size)
    {
      if (cred->po->openstat & O_NONBLOCK && mousebuf.size == 0)
	{
	  pthread_mutex_unlock (&global_lock);
	  return EWOULDBLOCK;
	}
      
      if (pthread_hurd_cond_wait_np (&mousebuf.readcond, &global_lock))
	{
	  pthread_mutex_unlock (&global_lock);
	  return EINTR;
	}
    }
  
  amount = (amount / sizeof (kd_event) - 1) * sizeof (kd_event);
  if (amount > mousebuf.size)
    amount = mousebuf.size;
  
  if (amount > 0)
    {
      char *mousedata;
      unsigned int i = 0;

      /* Allocate a buffer when this is required.  */
      if (*datalen < amount)
	{
	  *data = mmap (0, amount, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
	  if (*data == MAP_FAILED)
	    {
	      pthread_mutex_unlock (&global_lock);
	      return ENOMEM;
	    }
	}
      
      /* Copy the bytes to the user's buffer and remove them from the
	 mouse events buffer.  */
      mousedata = *data;
      while (i != amount)
	{
	  mousedata[i++] = mousebuf.evtbuffer[mousebuf.pos++];
	  mousebuf.pos = MOUSEBUF_POS (mousebuf.pos);
	}
      mousebuf.size -= amount;
      pthread_cond_broadcast (&mousebuf.writecond);
    }
  
  *datalen = amount;
  pthread_mutex_unlock (&global_lock);

  return 0;
}



static void *
input_loop (void *unused)
{
  kd_event *ev;
  vm_offset_t buf;
  mach_msg_type_number_t buf_size;
  
  while (1)
    {
      struct mouse_event evt = { 0 };
      device_read (mousedev, 0, 0, sizeof (kd_event),
		   (char **) &buf, &buf_size);
      ev = (kd_event *) buf;
      
      /* The repeater is set, send the event to the repeater.  */
      if (mouse_repeater_opened)
	{
	  repeat_event (ev);
	  vm_deallocate (mach_task_self(), buf, buf_size);
	  continue;
	}
      
      evt.mouse_movement = CONS_VCONS_MOUSE_MOVE_REL;

      switch (ev->type)
	{
	case MOUSE_LEFT:
	  evt.button = CONS_MOUSE_BUTTON1;
	  break;
	case MOUSE_MIDDLE:
	  evt.button = CONS_MOUSE_BUTTON2;
	  break;
	case MOUSE_RIGHT:
	  evt.button = CONS_MOUSE_BUTTON3;
	  break;

	case MOUSE_MOTION:
	  evt.x = ev->value.mmotion.mm_deltaX * mouse_sens;
	  evt.y = -ev->value.mmotion.mm_deltaY * mouse_sens;
	  break;
	}

      if (ev->type > 0 && ev->type <= 3)
	{
	  if (ev->value.up)
	    evt.mouse_button = CONS_VCONS_MOUSE_BUTTON_RELEASED;
	  else
	    evt.mouse_button = CONS_VCONS_MOUSE_BUTTON_PRESSED;
	}
      
      /* Generate a mouse movement event.  */
      console_move_mouse (&evt);
      vm_deallocate (mach_task_self(), buf, buf_size);
    }

  return NULL;
}


#define PROTO_MOUSESYSTEM	"mousesystem"
#define PROTO_MICROSOFT		"microsoft"
#define PROTO_PS2		"ps/2"
#define PROTO_NOMOUSE		"nomouse"
#define PROTO_LOGITECH		"logitech"
#define PROTO_MOUSE7		"mouse7"

/* The supported mouse protocols.  Be careful with adding more, the
   protocols are carefully ordered so the index is the major device
   number.  */
static char *mouse_protocols[] =
  {
    PROTO_MOUSESYSTEM,
    PROTO_MICROSOFT,
    PROTO_PS2,
    PROTO_NOMOUSE,
    PROTO_LOGITECH,
    PROTO_MOUSE7
  };

static const char doc[] = "Mouse Driver";

static const struct argp_option options[] =
  {
    { "protocol",	'p', "PROTOCOL", 0, "One of the protocols: " 
      PROTO_MOUSESYSTEM ", " PROTO_MICROSOFT ", " PROTO_PS2 ", "
      PROTO_NOMOUSE ", " PROTO_LOGITECH ", " PROTO_MOUSE7 },
    { "device",		'e', "DEVICE"  , 0,
      "One of the devices: " DEV_COM0 ", " DEV_COM1 },
    { "sensitivity",    's', "SENSITIVITY", 0, "The mouse"
      " sensitivity (default " DEFAULT_MOUSE_SENS_STRING ").  A lower value"
      " means more sensitive" },
    { "repeat",		'r', "NODE", OPTION_ARG_OPTIONAL,
      "Set a repeater translator on NODE (default: " DEFAULT_REPEATER_NODE ")"},
    { 0 }
  };

static error_t setrepeater (const char *nodename);

static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  int *pos = (int *) state->input;
  
  switch (key)
    {
    case 'p':
      {
	unsigned int i;
	
	for (i = 0; i < (sizeof (mouse_protocols) / sizeof (char *)); i++)
	  {
	    if (!strcasecmp (arg, mouse_protocols[i]))
	      {
		majordev = i;
		*pos = state->next;
		return 0;
	      }
	  }
	fprintf (stderr, "Unknown protocol `%s'\n", arg);
	argp_usage (state);
	return ARGP_ERR_UNKNOWN;
      }

    case 'e':
      {
	if (!strcasecmp (DEV_COM0, arg))
	  minordev = 0;
	else if (!strcasecmp (DEV_COM1, arg))
	  minordev = 1;
	else
	  {
	    fprintf (stderr, "Unknown device `%s'\n", arg);
	    argp_usage (state);
	    return ARGP_ERR_UNKNOWN;
	  }
	break;
      }
      
    case 'r':
      repeater_node = arg ? arg : DEFAULT_REPEATER_NODE;
      break;
     
    case 's':
      {
	char *tail;
	
	errno = 0;
	mouse_sens = strtod (arg, &tail);
	if (tail == NULL || tail == arg || *tail != '\0')
	  argp_error (state, "SENSITIVITY is not a number: %s", arg);
	if (errno)
	  argp_error (state, "Overflow in argument SENSITIVITY %s", arg);
	break;
      }
      
    case ARGP_KEY_END:
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }

  *pos = state->next;
  return 0;
}


static struct argp argp = {options, parse_opt, 0, doc};

static error_t
pc_mouse_init (void **handle, int no_exit, int argc, char *argv[], int *next)
{
  error_t err;
  int pos = 1;
  
  /* Parse the arguments.  */
  err = argp_parse (&argp, argc, argv, ARGP_IN_ORDER | ARGP_NO_EXIT
		    | ARGP_SILENT, 0, &pos);
  *next += pos - 1;
  if (err && err != EINVAL)
    return err;

  return 0;
}


static error_t
pc_mouse_start (void *handle)
{
  error_t err;
  pthread_t thread;
  char device_name[9];
  int devnum = majordev << 3 | minordev;
  device_t device_master;

  sprintf (device_name, "mouse%d", devnum);
  err = get_privileged_ports (0, &device_master);
  if (err)
    return err;
  
  err = device_open (device_master, D_READ, device_name, &mousedev);
  mach_port_deallocate (mach_task_self (), device_master);
  if (err)
    return ENODEV;

  err = driver_add_input (&pc_mouse_ops, NULL);
  if (err)
    {
      device_close (mousedev);
      mach_port_deallocate (mach_task_self (), mousedev);

      return err;
    }

  err = pthread_create (&thread, NULL, input_loop, NULL);
  if (!err)
    pthread_detach (thread);
  else
    {
      errno = err;
      perror ("pthread_create");
    }
  
  if (repeater_node)
    setrepeater (repeater_node);
  
  return 0;
}


static error_t
pc_mouse_fini (void *handle, int force)
{
  device_close (mousedev);
  mach_port_deallocate (mach_task_self (), mousedev);
  console_unregister_consnode (cnode);
  console_destroy_consnode (cnode);

  return 0;
}



struct driver_ops driver_pc_mouse_ops =
  {
    pc_mouse_init,
    pc_mouse_start,
    pc_mouse_fini
  };

static struct input_ops pc_mouse_ops =
  {
    NULL,
    NULL
  };


/* Set make repeater translator node named NODENAME.  */
static error_t
setrepeater (const char *nodename)
{
  error_t err;
  
  err = console_create_consnode (nodename, &cnode);
  if (err)
    return err;
  
  cnode->read = repeater_read;
  cnode->write = 0;
  cnode->select = repeater_select;
  cnode->open = repeater_open;
  cnode->close = repeater_close;
  cnode->demuxer = 0;
  
  pthread_mutex_init (&global_lock, NULL);
  
  pthread_cond_init (&mousebuf.readcond, NULL);
  pthread_cond_init (&mousebuf.writecond, NULL);
  pthread_cond_init (&select_alert, NULL);
  
  console_register_consnode (cnode);
  
  return 0;
}
