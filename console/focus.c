#include <argp.h>
#include <cthreads.h>
#include <string.h>

#include "vcons.h"
#include "input-drv.h"
#include "focus.h"

struct argp_option focus_argp_options[] =
  {
    { "focus-group",  'f', "NAME", OPTION_ARG_OPTIONAL, "Start a new focus group" },
    { "input-device", 'i', "NAME", 0, "Add input device NAME to focus group" },
    { "focus",        'F', "CONS[/VCONS]", 0, "Put focus on console CONS, and virtual console VCONS within if specified" },
    { 0 }
  };

struct
{
  focus_t focus;
} focus_config;

static error_t
parse_opt (int opt, char *arg, struct argp_state *state)
{
  switch (opt)
    {
    default:
      return ARGP_ERR_UNKNOWN;
    case ARGP_KEY_INIT:
    case ARGP_KEY_SUCCESS:
    case ARGP_KEY_ERROR:
      break;
    case 'f':
      /* Start a new focus group.  */
      focus_config.focus = 0;
      break;
    case 'i':
      {
	/* Start a new input device.  */
	int found = 0;
	int i = 0;
	while (input_driver[i])
	  {
	    //	    if (state->child_inputs[i])
	    //  {
	    //	input_driver->end_config
	    if (! strcmp (input_driver[i]->name, arg))
	      {
		found = 1;
		state->child_inputs[i] = (void *) 1;
	      }
	    else
	      state->child_inputs[i] = NULL;
	  }
	if (!found)
	  argp_error (state, "unknown input device %s", arg);
      }
    }
  return 0;
}

struct argp focus_argp =
  { focus_argp_options, parse_opt, 0, 0, 0 /* XXX input_argp_childs */};

typedef struct input *input_t;
struct input
{
  input_t next;
};

struct focus
{
  struct mutex lock;

  /* The virtual console receiving our input characters.  */
  vcons_t vcons;

  /* The list of input devices.  */
  input_t input_list;
};


error_t
focus_create (focus_t *r_focus)
{
  focus_t focus = calloc (1, sizeof (*focus));
  if (!focus)
    return ENOMEM;
  mutex_init (&focus->lock);

  return 0;
}


void
focus_add (focus_t focus, input_t input)
{
  mutex_lock (&focus->lock);
  input->next = focus->input_list;
  focus->input_list = input;
  mutex_unlock (&focus->lock);
}

void
focus_switch_to (focus_t focus, vcons_t vcons)
{
  mutex_lock (&focus->lock);
  vcons_activate (vcons, (int) focus);
  focus->vcons = vcons;
  mutex_unlock (&focus->lock);
}
