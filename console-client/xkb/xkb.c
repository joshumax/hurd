/*  xkb.c -- Main XKB routines.

    Copyright (C) 2002, 2003, 2004  Marco Gerards
   
    Written by Marco Gerards <metgerards@student.han.nl>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.  

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
  
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <iconv.h>
#include <locale.h>
#include <error.h>
#include <device/device.h>
#include <mach/mach_port.h>

#include "xkb.h"
#include <hurd/console.h>
#define XK_XKB_KEYS
#define XK_MISCELLANY
#include <X11/keysymdef.h>
#include <driver.h>
#include <mach-inputdev.h>
#include <wctype.h>



/* The converter.  */
extern iconv_t cd;

/* All interpretations for compatibility.  (Translation from keysymbol
   to actions).  */
xkb_interpret_t *interpretations;

/* All keysymbols and how they are handled by XKB.  */
struct key *keys = NULL;
int min_keys;
int max_keys;

/* The current set of modifiers.  */
static modmap_t bmods;
/* Temporary set of modifiers. This is a copy of mods, so mods won't
   be consumed (Effective modifiers).  */
static modmap_t emods;

/* Effective group.  */
static group_t egroup;
/* Base group.  */
static group_t bgroup;
/* Locked group.  */
static group_t lgroup;
/* Latched group.  */
static group_t latchedgroup;

static boolctrls bboolctrls;

/* A counter to count how often the modifier was set. This is used
   when two separate actions set the same modifier. (example: Left
   Shift and Right Shift.).  */
modcount_t modsc;

keystate_t keystate[255];

/* The locked modifiers. Lock simply works an an invertion.  */
static modmap_t lmods = {0, 0};

/* When the modifier is used the modifier will be consumed.  */
static modmap_t latchedmods = {0, 0};

/* Not setting GroupsWrap uses modulus to keep the value into the
   range.  */
static int GroupsWrap = 0;

/* MouseKeys, default: off.  */
static int MouseKeys = 0;
/* Default mousebutton. */
static int default_button = 0;

static xkb_indicator_t *indicators;
static int indicator_count;
static int indicator_map = 0;

/* unused
static int stickykeys_active = 1;
*/

int
debug_printf (const char *f, ...)
{
  va_list ap;
  int ret = 0;

  va_start (ap, f);
#ifdef XKB_DEBUG  
  ret = vfprintf (stderr, f, ap);
#endif
  va_end (ap);

  return ret;  
}


static void
interpret_kc (keycode_t kc)
{
  int cursym;
  int rmods = keys[kc].mods.rmods;
  struct xkb_interpret *interp;

  for (interp = interpretations; interp; interp = interp->next)
    {
      group_t group;

      for (group = 0; group < keys[kc].numgroups; group++)
	{
	  int width =  keys[kc].groups[group].width;

	  for (cursym = 0; cursym < width; cursym++)
	    {
	      int symbol = keys[kc].groups[group].symbols[cursym];

	      /* Check if a keysymbol requirement exists or if it
		 matches.  */
	      if (interp->symbol == 0 ||
		  (symbol && (interp->symbol == symbol)))
		{
		  int flags = interp->match & 0x7f;

		  /* XXX: use enum.  */
		  if ((flags == 0 && (!(interp->rmods & rmods))) ||
		      (flags == 1) ||
		      (flags == 2 && (interp->rmods & rmods)) ||
		      (flags == 3 && ((interp->rmods & rmods) ==
				      interp->rmods)) ||
		      (flags == 4 && interp->rmods == rmods))
		    {
		      xkb_action_t *action;

		      if (keys[kc].groups[group].actionwidth > cursym && 
			  keys[kc].groups[group].actions[cursym] &&
			  keys[kc].groups[group].actions[cursym]->type != 
			  SA_NoAction)
			continue;

		      action = malloc (sizeof (xkb_action_t));
		      memcpy (action, &interp->action, sizeof (xkb_action_t));
		      
		      key_set_action (&keys[kc], group, cursym, action);

		      keys[kc].flags = interp->flags | KEYHASACTION;
		      if (!keys[kc].mods.vmods)
			keys[kc].mods.vmods = interp->vmod;
		    }  
		}
	    }
	}
    }

}


/*  Test if c is an uppercase letter. */
static int islatin_upper (int c)
{
  return (c >= 'A' && c <= 'Z');
}

/*  Test if c is an lowercase letter. */
static int islatin_lower (int c)
{
  return (c >= 'a' && c <= 'z');
}

/*  A key is of the keytype KEYPAD when one of the symbols that can be produced
    by this key is in the KEYPAD symbol range.  */
static int
iskeypad (int width, int *sym)
{
  int i;
  
  for (i = 0; i < width; i++, sym++)
    {
      /* Numlock is in the keypad range but shouldn't be of the type
	 keypad because it will depend on itself in that case.  */
      if (*sym == XK_Num_Lock)
	return 0;
      if (*sym >= KEYPAD_FIRST_KEY && *sym <= KEYPAD_LAST_KEY)
	return 1;
    }
  return 0;   
}

/* Get the keytype (the keytype determines which modifiers are used
   for shifting.

   For reference, see FindAutomaticType@xkbcomp/symbols.c.

   These rules are used:

   * If the width is 1 the keytype is ONE_LEVEL.
   * If the first symbol is lowercase and the second is uppercase
   (latin alphabeth) the keytype is ALPHABETHIC.
   * If one of the symbols is in the keypad range the keytype is KEYPAD.
   * If width is 4 the type is either FOUR_LEVEL, FOUR_LEVEL_ALPHABETIC,
     FOUR_LEVEL_SEMI_ALPHABETIC (first sym pair is alphabetic) or
     FOUR_LEVEL_KEYPAD.
   * Else the keytype is TWO_LEVEL. */
static struct keytype *
get_keytype (int width, symbol *sym)
{
  struct keytype *ktfound = NULL;

  if (!sym)
    ktfound = keytype_find ("TWO_LEVEL");
  else if ((width == 1) || (width == 0))
    ktfound = keytype_find ("ONE_LEVEL");
  else if (width == 2) {
    if (islatin_lower (sym[0]) && islatin_upper (sym[1]))
      ktfound = keytype_find ("ALPHABETIC");
    else if (iskeypad (width, sym))
      ktfound = keytype_find ("KEYPAD");
    else
      ktfound = keytype_find ("TWO_LEVEL");
  }
  else if (width <= 4) {
    if (islatin_lower (sym[0]) && islatin_upper (sym[1]))
      if (islatin_lower(sym[2]) && islatin_upper(sym[3]))
        ktfound = keytype_find ("FOUR_LEVEL_ALPHABETIC");
      else
        ktfound = keytype_find ("FOUR_LEVEL_SEMIALPHABETIC");
    else if (iskeypad (2, sym))
      ktfound = keytype_find ("FOUR_LEVEL_KEYPAD");
    else
      ktfound = keytype_find ("FOUR_LEVEL");
  }

  if (!ktfound)
    ktfound = keytype_find ("TWO_LEVEL");
  if (!ktfound)
    {
      console_error (L"Default keytypes have not been defined!\n");
      exit (1);
    }

  return ktfound;
}

/* Create XKB style actions for every action described by keysymbols.  */
static void
interpret_all (void)
{
  keycode_t curkc;

  /* Check every key.  */
  for (curkc = 0; curkc < max_keys; curkc++)
    interpret_kc (curkc);
}

static void
determine_keytypes (void)
{
  keycode_t curkc;

  /* Check every key.  */
  for (curkc = 0; curkc < max_keys; curkc++)
    {
      group_t group;
      for (group = 0; group < 4; group++)
	{
	  struct keygroup *kg = &keys[curkc].groups[group];

	  if (!kg->keytype)
	    kg->keytype = get_keytype (kg->width, kg->symbols);
	}
    }
}

/* Wrap the group GROUP into a valid group range. The method to use is
   defined by the GroupsWrap control.  */
static int
wrapgroup (group_t group, int maxgroup)
{
  /* If the group is in an invalid range, fix it.  */
  if (group < 0 || group >= maxgroup)
    {
      /* If RedirectIntoRange should be used use the 4 LSbs of
	 the GroupsWrap control instead of the current group. */ 
      if (GroupsWrap & RedirectIntoRange)
	group = GroupsWrap & 0x0F;
      /* Select the closest valid group.  */
      else if (GroupsWrap & ClampIntoRange)
	{
	  if (group < 0)
	    group = 0;
	  else
	    group = maxgroup - 1;
	}
      else /* Default: use modulus to wrap the group.  */
	group %= maxgroup;
    }
  
  return group;
}



/* This function must be called after a modifier, group or control has
   been changed. The indicator map will be regenerated and the hardwre
   representation of this map will be updated.  */
static void
set_indicator_mods (void)
{
  int i;

  /* Calculate the effective modmap.  */
  emods = bmods;
  emods.rmods |= lmods.rmods;
  emods.vmods |= lmods.vmods;
  emods.rmods |= latchedmods.rmods;
  emods.vmods |= latchedmods.vmods;

  for (i = 0; i < indicator_count; i++)
    {
      if (!(indicators[i].flags & IM_NoAutomatic))
	{
	  if (indicators[i].which_mods & IM_UseBase)
	    {
	      if (((indicators[i].modmap.rmods & bmods.rmods) ==
		   indicators[i].modmap.rmods) &&
		  ((indicators[i].modmap.vmods & bmods.vmods) == 
		   indicators[i].modmap.vmods))
		{
		  indicator_map |= (1 << i);
		  continue;
		}
	    }
	  if (indicators[i].which_mods & IM_UseLatched)
	    {
	      if (((indicators[i].modmap.rmods & latchedmods.rmods) == 
		   indicators[i].modmap.rmods) &&
		  ((indicators[i].modmap.vmods & latchedmods.vmods) == 
		   indicators[i].modmap.vmods))
		{
		  indicator_map |= (1 << i);
		  continue;
		}
	    }
	  if (indicators[i].which_mods & IM_UseLocked)
	    {
	      if (((indicators[i].modmap.rmods & lmods.rmods) == 
		   indicators[i].modmap.rmods) &&
		  ((indicators[i].modmap.vmods & lmods.vmods) == 
		   indicators[i].modmap.vmods))
		{
		  indicator_map |= (1 << i);
		  continue;
		}
	    }
	  if (indicators[i].which_mods & IM_UseEffective)
	    {
	      if (((indicators[i].modmap.rmods & emods.rmods) == 
		   indicators[i].modmap.rmods) &&
		  ((indicators[i].modmap.vmods & emods.vmods) == 
		   indicators[i].modmap.vmods))
		{
		  indicator_map |= (1 << i);
		  continue;
		}
	    }
	  /* The indicator shouldn't be on anymore so turn it off.  */
	  indicator_map &= ~(1 << i);
	}
    }
  debug_printf ("INDICATOR: %d\n", indicator_map);
}

/* Set base modifiers.  A counter exists for every modifier. When a
   modifier is set this counter will be incremented with one.  */
static void
setmods (modmap_t smods, keypress_t key)
{
  /* Higher the modcount for every set modifier.  */
  void set_xmods (int xmods, int modsc[])
    {
      int i = 0;
      
      while (xmods)
	{
	  if (xmods & 1)
	    modsc[i]++;

	  xmods >>= 1;
	  i++;
	}
    }

  bmods.rmods |= smods.rmods;
  bmods.vmods |= smods.vmods;
  
  set_xmods (smods.rmods, modsc.rmods);
  set_xmods (smods.vmods, modsc.vmods);

  set_indicator_mods ();
}

/* Clear base modifiers.  A counter exists for every modifier. When a
   key release wants to clear a modifier this counter will be
   decreased with one. When the counter becomes 0 the modifier will be
   cleared and unlocked if the clearLocks flag is set.  */
static void
clearmods (modmap_t cmods, keypress_t key, int flags)
{
#define	CLEAR_XMOD(MODTYPE)			\
  {						\
    int i = 0;					\
    int mmods = cmods.MODTYPE;			\
						\
    while (mmods)				\
      {						\
	if (mmods & 1)				\
	  if (!(--modsc.MODTYPE[i]))		\
	    {					\
	      bmods.MODTYPE &= ~(1 << i);	\
	      if (flags & clearLocks)		\
		lmods.MODTYPE &= ~(1 << i);	\
	    }					\
	mmods >>= 1;				\
	i++;					\
      }						\
  }
    
  CLEAR_XMOD(rmods);
  CLEAR_XMOD(vmods);
  
  set_indicator_mods ();
}

/* Set modifiers in smods and also lock them if the flag noLock is
   not set.  */
static void
setlocks (modmap_t smods, keypress_t key, int flags)
{
  if (!key.rel)
    {
      modmap_t cmods;
      cmods.rmods = lmods.rmods & smods.rmods;
      cmods.vmods = lmods.vmods & smods.vmods;

      /* Locking also sets the base modifiers.  */
      setmods (smods, key);
      
      if (!(flags & noUnlock))
	{
	  lmods.rmods &= ~cmods.rmods;
	  lmods.vmods &= ~cmods.vmods;
	}
      
      if (!(flags & noLock))
	{
	  lmods.rmods |= (~cmods.rmods & smods.rmods);
	  lmods.vmods |= (~cmods.vmods & smods.vmods);
	}
    }
  else
    clearmods (smods, key, flags);
}

/* Latch the modifiers smods for key KEY. When another key is operated while
   KEY is pressed the release of KEY will just clear the base
   modifiers, otherwise latch the modifiers like is described in the
   protocol specification.  */
static void
latchmods (modmap_t smods, keypress_t key, int flags)
{
  if (!key.rel)
    setmods (smods, key);
  else
    {
      modmap_t oldlmods;
      oldlmods = lmods;
      clearmods (smods, key, flags);
      /* Modifier that have been unlocked don't have effect anymore.  */
      smods.rmods &= ~(lmods.rmods & oldlmods.rmods);
      smods.vmods &= ~(lmods.vmods & oldlmods.vmods);


      /* If another key has been pressed while this modifier key was
	 pressed don't latch, just behave as SetMods.  */
      if (key.keycode == key.prevkc)
	{
	  if (flags & latchToLock)
	    {
	      /* When a modifier exists as a locked modifier, consume
		 and unlock.  */
	      lmods.rmods |= latchedmods.rmods & smods.rmods;
	      lmods.vmods |= latchedmods.vmods & smods.vmods;

	      smods.rmods &= ~(latchedmods.rmods & smods.rmods);
	      smods.vmods &= ~(latchedmods.vmods & smods.vmods);
	    }
	      
	  /* Use the remaining modifiers for latching.  */
	  latchedmods.rmods |= smods.rmods;
	  latchedmods.vmods |= smods.vmods;
	}
    }
}

static void
setgroup (keypress_t key, group_t group, int flags)
{
  debug_printf ("Setgroup ()\n");
  if (!key.rel)
    {
      if (flags & groupAbsolute)
	{
	  bgroup = group;
	  keystate[key.keycode].oldgroup = bgroup;
	}
      else
	bgroup += group;
    }
  else
    {
      if ((key.keycode == key.prevkc) && (flags & clearLocks))
	lgroup = 0;
      if (flags & groupAbsolute)
	bgroup = keystate[key.keycode].oldgroup;
      else
	/* XXX: Maybe oldgroup should be restored for !groupAbsolute
	   too, because wrapgroup might have affected the calculation
	   and subtracting will not undo the set operation. However
	   this way of handeling relative groups is better because the
	   order of releasing keys when multiple relative setgroup keys
	   are pressed doesn't matter.  */
	bgroup -= group;
    }
}

static void
latchgroup (keypress_t key, group_t sgroup, int flags)
{
  group_t oldlgroup = sgroup;
  setgroup (key, sgroup, flags);
  debug_printf ("Latchgroup ()\n");
  
  if (key.keycode == key.prevkc && oldlgroup == lgroup)
    {
      if ((flags & latchToLock) && latchedgroup)
	{
	  lgroup += sgroup;
	  latchedgroup -= sgroup;
	}
      else
	latchedgroup += sgroup;
    }
}

static void
lockgroup (keypress_t key, group_t group, int flags)
{
  debug_printf (">L: %d, g: %d\n", lgroup, group);

  keystate[key.keycode].oldgroup = lgroup;
  if (flags & groupAbsolute)
    lgroup = group;
  else
    lgroup += group;

  lgroup = wrapgroup (lgroup, 4);

  debug_printf ("<L: %d, g: %d\n", lgroup, group);

}

static void
setcontrols (keypress_t key, boolctrls ctrls, int flags)
{
  keystate[key.keycode].bool = ctrls & ~bboolctrls;
  bboolctrls |= ctrls;
}

static void
clearcontrols (keypress_t key, boolctrls ctrls, int flags)
{
  bboolctrls &= ~keystate[key.keycode].bool;
}

static void
lockcontrols (keypress_t key, boolctrls ctrls, int flags)
{
  /* XXX this needs a closer look.  */
  if (!key.rel)
    {
      //setcontrols (key, boolctrls, flags);
      //if (!(flags & noLock))
      //	lboolctrls |= boolctrls;
    }
  else
    {
      //      clearcontrols (key, boolctrls, flags);
      /* This unlock behaviour doesn't work and sucks, just like the protocol
	 specification where it was documented.  */
      //      if (!(flags & noUnlock))
      //	lboolctrls &= ~keystate[key.keycode].bool;
    }
  
}

/* Not properly implemented, not very high priority for me.  */
/* static void */
/* isolock (keypress_t key, group group, modmap_t mods, int flags) */
/* { */
/*     if (!key.rel) */
/*       { */
/*         struct isolock *newiso; */

/*         if (flags & dfltIsGroup) */
/*   	setgroup (key, group, flags & groupAbsolute); */
/*         else */
/*   	setmods (key, mods, 0); */
      
/*         newiso = malloc (sizeof struct isolock); */
/*         if (!newiso) */
/*   	;// return error */
/*         active_isolocks.anchor */
/*       } */
/*     else */
/*       { */
/*         if (flags & dfltIsGroup) */
/*   	{ */
/*   	  cleargroup (key, group, flags & groupAbsolute); */
/*   	  if (bla) */
/*   	    lockgroup (key, group, flags & groupAbsolute); */
/*   	} */
/*         else */
/*   	{ */
/*   	  clearmods (key, mods, 0); */
/*   	  if (bla) */
/*   	    { */
/*   	      lmods.rmods |= mods.rmods; */
/*   	      lmods.vmods |= mods.vmods; */
/*   	    } */
/*   	} */
/*       } */
/* } */

/* Move the mousepointer relational to its current position.  */
static void
mouse_x_move (int xdelta)
{
  /* XXX: Ofcouse this function has to do *something* :).  */
}

/* Change the horizontal position of the mousepointer.  */
static void
mouse_x_move_to (int x)
{
  /* XXX: Ofcouse this function has to do *something* :).  */
}

/* Move the mousepointer relational to its current position.  */
static void
mouse_y_move (int ydelta)
{
  /* XXX: Ofcouse this function has to do *something* :).  */
}

/* Change the vertical position of the mousepointer.  */
static void
mouse_y_move_to (int y)
{
  /* XXX: Ofcouse this function has to do *something* :).  */
}

/* Simulate a mouse button press for button BUTTON.  */
static void
mouse_button_press (int button)
{
  /* XXX: Ofcouse this function has to do *something* :).  */
}

/* Simulate a mouse button press for button BUTTON.  */
static void
mouse_button_release (int button)
{
  /* XXX: Ofcouse this function has to do *something* :).  */
}



/* Forward declaration for redirected keys.  */
static symbol handle_key (keypress_t);


/* Execute an action bound to a key. When the action isn't supported
   or when the action doesn't consume the key return true, otherwise
   return false.  */
static int
action_exec (xkb_action_t *action, keypress_t key)
{
  if (!action) 
    return KEYNOTCONSUMED;

  debug_printf ("EXEC: %d\n", action->type);

  switch (action->type)
    {
      /* LockMods: Lock/Unlock modifiers when the key is pressed.  */
    case SA_LockMods:
      {
	action_setmods_t *setmodmap =  (action_setmods_t *) action;
	modmap_t modm = setmodmap->modmap;

	/* UseModMap  */
	if (setmodmap->flags & useModMap)
	  {
	    modm.rmods |= keys[key.keycode].mods.rmods;
	    modm.vmods |= keys[key.keycode].mods.vmods;
	  }
	
	setlocks (modm, key, setmodmap->flags);
      }
      break;
      /* SetMods: Set/Unset modifiers. Those modifier will be set as
	 long the key is pressed. Keys like shift, alt and control are
	 used here often.  */
    case SA_SetMods: 
      {
	action_setmods_t *setmodmap =  (action_setmods_t *) action;
	modmap_t modm = setmodmap->modmap;

	/* UseModMapMods means: also use the real modifiers specified
	   in the key's modmap.  */
	if (setmodmap->flags & useModMap)
	  {
	    debug_printf ("Apply modmaps\n");
	    modm.rmods |= keys[key.keycode].mods.rmods;
	    modm.vmods |= keys[key.keycode].mods.vmods;
	  }

	/* When the key is pressed set the modifiers.  */
	if (!key.rel)
	  setmods (modm, key);
	else	/* When the key is released clear the modifiers.  */
	  clearmods (modm, key, setmodmap->flags);
	
	break;
      }
      /* Set the basegroup. When groupAbsolute isn't used add it
	 to the basegroup.  */
    case SA_LatchMods:
      {
	action_setmods_t *setmodmap =  (action_setmods_t *) action;

	modmap_t modm = setmodmap->modmap;
	    
	/* UseModMapMods means: also use the real modifiers specified
	   in the key's modmap.  */
	if (setmodmap->flags & useModMap)
	  {
	    modm.rmods |= keys[key.keycode].mods.rmods;
	    modm.vmods |= keys[key.keycode].mods.vmods;
	  }

	latchmods (modm, key, setmodmap->flags);
	
	break;
      }
    case SA_SetGroup:
      {
	action_setgroup_t *setgroupac =  (action_setgroup_t *) action;

	setgroup (key, setgroupac->group, setgroupac->flags);
	break;
      }
    case SA_LockGroup:
      {
	action_setgroup_t *setgroupac =  (action_setgroup_t *) action;

	if (!key.rel)
	  lockgroup (key, setgroupac->group, setgroupac->flags);
	break;
      }
    case SA_LatchGroup:
      {
	action_setgroup_t *setgroupac =  (action_setgroup_t *) action;

	latchgroup (key, setgroupac->group, setgroupac->flags);
	break;
      }

    case SA_PtrBtn:
      {
	action_ptrbtn_t *ptrbtnac = (action_ptrbtn_t *) action;
	int i;
	int button;

	if (!MouseKeys)
	  return KEYNOTCONSUMED;

	if (ptrbtnac->flags & useDfltBtn)
	  button = default_button;
	else
	  button = ptrbtnac->button;

	if (ptrbtnac->count)
	  for (i = 0; i < ptrbtnac->count; i++)
	    {
	      /* XXX: Should there be a delay?  */
	      mouse_button_press (button);
	      mouse_button_release (button);
	    }
	else if (!key.rel)
	  mouse_button_press (button);
	else
	  mouse_button_release (button);
	break;
      }
    case SA_LockPtrBtn:
      {
	action_ptrbtn_t *ptrbtnac = (action_ptrbtn_t *) action;

	int button;

	if (!MouseKeys)
	  return  KEYNOTCONSUMED;

	if (ptrbtnac->flags & useDfltBtn)
	  button = default_button;
	else
	  button = ptrbtnac->button;
	    
	/* XXX: Do stuff.  */

	break;
      }
    case SA_SetPtrDflt:
      {
	action_ptr_dflt_t *ptrdfltac = (action_ptr_dflt_t *) action;
	    
	if (!MouseKeys)
	  return  KEYNOTCONSUMED;

	if (!key.rel)
	  {
	    if (ptrdfltac->flags & DfltBtnAbsolute)
	      default_button = ptrdfltac->value;
	    else
	      default_button += ptrdfltac->value;
	  }

	if (default_button < 0)
	  default_button = 0;

	if (default_button > 5)
	  default_button = 5;

	break;
      }
    case SA_TerminateServer:
      /* Zap! */
      console_exit ();
      break;
    case SA_SwitchScreen:
      {
	action_switchscrn_t *switchscrnac = (action_switchscrn_t *) action;

	if (key.rel)
	  break;
	    
	if (switchscrnac->flags & screenAbs)
	  /* Switch to screen.  */
	  console_switch ((char) switchscrnac->screen, 0); 
	else
	  /* Move to next/prev. screen.  */
 	  console_switch (0, (char) switchscrnac->screen);
	break;
      }
    case SA_RedirectKey:
      {
	action_redirkey_t *redirkeyac = (action_redirkey_t *) action;	    
	
	key.keycode = redirkeyac->newkey & (key.rel ? 0x80:0);
	
	/* For the redirected key other modifiers should be used.  */
	emods.rmods = bmods.rmods | lmods.rmods | latchedmods.rmods;
	emods.vmods = bmods.vmods | lmods.vmods | latchedmods.vmods;

	emods.rmods &= ~redirkeyac->rmodsmask;
	emods.rmods |= redirkeyac->rmods;
	emods.vmods &= ~redirkeyac->vmods;
	emods.vmods |= redirkeyac->vmodsmask;
	
	/* XXX: calc group etc.  */

	handle_key (key);
	break;
      }
    case SA_ConsScroll:
      {
	action_consscroll_t *scrollac = (action_consscroll_t *) action;
	
	if (key.rel)
	  break;

	if (scrollac->flags & usePercentage)
	  console_scrollback (CONS_SCROLL_ABSOLUTE_PERCENTAGE,
			      100 - scrollac->percent);

	if (scrollac->screen)
	  console_scrollback (CONS_SCROLL_DELTA_SCREENS, -scrollac->screen);

	if (scrollac->line)
	  {
	    int type = (scrollac->flags & lineAbs) ? 
	      CONS_SCROLL_ABSOLUTE_LINE : CONS_SCROLL_DELTA_LINES;
	    console_scrollback (type, -scrollac->line);
	  }
	break;
      }
    case SA_ActionMessage:
    case SA_DeviceBtn:
    case SA_LockDeviceBtn:
    case SA_DeviceValuator:
      return KEYNOTCONSUMED;
    case SA_MovePtr:
      {
	action_moveptr_t *moveptrac = (action_moveptr_t *) action;

	if (!MouseKeys)
	  return KEYNOTCONSUMED;

	if (moveptrac->flags & MoveAbsoluteX)
	  mouse_x_move_to (moveptrac->x);
	else
	  mouse_x_move (moveptrac->x);
	  
	if (moveptrac->flags & MoveAbsoluteY)
	  mouse_y_move_to (moveptrac->y);
	else
	  mouse_y_move (moveptrac->y);
	break;
      }
    case SA_SetControls:
      {
	action_setcontrols_t *controlsac = (action_setcontrols_t *) action;
	if (key.rel)
	  clearcontrols (key, controlsac->controls, 0);
	else
	  setcontrols (key, controlsac->controls, 0);
	break;
      }
    case SA_LockControls:
      {
	action_setcontrols_t *controlsac = (action_setcontrols_t *) action;
	lockcontrols (key, controlsac->controls, 0);
	break;
      }
    default:
      /* Preserve the keycode.  */
      return KEYNOTCONSUMED;
      break;
    }
  
  /* Don't preserve the keycode because it was consumed.  */
  return KEYCONSUMED;
}



/* Calculate the shift level for a specific key.  */
static int
calc_shift (keycode_t key)
{
  /* The keytype for this key.  */
  struct keytype *keytype = keys[key].groups[egroup].keytype;
  struct typemap *map;
  
  /* XXX: Shouldn't happen, another way to fix this?  */
  if (!keytype)
    return 0;
    
  /* Scan though all modifier to level maps of this keytype to search
     the level.  */
  for (map = keytype->maps; map; map = map->next)
    /* Does this map meet our requirements?  */
    if (map->mods.rmods == (emods.rmods & keytype->modmask.rmods) &&
	map->mods.vmods == (emods.vmods & keytype->modmask.vmods))
      {
	/* Preserve all modifiers specified in preserve for this map.  */
	emods.rmods &= ~(map->mods.rmods & (~map->preserve.rmods));
	emods.vmods &= ~(map->mods.vmods & (~map->preserve.vmods));
	return map->level;
      }

  /* When no map is found use the default shift level and consume all
     modifiers.  */
  emods.vmods &= ~keytype->modmask.vmods;
  emods.rmods &= ~keytype->modmask.rmods;

  return 0;
}

static symbol
symtoctrlsym (symbol c)
{
  c = toupper (c);
		  
  switch (c)
    {
    case 'A' ... 'Z':
      c = c - 'A' + 1;
      break;
    case '[': case '3':
      c = '\e';
      break;
    case '\\': case '4':
      c = '';
      break;
    case ']': case '5':
      c = '';
      break;
    case '^': case '6':
      c = '';
      break;
    case '/':
      c = '/';
      break;
    case ' ':
      c = '\0';
      break;
    case '_': case '7':
      c= '';
      break;
    case '8':
      c = '\x7f';
      break;
    }
  
  return c;
}


/* Handle all actions, etc. bound to the key KEYCODE and return a XKB
   symbol if one is generated by this key. If redirected_key contains
   1 this is keypress generated by the action SA_RedirectKey, don't
   change the effective modifiers because they exist and have been
   changed by SA_RedirectKey.  */
static symbol
handle_key (keypress_t key)
{
  int actioncompl = 0;

  modmap_t oldmods;
  group_t oldgroup = 0;
  
  /* The level for this key.  */
  int level;

  /* The symbol this keypress generated.  */
  symbol sym = 0;

  debug_printf ("groups\n");
  /* If the key does not have a group there is nothing to do.  */
  if (keys[key.keycode].numgroups == 0)
    return -1;

  /* The effective group is the current group, but it can't be
     out of range.  */
  egroup = wrapgroup (bgroup + lgroup,
		      keys[key.keycode].numgroups);

  if (keys[key.keycode].groups[egroup].actions)
    {
      if (key.rel)
	{
	  debug_printf ("action\n");
	  if (!keystate[key.keycode].prevstate)
	    /* Executing the inverse action of a never executed
	       action... Stop! */
	    return -1;
	    
	  keystate[key.keycode].prevstate = 0;
	  emods = keystate[key.keycode].prevmods;
	  egroup = wrapgroup (keystate[key.keycode].prevgroup,
			      keys[key.keycode].numgroups);
	}
      else /* This is a keypress event.  */
	{
	  /* Calculate the effective modmap.  */
	  emods = bmods;
	  emods.rmods |= lmods.rmods;
	  emods.vmods |= lmods.vmods;
	  emods.rmods |= latchedmods.rmods;
	  emods.vmods |= latchedmods.vmods;
	}

      oldmods = emods;
      oldgroup = egroup;
      
      level = calc_shift (key.keycode);// % 

      if (keys[key.keycode].groups[egroup].actionwidth >= level + 1
	  && keys[key.keycode].groups[egroup].actions[level])
	{
	  actioncompl = action_exec
	    (keys[key.keycode].groups[egroup].actions[level], key);
	}
    }

  if (actioncompl == KEYCONSUMED && !key.rel)
    {
      /* An action was executed. Store the effective modifier this key
	 so the reverse action can be called on key release.  */
      keystate[key.keycode].prevstate = 1;
      keystate[key.keycode].prevmods = oldmods;
      keystate[key.keycode].prevgroup = oldgroup;
    }

  debug_printf ("consumed: %d - %d -%d\n", actioncompl, key.rel,
	  !keys[key.keycode].groups[egroup].width);
  /* If the action comsumed the keycode, this is a key release event
     or if the key doesn't have any symbols bound to it there is no
     symbol returned.  */
  if (actioncompl == KEYCONSUMED || key.rel ||
      !keys[key.keycode].groups[egroup].width)
    return -1;

  /* Calculate the effective modmap.  */
  emods = bmods;
  emods.rmods |= lmods.rmods;
  emods.vmods |= lmods.vmods;
  emods.rmods |= latchedmods.rmods;
  emods.vmods |= latchedmods.vmods;

  level = calc_shift (key.keycode) % keys[key.keycode].groups[egroup].width;

  /* The latched modifier is used for a symbol, clear it.  */
  latchedmods.rmods = latchedmods.vmods = 0;

  /* Search the symbol for this key in the keytable. Make sure the
     group and shift level exists.  */
  sym = keys[key.keycode].groups[egroup].symbols[level];
  
  /* Convert keypad symbols to symbols. XXX: Is this the right place
     to do this? */
  if ((sym >= XK_KP_Multiply && sym <= XK_KP_Equal) || sym == XK_KP_Enter)
    sym &= ~0xFF80;

  /* Check if this keypress was a part of a compose sequence.  */
  sym = compose_symbols (sym);

  return sym;
}

void
xkb_input (keypress_t key)
{
  char buf[100];
  size_t size = 0;      
  wchar_t input;

  debug_printf ("input: %d, rel: %d, rep: %d\n", key.keycode, key.rel, key.repeat);
  
  if (key.rel)
    keystate[key.keycode].lmods = lmods;
  input = handle_key (key);

  debug_printf ("handle: %d\n", input);
  if (input == -1)
    return;

  /* If the realmodifier MOD1 (AKA Alt) is set generate an ESC
     symbol.  */
  if (emods.rmods & RMOD_MOD1)
    buf[size++] = '\e';

  buf[size] = '\0';

  if (!input)
    return;

  /* Special key, generate escape sequence.  */
  char *escseq = NULL;

  switch (input)
    {
    case XK_Up: case XK_KP_Up:
      escseq = CONS_KEY_UP;
      break;
    case XK_Down: case XK_KP_Down:
      escseq = CONS_KEY_DOWN;
      break;
    case XK_Left: case XK_KP_Left:
      escseq = CONS_KEY_LEFT;
      break;
    case XK_Right: case XK_KP_Right:
      escseq = CONS_KEY_RIGHT;
      break;
    case XK_BackSpace:
      escseq = CONS_KEY_BACKSPACE;
      break;
    case XK_F1: case XK_KP_F1:
      escseq = CONS_KEY_F1;
      break;
    case XK_F2: case XK_KP_F2:
      escseq = CONS_KEY_F2;
      break;
    case XK_F3: case XK_KP_F3:
      escseq = CONS_KEY_F3;
      break;
    case XK_F4: case XK_KP_F4:
      escseq = CONS_KEY_F4;
      break;
    case XK_F5:
      escseq = CONS_KEY_F5;
      break;
    case XK_F6:
      escseq = CONS_KEY_F6;
      break;
    case XK_F7:
      escseq = CONS_KEY_F7;
      break;
    case XK_F8:
      escseq = CONS_KEY_F8;
      break;
    case XK_F9:
      escseq = CONS_KEY_F9;
      break;
    case XK_F10:
      escseq = CONS_KEY_F10;
      break;
    case XK_F11:
      escseq = CONS_KEY_F11;
      break;
    case XK_F12:
      escseq = CONS_KEY_F12;
      break;
    case XK_F13:
      escseq = CONS_KEY_F13;
      break;
    case XK_F14:
      escseq = CONS_KEY_F14;
      break;
    case XK_F15:
      escseq = CONS_KEY_F15;
      break;
    case XK_F16:
      escseq = CONS_KEY_F16;
      break;
    case XK_F17:
      escseq = CONS_KEY_F17;
      break;
    case XK_F18:
      escseq = CONS_KEY_F18;
      break;
    case XK_F19:
      escseq = CONS_KEY_F19;
      break;
    case XK_F20:
      escseq = CONS_KEY_F20;
      break;
    case XK_Home: case XK_KP_Home:
      escseq = CONS_KEY_HOME;
      break;
    case XK_Insert: case XK_KP_Insert:
      escseq = CONS_KEY_IC;
      break;
    case XK_Delete: case XK_KP_Delete:
      escseq = CONS_KEY_DC;
      break;
    case XK_End: case XK_KP_End:
      escseq = CONS_KEY_END;
      break;
    case XK_Page_Up: case XK_KP_Page_Up:
      escseq = CONS_KEY_PPAGE;
      break;
    case XK_Page_Down: case XK_KP_Page_Down:
      escseq = CONS_KEY_NPAGE;
      break;
    case XK_KP_Begin:
      escseq = CONS_KEY_B2;
      break;
    case XK_ISO_Left_Tab:
      escseq = CONS_KEY_BTAB;
      break;
    case XK_Return: case XK_KP_Enter:
      escseq = "\x0d";
      break;
    case XK_Tab: case XK_KP_Tab:
      escseq = "\t";
      break;
    case XK_Escape:
      escseq = "\e";
      break;
    }

  if (escseq != NULL)
    {
      strcat (buf + size, escseq);
      size += strlen (escseq);
    }
  else
    {
      char *buffer = &buf[size];
      size_t left = sizeof (buf) - size;
      char *inbuf = (char *) &input;
      size_t inbufsize = sizeof (wchar_t);
      size_t nr;

      /* Control key behaviour.  */
      if (bmods.rmods & RMOD_CTRL)
	input = symtoctrlsym (input);
	  
      /* Convert the Keysym to a UCS4 characted.  */
      input = KeySymToUcs4 (input);
      /* 	      if (!input) */
      /* 		continue; */

      debug_printf ("UCS4: %d -- %c\n", (int) input, input);

      /* If CAPSLOCK is active capitalize the symbol.  */
      if (emods.rmods & 2)
      	input = towupper (input);
		      
      nr = iconv (cd, &inbuf, &inbufsize, &buffer, &left);
      if (nr == (size_t) -1)
	{
	  if (errno == E2BIG)
	    console_error (L"Input buffer overflow");
	  else if (errno == EILSEQ)
	    console_error
	      (L"Input contained invalid byte sequence");
	  else if (errno == EINVAL)
	    console_error
	      (L"Input contained incomplete byte sequence");
	  else
	    console_error
	      (L"Input caused unexpected error");
	}
      size = sizeof (buf) - left;
    }

  if (size)
    console_input (buf, size);
  size = 0;
}

error_t parse_xkbconfig (char *xkbdir, char *xkbkeymapfile, char *xkbkeymap);

error_t
xkb_load_layout (char *xkbdir, char *xkbkeymapfile, char *xkbkeymap)
{
  error_t err;

  err = parse_xkbconfig (xkbdir, xkbkeymapfile, xkbkeymap);
  if (err)
    return err;

  determine_keytypes ();
  interpret_all ();
  return 0;
}
