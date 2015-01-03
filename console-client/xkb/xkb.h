/*  Keyboard plugin for the Hurd console using XKB keymaps.

    Copyright (C) 2002,03  Marco Gerards
   
    Written by Marco Gerards <marco@student.han.nl>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.  */

#include <errno.h>
#include <argp.h>
#include <X11/Xlib.h>

typedef int keycode_t;
typedef unsigned int scancode_t;
typedef int symbol;
typedef int group_t;
typedef unsigned int boolctrls;

#define	KEYCONSUMED	1
#define	KEYNOTCONSUMED	0

#define RedirectIntoRange  1
#define ClampIntoRange     2
#define WrapIntoRange      0

typedef enum mergemode
  {
    augment,
    override,
    replace,
    alternate,
    defaultmm
  } mergemode;

extern mergemode merge_mode;

typedef unsigned long KeySym;

/* Real modifiers.  */
#define RMOD_SHIFT	1 << 0
#define RMOD_LOCK	1 << 1
#define RMOD_CTRL	1 << 2
#define RMOD_MOD1	1 << 3
#define RMOD_MOD2	1 << 4
#define RMOD_MOD3	1 << 5
#define RMOD_MOD4	1 << 6
#define RMOD_MOD5	1 << 7

/* If set the key has action(s).  */
#define KEYHASACTION (1<<4)
/* Normally the keytype will be calculated, but some keys like SYSRQ
   can't be calculated. For these keys the name for the keytypes will
   be given for every group fro whch the bit is set.  */
#define KEYHASTYPES 0xf
#define KEYHASBEHAVIOUR (1<<5)
/* Will the key be repeated when held down, or not.  */
#define KEYREPEAT (1<<6)
#define KEYNOREPEAT (1<<7)

/* The complete set of modifiers.  */
typedef struct modmap
{
  /* Real modifiers.  */
  int rmods;
  /* Virtual modifiers.  */
  int vmods;
} modmap_t;

/* Modifier counter.  */
typedef struct modcount
{
  int rmods[8];
  int vmods[16];
} modcount_t;

/* Map modifiers to a shift level.  */
typedef struct typemap
{
  /* Shift level used when required modifiers match the active modifiers.  */
  int level;
  modmap_t mods;
  modmap_t preserve;
  struct typemap *next;
} typemap_t;

/* The keypad symbol range.  */
#define KEYPAD_FIRST_KEY 0xFF80
#define KEYPAD_LAST_KEY  0xFFB9
#define KEYPAD_MASK      0xFF80

/* Convert a keypad symbol to a ASCII symbol.  */
#define keypad_to_ascii(c) c = (c & (~KEYPAD_MASK))

/* The default keytypes. These can be calculated.  */
#define KT_ONE_LEVEL 0
#define KT_TWO_LEVEL 1
#define KT_ALPHA     2
#define KT_KEYPAD    3

typedef struct keytype
{
  /* Mask that determines which modifiers should be checked. */
  modmap_t modmask;
  /* Amount of levels. */
  int levels;
  /* The required set of modifiers for one specific level.  */
  struct typemap *maps;

  char *name;
  struct keytype *hnext;
  struct keytype **prevp;
} keytype_t;

extern struct keytype *keytypes;
extern int keytype_count;

/* All Actions as described in the protocol specification.  */
typedef enum actiontype
  {
    SA_NoAction,
    SA_SetMods,
    SA_LatchMods,
    SA_LockMods,
    SA_SetGroup,
    SA_LatchGroup,
    SA_LockGroup,
    SA_MovePtr,
    SA_PtrBtn,
    SA_LockPtrBtn,
    SA_SetPtrDflt,
    SA_ISOLock,
    SA_TerminateServer,
    SA_SwitchScreen,
    SA_SetControls,
    SA_LockControls,
    SA_ActionMessage,
    SA_RedirectKey,
    SA_DeviceBtn,
    SA_LockDeviceBtn,
    SA_DeviceValuator,
    SA_ConsScroll
  } actiontype_t;

typedef struct xkb_action
{
  actiontype_t type;
  int data[15];
} xkb_action_t;

#define	useModMap	4
#define	clearLocks	1
#define	latchToLock	2   
#define	noLock		1
#define	noUnlock	2      
#define	groupAbsolute	4
#define	NoAcceleration	1
#define	MoveAbsoluteX	2
#define	MoveAbsoluteY	4
/* XXX: This flag is documentated and I've implemented it. Weird
   stuff.  */
#define useDfltBtn	0
#define DfltBtnAbsolute	2
#define	AffectDfltBtn	1
#define switchApp	1
#define screenAbs	4
#define lineAbs		2
#define usePercentage	8

/* Defines how symbols and rmods are interpreted.  This is used to
   bound an action to a key that doesn't have an action bound to it,
   only a modifier or action describing symbol.  */
typedef struct xkb_interpret
{
  symbol symbol;
  int rmods;
  int match;
  int vmod; /* XXX: Why does this field have a size of only 8 bits?  */
  int flags;
  struct xkb_action action;
  struct xkb_interpret *next;
} xkb_interpret_t;

extern xkb_interpret_t *interpretations;
extern int interpret_count;

/* These are the parameter names that are used by the actions that
   control modifiers. (this is stored in the data field of
   xkb_action)*/
typedef struct action_setmods
{
  actiontype_t type;
  /*  The flags configure the behaviour of the action.  */
  int flags;
  /* XXX: The real modifiers that can be controlled by this action.  */
  int modmask;
  /* The modifiers that are will be set/unset by this action.  */
  modmap_t modmap;
} action_setmods_t;

typedef struct action_setgroup
{
  actiontype_t type;
  int flags;
  int group;
} action_setgroup_t;

typedef struct action_moveptr
{
  actiontype_t type;
  int flags;
  int x;
  int y;
} action_moveptr_t;

typedef struct action_ptrbtn
{
  actiontype_t type;
  int flags;
  int count; /* Isn't used for LockPtrBtn.  */
  int button;
} action_ptrbtn_t;

typedef struct action_ptr_dflt
{
  actiontype_t type;
  int flags;
  int affect;
  int value;
} action_ptr_dflt_t;

typedef struct action_switchscrn
{
  actiontype_t type;
  int flags;
  int screen;
} action_switchscrn_t;

typedef struct action_consscroll
{
  actiontype_t type;
  int flags;
  double screen;
  int line;
  int percent;
} action_consscroll_t;

typedef struct action_redirkey
{
  actiontype_t type;
  int newkey;
  int rmodsmask;
  int rmods;
  int vmodsmask;
  int vmods;
} action_redirkey_t;

typedef struct action_setcontrols
{
  actiontype_t type;
  int controls;
} action_setcontrols_t;

/* Every key can have 4 groups, this is the information stored per
   group.  */
struct keygroup
{
  /* All symbols for every available shift level and group.  */
  symbol *symbols;
  /* All actions for every available shift level and group. */
  struct xkb_action **actions;
  /* The keytype of this key. The keytype determines the available
     shift levels and which modiers are used to set the shift level.
   */
  struct keytype *keytype;
  /* Amount of symbols held in this group of this key.  */
  int width;
  int actionwidth;
};

/*  A single key scancode stored in memory.  */
typedef struct key
{
  /* The flags that can be set for this key (To change the behaviour
     of this key). */ 
  int flags;
  /* Every key has a maximum of 4 groups.  (XXX: According to Ivan
     Pascal's documentation... I'm not really sure if that is true.)  */
  struct keygroup groups[4];
  int numgroups;
  struct modmap mods;
} keyinf_t;

extern struct key *keys;
extern int min_keys;
extern int max_keys;

/* The current state of every key.  */
typedef struct keystate
{
  /* Key is pressed.  */
  unsigned short keypressed:1;
  unsigned short prevstate:1;
  /* The key was disabled for bouncekeys.  */
  unsigned short disabled:1;
  /* Information about locked modifiers at the time of the keypress,
     this information is required for unlocking when the key is released.  */
  modmap_t lmods;
  /* The modifiers and group that were active at keypress, make them
     active again on keyrelease so the action will be undone.  */
  modmap_t prevmods;
  boolctrls bool;
  group_t prevgroup;
  group_t oldgroup;
} keystate_t;

extern struct keystate keystate[255];

typedef struct keypress
{
  keycode_t keycode;
  keycode_t prevkc;
  unsigned short repeat:1;	/* It this a real keypress?.  */
  unsigned short redir:1;	/* This is not a real keypress.  */
  unsigned short rel;		/* Key release.  */
} keypress_t;

/* Flags for indicators.  */
#define	IM_NoExplicit	0x80
#define	IM_NoAutomatic	0x40
#define	IM_LEDDrivesKB	0x20

#define	IM_UseCompat	0x10
#define	IM_UseEffective	0x08
#define	IM_UseLocked	0x04
#define	IM_UseLatched	0x02
#define	IM_UseBase	0x01


typedef struct xkb_indicator
{
  int flags;
  int which_mods;
  modmap_t modmap;
  int which_groups;
  int groups;
  unsigned int ctrls;
} xkb_indicator_t;

unsigned int KeySymToUcs4(int keysym);
symbol compose_symbols (symbol symbol);
error_t read_composefile (char *);
struct keytype *keytype_find (char *name);

void key_set_action (struct key *key, group_t group, int level,
		     xkb_action_t *action);


/* Interfaces for xkbdata.c:  */
extern struct xkb_interpret default_interpretation;


/* Assign the name KEYNAME to the keycode KEYCODE.  */
error_t keyname_add (char *keyname, int keycode);

/* Find the numberic representation of the keycode with the name
   KEYNAME.  */
int keyname_find (char *keyname);

/* Search the keytype with the name NAME.  */
struct keytype *keytype_find (char *name);

/* Remove the keytype KT.  */
void keytype_delete (struct keytype *kt);

/* Create a new keytype with the name NAME.  */
error_t keytype_new (char *name, struct keytype **new_kt);

/* Add a level (LEVEL) to modifiers (MODS) mapping to the current
   keytype.  */
error_t keytype_mapadd (struct keytype *kt, modmap_t mods, int level);

/* For the current keytype the modifiers PRESERVE should be preserved
   when the modifiers MODS are pressed.  */
error_t keytype_preserve_add (struct keytype *kt, modmap_t mods,
			      modmap_t preserve);

/* Add a new interpretation.  */
error_t interpret_new (xkb_interpret_t **new_interpret, symbol ks);

/* Get the number assigned to the virtualmodifier with the name
   VMODNAME.  */
int vmod_find (char *vmodname);

/* Give the virtualmodifier VMODNAME a number and add it to the
   hashtable.  */
error_t vmod_add (char *vmodname);

/* Initialize the list for keysyms to realmodifiers mappings.  */
void ksrm_init ();

/* Add keysym to realmodifier mapping.  */
error_t ksrm_add (symbol ks, int rmod);

/* Apply the rkms (realmods to keysyms) table to all keysyms.  */
void ksrm_apply (void);

/* Set the current rmod for the key with keyname KEYNAME.  */
/* XXX: It shouldn't be applied immediately because the key can be
   replaced.  */
void set_rmod_keycode (char *keyname, int rmod);

/* Initialize XKB data structures.  */
error_t xkb_data_init (void);

error_t xkb_input_key (int key);

error_t xkb_init_repeat (int delay, int repeat);

void xkb_input (keypress_t key);

int debug_printf (const char *f, ...);

error_t xkb_load_layout (char *xkbdir, char *xkbkeymapfile, char *xkbkeymap);
