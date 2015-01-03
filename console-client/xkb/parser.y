/*  parser.y -- XKB parser.

    Copyright (C) 2003, 2004  Marco Gerards
   
    Written by Marco Gerards <marco@student.han.nl>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.  */

%{
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "xkb.h"

void yyerror(char *);
int yylex (void);
static error_t include_section (char *incl, int sectionsymbol, char *dirname,
				mergemode);
static error_t include_sections (char *incl, int sectionsymbol, char *dirname,
				 mergemode);
void close_include ();
static void skipsection (void);
static error_t set_default_action (struct xkb_action *, struct xkb_action **);
static void key_set_keysym (struct key *key, group_t group, int level,
			    symbol ks);
static void key_new (char *keyname);
static void key_delete (char *keyname);
void scanner_unput (int c);
static void remove_symbols (struct key *key, group_t group);

struct xkb_interpret *current_interpretation;
struct xkb_action *current_action;
struct xkb_indicator indi;
struct xkb_indicator *current_indicator = &indi;
struct key defkey;
struct key *default_key = &defkey;

/* The default settings for actions.  */
struct xkb_action default_setmods = { SA_SetMods };
struct xkb_action default_lockmods = { SA_LockMods };
struct xkb_action default_latchmods = { SA_LatchMods };
struct xkb_action default_setgroup = { SA_SetGroup };
struct xkb_action default_latchgroup = { SA_LatchGroup };
struct xkb_action default_lockgroup = { SA_LockGroup };
struct xkb_action default_moveptr = { SA_MovePtr };
struct xkb_action default_ptrbtn = { SA_PtrBtn };
struct xkb_action default_lockptrbtn = { SA_LockPtrBtn };
struct xkb_action default_ptrdflt = { SA_SetPtrDflt };
struct xkb_action default_setcontrols = { SA_SetControls };
struct xkb_action default_lockcontrols = { SA_LockControls };
struct xkb_action default_isolock = { SA_ISOLock };
struct xkb_action default_switchscrn = { SA_SwitchScreen };
struct xkb_action default_consscroll = { SA_ConsScroll };

static struct key *current_key;

/* The dummy gets used when the original may not be overwritten.  */
static struct key dummy_key;

/* The current parsed group.  */
static int current_group;
static int current_rmod = 0;

/* The current symbol in the currently parsed key.  */
static int symbolcnt;
static int actioncnt;

mergemode merge_mode = override;

//#define	YYDEBUG	1

#ifndef YY_NULL
#define YY_NULL 0
#endif

static struct keytype *current_keytype;
%}

%union {
  int val;
  char *str;
  modmap_t modmap;
  struct xkb_action *action;
  double dbl;
  mergemode mergemode;
}

%token XKBKEYMAP	"xkb_keymap"
%token XKBKEYCODES	"xkb_keycodes"
%token XKBCOMPAT	"xkb_compatibility"
%token XKBGEOMETRY	"xkb_geometry"
%token XKBTYPES		"xkb_types"
%token XKBSYMBOLS	"xkb_symbols"
%token STR
%token HEX
%token FLAGS
%token KEYCODE
%token NUM
%token MINIMUM		"minimum"
%token MAXIMUM		"maximum"
%token VIRTUAL		"virtual"
%token INDICATOR	"indicator"
%token ALIAS		"alias"
%token IDENTIFIER
%token VMODS		"virtualmods"
%token TYPE		"type"
%token DATA		"data"
%token MAP		"map"
%token LEVEL_NAME	"level_name"
%token PRESERVE		"preserve"
%token LEVEL
%token USEMODMAP	"usemodmap"
%token REPEAT		"repeat"
%token LOCKING		"locking"
%token VIRTUALMODIFIER	"virtualmod"
%token BOOLEAN
%token INTERPRET	"interpret"
%token INTERPMATCH
%token CLEARLOCKS	"clearlocks"
%token MODS		"mods"
%token SETMODS		"setmods"
%token LATCHMODS	"latchmods"
%token LOCKMODS		"lockmods"
%token ACTION		"action"
%token LATCHTOLOCK	"latchtolock"
%token GROUP		"group"
%token GROUPS		"groups"
%token SETGROUP		"setgroup"
%token LATCHGROUP	"latchgroup"
%token LOCKGROUP	"lockgroup"
%token ACCEL		"accel"
%token MOVEPTR		"moveptr"
%token PRIVATE		"private"
%token BUTTON		"button"
%token BUTTONNUM
%token DEFAULT		"default"
%token COUNT		"count"
%token PTRBTN		"ptrbtn"
%token DEFAULTBTN	"defaultbutton"
%token ALL		"all"
%token NONE		"none"
%token ANY		"any"
%token CONTROLFLAG
%token AFFECT		"affect"
%token PTRDFLT		"setptrdflt"
%token LOCKPTRBTN	"lockptrbtn"
%token SETCONTROLS	"setcontrols"
%token LOCKCONTROLS	"lockcontrols"
%token CONTROLS		"controls"
%token TERMINATE	"terminate"
%token WHICHMODSTATE	"whichmodstate"
%token WHICHGROUPSTATE	"whichgroupstate"
%token WHICHSTATE	"whichstate"
%token INDEX		"index"
%token ALLOWEXPLICIT	"allowexplicit"
%token DRIVESKBD	"driveskbd"
//%token AFFECTBTNLOCK
%token SYMBOLS		"symbols"
%token NAME		"name"
%token GROUPNUM
%token ACTIONS		"actions"
%token KEY		"key"
%token MODMAP		"modifier_map"
%token SHIFT		"shift"
%token LOCK		"lock"
%token CONTROL		"control"
%token MOD1		"mod1"
%token MOD2		"mod2"
%token MOD3		"mod3"
%token MOD4		"mod4"
%token MOD5		"mod5"
%token UNLOCK		"unlock"
%token BOTH		"both"
%token NEITHER		"neither"

%token INCLUDE		"include"
%token AUGMENT		"augment"
%token OVERRIDE		"override"
%token REPLACE		"replace"


%token ISOLOCK		"isolock"
%token POINTERS		"pointers"
%token NOACTION		"noaction"
%token GROUPSWRAP	"groupswrap"
%token GROUPSCLAMP	"groupsclamp"
%token GROUPSREDIRECT	"groupsredirect"
%token OVERLAY		"overlay"
%token SWITCHSCREEN	"switchscreen"
%token SAMESERVER	"sameserver"
%token SCREEN		"screen"
%token LINE		"line"
%token PERCENT		"percent"
%token CONSSCROLL	"consscroll"
%token FLOAT		"float"
%type <str> STR KEYCODE IDENTIFIER
%type <val> FLAGS NUM HEX vmod level LEVEL rmod BOOLEAN symbol INTERPMATCH
%type <val> clearlocks usemodmap latchtolock noaccel button BUTTONNUM
%type <val> ctrlflags allowexplicit driveskbd
%type <val> DRIVESKBD GROUPNUM group symbolname  groups whichstate WHICHSTATE
/* Booleans */
%type <val> locking repeat groupswrap groupsclamp sameserver
%type <modmap> mods
%type <action> action
%type <action> ctrlparams
%type <mergemode> include
%type <dbl> FLOAT
//%debug

%%
/* A XKB keymap.  */
xkbkeymap:
  "xkb_keymap" '{' keymap '}' ';' { YYACCEPT; }
| "xkb_keymap" STR '{' keymap '}' ';' { YYACCEPT; }
| '{' keymap '}' { YYACCEPT; } ';'
;

/* A XKB configuration has many sections. */
keymap:
	/* empty */
| keymap types
| keymap keycodes
| keymap compat
| keymap symbols
| keymap geometry
;

include:
  "include"  { $$ = defaultmm; }
| "augment"  { $$ = augment;   }
| "replace"  { $$ = replace;   }
| "override" { $$= override;   }
;

/* All flags assigned to a section.  */
flags:
  /* empty */
| flags FLAGS
;

/* The header of a keycode section.  */
keycodes:
  flags "xkb_keycodes" '{' keycodesect '}' ';'
| flags "xkb_keycodes" STR '{' keycodesect '}' ';'
;

/* Process the includes on the stack.  */
keycodesinclude:
  '{' keycodesect '}'			 { close_include (); }
| keycodesinclude '{' keycodesect '}'	 { close_include (); }
;

/* The first lines of a keycode section. The amount of keycodes are
   declared here.  */
keycodesect:
/* empty */
| "minimum" '=' NUM ';' keycodesect
   { 
     min_keys = $3;
     debug_printf ("working on key: %d\n", $3);
     current_key = &keys[$3];
   }
| MAXIMUM '=' NUM ';' keycodesect 
   { 
     max_keys = $3;
     keys = calloc ($3, sizeof (struct key));
   }
| KEYCODE '=' NUM ';'
   { keyname_add ($1, $3); }
  keycodesect	
| "replace" KEYCODE '=' NUM ';'
   { keyname_add ($2, $4); }
  keycodesect
| "indicator" NUM '=' STR ';' keycodesect {  }
| "virtual" INDICATOR NUM '=' STR ';' keycodesect
| "alias" KEYCODE '=' KEYCODE ';'
   { 
     keycode_t key = keyname_find ($4);
     if (key)
       keyname_add ($2, key);
     else
       {
	 key = keyname_find ($2);
	 if (key)
	   keyname_add ($4, key);
       }
   }
  keycodesect
| include STR 
   { include_sections ($2, XKBKEYCODES, "keycodes", $1); }
  keycodesinclude keycodesect
;

/* The header of a keytypes section.  */
types:
  flags "xkb_types" '{' typessect '}' ';'
| flags "xkb_types" STR '{' typessect '}' ';'
;

/* A list of virtual modifier declarations (see vmods_def), separated
   by commas.  */
vmodslist:
  IDENTIFIER { vmod_add ($1); }
| vmodslist ',' IDENTIFIER { vmod_add ($3); }
;

/* Virtual modifiers must be declared before they can be used.  */
vmods_def:
  "virtualmods" vmodslist ';'
;

/* Return the number of the virtual modifier.  */
vmod:
	IDENTIFIER
	{ if (($$ = vmod_find ($1)) != 0)
	    $$ = 1 << ($$ - 1);
	  else
	    fprintf(stderr, "warning: %s virtual modifier is not defined.", $1);
	}
;

/* A single realmodifier.  */
rmod:
  "shift" 	{ $$ = RMOD_SHIFT; }
| "lock" 	{ $$ = RMOD_LOCK; }
| "control"	{ $$ = RMOD_CTRL; }
| "mod1"	{ $$ = RMOD_MOD1; }
| "mod2"	{ $$ = RMOD_MOD2; }
| "mod3"	{ $$ = RMOD_MOD3; }
| "mod4"	{ $$ = RMOD_MOD4; }
| "mod5"	{ $$ = RMOD_MOD5; }
;

/* One of more modifiers, separated by '+'. A modmap_t will return all real
   and virtual modifiers specified.  */
mods:
  mods '+' rmod { $$.rmods = $1.rmods | $3; }
| mods '+' vmod { $$.vmods = $1.vmods | $3; }
	/* Use a mid-rule action to start with no modifiers.  */
| { $<modmap>$.rmods = 0; $<modmap>$.vmods = 0; } rmod		{ $$.rmods = $2; }
| { $<modmap>$.rmods = 0; $<modmap>$.vmods = 0; } vmod 		{ $$.vmods = $2; }
| "all"	{ $$.rmods = 0xFF; $$.vmods = 0xFFFF; }
| "none"  { $$.rmods = 0; $$.vmods = 0; }
;

/* The numeric level starts with 0. Level1-Level4 returns 0-3, also
   numeric values can be used to describe a level.  */
level:
  LEVEL	{ $$ = $1 - 1; }
| "any" { $$ = 0;      }
| NUM	{ $$ = $1 - 1; }
;

/* A single keytype.  */
type:
	/* Empty */
| type MODS '=' mods ';'
   { current_keytype->modmask = $4; }
| type MAP '[' mods ']' '=' level ';' 	     
   { keytype_mapadd (current_keytype, $4, $7); }
| type "level_name" '[' level ']' '=' STR ';'
| type "preserve" '[' mods ']' '=' mods ';'    
   { keytype_preserve_add (current_keytype, $4, $7); }
;

/* Process the includes on the stack.  */
typesinclude:
  '{' typessect '}'			 { close_include (); }
| typesinclude '{' typessect '}'	 { close_include (); }
;

/* A keytype section contains keytypes and virtual modifier declarations.  */
typessect:
	/* Empty */
| typessect vmods_def
| typessect TYPE STR { keytype_new ($3, &current_keytype); }'{' type '}' ';' { }
| typessect include STR 
   { include_sections ($3, XKBTYPES, "types", $2); }
  typesinclude
;

/* The header of a compatibility section.  */
compat:
  flags "xkb_compatibility"     '{' compatsect '}' ';'
| flags "xkb_compatibility" STR '{' compatsect '}' ';'
;

/* XXX: A symbol can be more than just an identifier (hex).  */
symbol:
  IDENTIFIER		{ $$ = (int) XStringToKeysym ( $1) ? : -1;  }
| ANY 			{ $$ = 0;  }
| error 		{ yyerror ("Invalid symbol."); }
;

/* Which kinds of modifiers (like base, locked, etc.) can affect the
   indicator.  */
whichstate:
  WHICHSTATE { $$ = $1; }
| "none"     { $$ = 0; }
| "any"      { $$ = 0xff; } /* XXX */
;  

/* The groups that can affect a indicator.  */
groups:
  groups '+' group
| groups '-' group
| group {}
| "all" { $$ = 0xff; } /* XXX */
;

indicators:
/* empty */
| indicators indicator
;

/* An indicator desciption.  */
indicator:
  "mods" '=' mods ';' { current_indicator->modmap = $3; }
| "groups" '=' groups ';' { current_indicator->groups = $3; }
| "controls" '=' ctrls ';'
| "whichmodstate" '=' whichstate ';' { current_indicator->which_mods = $3; }
| "whichgroupstate" '=' whichstate ';' { current_indicator->which_mods = $3; }
| allowexplicit ';' {} /* Ignored for now.  */
| driveskbd ';' {}
| "index" '=' NUM ';' {}
;

/* Boolean for allowexplicit.  */
allowexplicit:
  '~' "allowexplicit"		 { $$ = 0;  }
| '!' "allowexplicit"		 { $$ = 0;  }
| "allowexplicit"		 { $$ = 1;  }
| "allowexplicit" '=' BOOLEAN	 { $$ = $3; }
;

/* Boolean for driveskbd.  */
driveskbd:
  '~' "driveskbd"		 { $$ = 0;  }
| '!' "driveskbd"		 { $$ = 0;  }
| "driveskbd"			 { $$ = 1;  }
| "driveskbd" '=' BOOLEAN	 { $$ = $3; }
;

interprets:
  /* Empty */
| interprets interpret
;

/* A single interpretation.  */
interpret:
  "usemodmap" '=' level ';'
	{ current_interpretation->match &= 0x7F | ($3 << 7); } /* XXX */
| repeat ';'
  {
    current_interpretation->flags &= ~(KEYREPEAT | KEYNOREPEAT);
    current_interpretation->flags |= $1;
  }
| locking ';' {}
| "virtualmod" '=' vmod ';' 	{ current_interpretation->vmod = $3; }
| "action" '=' action ';' 	
   { 
     memcpy (&current_interpretation->action, $3, sizeof (xkb_action_t));
     free ($3);
   }
;

/* Process the includes on the stack.  */
compatinclude:
  '{' compatsect '}'			 { close_include (); }
| compatinclude '{' compatsect '}'	 { close_include (); }
;

/* The body of a compatibility section.  */
compatsect:
	/* Empty */
| compatsect vmods_def
| compatsect "interpret" '.' 
   { current_interpretation = &default_interpretation; }
  interpret
| compatsect "interpret" symbol 
	{ 
	  if ($3 != -1)
	    {
	      interpret_new (&current_interpretation, $3);
	      current_interpretation->match |= 1;
	    }
	}
  '{' interprets '}' ';'
| compatsect "interpret" symbol '+' rmod
	{
	  if ($3 != -1)
	    {
	      interpret_new (&current_interpretation, $3);
	      current_interpretation->rmods = $5;
	      current_interpretation->match |= 4;
	    }
	}
  '{' interprets '}' ';'
| compatsect "interpret" symbol '+' "any"
	{
	  if ($3 != -1)
	    {
	      interpret_new (&current_interpretation, $3);
	      current_interpretation->rmods = 255;
	      current_interpretation->match |= 2;
	    }
	}
  '{' interprets '}' ';'
| compatsect "interpret" symbol '+' INTERPMATCH '(' mods ')'
        {
	  if ($3 != -1)
	    {
	      interpret_new (&current_interpretation, $3);
	      current_interpretation->rmods = $7.rmods;
	      current_interpretation->match |= $5;
	    }
	}
  '{' interprets '}' ';'
| compatsect GROUP NUM '=' mods ';'
| compatsect "indicator" STR '{' indicators '}' ';'
| compatsect include STR
   { include_sections ($3, XKBCOMPAT, "compat", $2); }
  compatinclude
| compatsect actiondef
| compatsect "indicator" '.' indicator
;


	/* Booleans  */
/* Boolean for clearlocks.  */
clearlocks:
  '~' "clearlocks"		 { $$ = 0; }
| '!' "clearlocks"		 { $$ = 0; }
| "clearlocks"			 { $$ = 1; }
| "clearlocks" '=' BOOLEAN	 { $$ = $3; }
;

/* Boolean for latchtolock.  */
latchtolock:
  '~' "latchtolock"		 { $$ = 0; }
| '!' "latchtolock"		 { $$ = 0; }
| "latchtolock"			 { $$ = 1; }
| "latchtolock" '=' BOOLEAN	 { $$ = $3; }
;

/* Boolean for useModMap.  */
usemodmap:
  '~' "usemodmap"	 	 { $$ = 0; }
| '!' "usemodmap"		 { $$ = 0; }
| "usemodmap"			 { $$ = 1; }
| "usemodmap" '=' BOOLEAN 	 { $$ = $3; }
;

/* Boolean for locking.  */
locking:
  '~' "locking"		 	 { $$ = 0; }
| '!' "locking"		 	 { $$ = 0; }
| "locking"			 { $$ = 1; }
| "locking" '=' BOOLEAN		 { $$ = $3; }
;

/* Boolean for repeat.  */
repeat:
  '~' "repeat"			 { $$ = KEYNOREPEAT; }
| '!' "repeat"			 { $$ = KEYNOREPEAT; }
| "repeat"			 { $$ = KEYREPEAT;   }
| "repeat" '=' BOOLEAN	 
  {
    if ($3)
      $$ = KEYREPEAT;
    else
      $$ = KEYNOREPEAT;
  }
;

/* Boolean for groupswrap.  */
groupswrap:
  '~' "groupswrap"		 { $$ = 0; }
| '!' "groupswrap"		 { $$ = 0; }
| "groupswrap"			 { $$ = 1; }
| "groupswrap" '=' BOOLEAN	 { $$ = $3; }
;

/* Boolean for groupsclamp.  */
groupsclamp:
  '~' "groupsclamp"		 { $$ = 0;  }
| '!' "groupsclamp"		 { $$ = 0;  }
| "groupsclamp"			 { $$ = 1;  }
| "groupsclamp" '=' BOOLEAN	 { $$ = $3; }
;

/* Boolean for noaccel.  */
noaccel:
  '~' "accel"		 	 { $$ = 0;  }
| '!' "accel"		 	 { $$ = 0;  }
| "accel"		 	 { $$ = 1;  }
| "accel" '=' BOOLEAN	 	 { $$ = $3; }
;


sameserver:
  '~' "sameserver"		 { $$ = 0;  }
| '!' "sameserver"		 { $$ = 0;  }
| "sameserver"			 { $$ = 1;  }
| "sameserver" '=' BOOLEAN 	 { $$ = $3; }
;

setmodsparams:
 /* empty */
| setmodsparam
| setmodsparams ',' setmodsparam
;


/* Parameter for the (Set|Lock|Latch)Mods action.  */
setmodsparam:
  "mods" '=' mods		
  { 
    ((action_setmods_t *) current_action)->modmap = $3;
  }    
| "mods" '=' "usemodmap"
  { ((action_setmods_t *) current_action)->flags |= useModMap;
 }
| clearlocks
  {
    ((action_setmods_t *) current_action)->flags &= ~clearLocks;
    ((action_setmods_t *) current_action)->flags |= $1;
  }
| usemodmap
  { 
    ((action_setmods_t *) current_action)->flags &= ~useModMap;
    ((action_setmods_t *) current_action)->flags |= $1;
  }
| latchtolock
  { 
    ((action_setmods_t *) current_action)->flags &= ~latchToLock;
    ((action_setmods_t *) current_action)->flags |= $1;
  }
;

setgroupparams:
/* empty */
| setgroupparam
| setgroupparams ',' setgroupparam
;

/* Parameter for the (Set|Lock|Latch)Group action.  */
setgroupparam:
  "group" '=' NUM
   {
     ((action_setgroup_t *) current_action)->group = $3;
     ((action_setgroup_t *) current_action)->flags |= groupAbsolute;
   }
| "group" '=' '+' NUM
   {
     ((action_setgroup_t *) current_action)->group = $4;
   }
| "group" '=' '-' NUM
   {
     ((action_setgroup_t *) current_action)->group = -$4;
   }
| clearlocks
   {
     ((action_setgroup_t *) current_action)->flags |= $1;
   }
| latchtolock
   {
     ((action_setgroup_t *) current_action)->flags |= $1;
   }
;

moveptrparams:
/* empty */
| moveptrparam
| moveptrparams ',' moveptrparam
;

/* Parameters for the MovePtr action.  */
moveptrparam:
  IDENTIFIER '=' NUM
   {
     ((action_moveptr_t *) current_action)->x = $3;
     ((action_setgroup_t *) current_action)->flags |= MoveAbsoluteX;
   }
| IDENTIFIER '=' '+' NUM
   {
     ((action_moveptr_t *) current_action)->x = $4;
   }
| IDENTIFIER '=' '-' NUM
   {
     ((action_moveptr_t *) current_action)->x = -$4;
   }
| noaccel
   {
     ((action_moveptr_t *) current_action)->flags |= NoAcceleration;
   }
;

/* A mouse button.  */
button:
  NUM 			{ $$ = $1; }
| BUTTONNUM		{ $$ = $1; }
| "default"		{ $$ = 0;  }
;

affectbtnlock:
  "lock"
| "unlock"  
| "both"
| "neither"
;  

ptrbtnparams:
  /* empty */
| ptrbtnparam
| ptrbtnparams ',' ptrbtnparam
;

/* Parameters for the (Set|Lock|Latch)PtrBtn action.  */
ptrbtnparam:
  "button" '=' button
   { ((action_ptrbtn_t *) current_action)->button = $3; }
| "count" '=' NUM
   { ((action_ptrbtn_t *) current_action)->count = $3;  }
| "affect" '=' affectbtnlock
   {
     //     ((action_ptrbtn_t *) $$)->a = $3;
   }
;

/* XXX: This should be extended.  */
affectbtns:
  "defaultbutton"
| "button"
| "all"
;

ptrdfltparams:
/* empty */
| ptrdfltparam
| ptrdfltparams ',' ptrdfltparam
;

/* Parameters for the SetPtrDflt action.  */
ptrdfltparam:
  "button" '=' button { }
| "button" '=' '+' button { }
| "button" '=' '-' button { }
| "affect" '=' affectbtns { }
;

/* A list of controlflags.  */
ctrls:
  ctrls '+' CONTROLFLAG// { $$ |= $3; }
| CONTROLFLAG// 		{ $$ = $1;  }
| OVERLAY
;

/* Modified controlflags.  */
ctrlflags:
  ctrls { /*$$ = $1;*/ 	}
| "all" 	{ $$ = 0xFFFF; 	}
| "none" 	{ $$ = 0; 	}
;

/* The parameters of a (Set|Lock|Latch)Ctrls Action.  */
ctrlparams:
  "controls" '=' ctrlflags
    { /* ((action_setcontrols_t *) $$)->controls = $3; */ } 
;

isoaffect:
  "mods"
| "groups"
| "controls"
| "pointers"
| "all"
| "none"
;

isolockparams:
/* empty */
| isolockparam
| isolockparams ',' isolockparam
;

/* Parameters for the ISOLock action.  */
isolockparam:
  "mods" '=' mods		
| "mods" '=' USEMODMAP
| "group" '=' group
| "controls" '=' ctrlflags
| "affect" '=' isoaffect
;

switchscrnparams:
  switchscrnparam 
  | switchscrnparams ',' switchscrnparam
;

/* Parameters for the SwitchScreen action.  */
switchscrnparam:
  "screen" '=' NUM
   {
     ((action_switchscrn_t *) current_action)->screen = $3;
     ((action_switchscrn_t *) current_action)->flags |= screenAbs;
   }
| "screen" '+' '=' NUM
   {
     ((action_switchscrn_t *) current_action)->screen = $4;
   }
| "screen" '-' '=' NUM
   {
     ((action_switchscrn_t *) current_action)->screen = -$4;
   }
| sameserver
   {
     /* XXX: Implement this.  */
/*      ((action_switchscrn_t *) current_action)->flags &= ~0; */
/*      ((action_switchscrn_t *) current_action)->flags |= $1; */
   }
;  

consscrollparams:
   consscrollparam
 | consscrollparams ',' consscrollparam
;

/* Parameters for the ConsScroll action.  */
consscrollparam:
  "screen" '+' '=' FLOAT
   {
     ((action_consscroll_t *) current_action)->screen = $4;
   }
| "screen" '-' '=' FLOAT
   {
     ((action_consscroll_t *) current_action)->screen = -$4;
   }
| "line" '=' NUM
   {
     ((action_consscroll_t *) current_action)->line = $3;
     ((action_consscroll_t *) current_action)->flags |= lineAbs;
   }
| "line" '+' '=' NUM
   {
     ((action_consscroll_t *) current_action)->line = $4;
   }
| "line" '-' '=' NUM
   {
     ((action_consscroll_t *) current_action)->line = -$4;
   }
| "percent" '=' NUM
   {
     ((action_consscroll_t *) current_action)->percent = $3;
     ((action_consscroll_t *) current_action)->flags |= usePercentage;     
   }
;

privateparams:
   /* empty  */
 | privateparam
 | privateparams ',' privateparam
;

privateparam:
  "type" '=' HEX
    {
    }
| "data" '=' STR
    {
    }
;

/* An action definition.  */
action:
  "setmods" 
   { 
     if (set_default_action (&default_setmods, &current_action))
       YYABORT;
   }
  '(' setmodsparams ')'		{ $$ = current_action; }
| "latchmods" 
   { 
     if (set_default_action (&default_latchmods, &current_action))
       YYABORT;
   }
  '(' setmodsparams ')' 	{ $$ = current_action; }
| "lockmods"
   {
     if (set_default_action (&default_lockmods, &current_action))
       YYABORT;
   }
  '(' setmodsparams ')' 	{ $$ = current_action; }
| "setgroup"
   {
     if (set_default_action (&default_setgroup, &current_action))
       YYABORT;
   }
  '(' setgroupparams ')' 	{ $$ = current_action; }
| "latchgroup" 
   { 
     if (set_default_action (&default_latchgroup, &current_action))
       YYABORT;
   }
  '(' setgroupparams ')' 	{ $$ = current_action; }
| "lockgroup"
   {
     if (set_default_action (&default_lockgroup, &current_action))
       YYABORT;
   }
     '(' setgroupparams ')' 	{ $$ = current_action; }
| "moveptr"
   { 
     if (set_default_action (&default_moveptr, &current_action))
       YYABORT;
   }
  '(' moveptrparams ')' 	{ $$ = current_action; }
| "ptrbtn"
   {
     if (set_default_action (&default_ptrbtn, &current_action))
       YYABORT;
   }
  '(' ptrbtnparams ')' 		{ $$ = current_action; }
| "lockptrbtn"
   {
     if (set_default_action (&default_lockptrbtn, &current_action))
       YYABORT;
   }
  '(' ptrbtnparams ')' 		{ $$ = current_action; }
| "setptrdflt"
   {
     if (set_default_action (&default_ptrdflt, &current_action))
       YYABORT;
   }
  '(' ptrdfltparams ')'	 	{ $$ = current_action; }
| "setcontrols"
   {
     if (set_default_action (&default_setcontrols, &current_action))
       YYABORT;
   }
  '(' ctrlparams ')'	 	{ $$ = current_action; }
| "lockcontrols"
   { 
     if (set_default_action (&default_lockcontrols, &current_action))
       YYABORT;
   }
  '(' ctrlparams ')'	 	{ $$ = current_action; }
| "terminate" '(' ')'
   { $$ = calloc (1, sizeof (xkb_action_t)); $$->type = SA_TerminateServer; }
| "switchscreen"
   {
     if (set_default_action (&default_switchscrn, &current_action))
       YYABORT;
   }
'(' switchscrnparams ')' 	{ $$ = current_action; }
| "consscroll"
   { 
     if (set_default_action (&default_consscroll, &current_action))
       YYABORT;
   }
   '(' consscrollparams ')' 	{ $$ = current_action; }
| "isolock"
  {
    if (set_default_action (&default_isolock, &current_action))
      YYABORT;
  }
  '(' isolockparams ')'	 	{ $$ = current_action; }
| "private" '(' privateparams ')'
  { $$ = calloc (1, sizeof (xkb_action_t)); $$->type = SA_NoAction; }
| "noaction" '(' ')'
  { $$ = calloc (1, sizeof (xkb_action_t)); $$->type = SA_NoAction; }
| error ')'	{ yyerror ("Invalid action\n"); }
;

/* Define values for default actions.  */
actiondef:
  "setmods"      '.' { current_action = &default_setmods;   } setmodsparam ';'
| "latchmods"    '.' { current_action = &default_latchmods; } setmodsparam ';'
| "lockmods"     '.' { current_action = &default_lockmods;  } setmodsparam ';'
| "setgroup"     '.' { current_action = &default_setgroup;  } setgroupparam ';'
| "latchgroup"   '.' { current_action = &default_latchgroup; } setgroupparam ';'
| "lockgroup"    '.' { current_action = &default_lockgroup; } setgroupparam ';'
| "moveptr"      '.' { current_action = &default_moveptr;   } moveptrparam ';'
| "ptrbtn"       '.' { current_action = &default_ptrbtn;    } ptrbtnparam ';'
| "lockptrbtn"   '.' { current_action = &default_lockptrbtn; } ptrbtnparam ';'
| "setptrdflt"   '.' { current_action = &default_ptrdflt;   } ptrdfltparam ';'
| "setcontrols"  '.' { current_action = &default_setcontrols; } ctrlparams ';'
| "lockcontrols" '.' { current_action = &default_lockcontrols; } ctrlparams ';'
| "isolock"      '.' { current_action = &default_isolock;   } isolockparam ';'
| "switchscreen" '.' { current_action = &default_switchscrn; } switchscrnparam ';'
;

/* The header of a symbols section.  */
symbols:
  flags "xkb_symbols" '{' symbolssect '}' ';'
| flags "xkb_symbols" STR '{' symbolssect '}' ';'
;

/* A group.  */
group:
  GROUPNUM 		{ $$ = $1 - 1; }
| NUM 			{ $$ = $1 - 1; }
| HEX 			{ $$ = $1 - 1; }
;

/* A list of keysyms and keycodes bound to a realmodifier.  */
key_list:
  key_list ',' KEYCODE		{ set_rmod_keycode ($3, current_rmod); }
| key_list ',' symbolname 	{ ksrm_add ($3, current_rmod);  }
| KEYCODE 			{ set_rmod_keycode ($1, current_rmod); }
| symbolname    		{ ksrm_add ($1, current_rmod);  }
;

/* Process the includes on the stack.  */
symbolinclude:
  '{' symbolssect '}'			 { close_include (); }
| symbolinclude '{' symbolssect '}'	 { close_include (); }
;

/* A XKB symbol section. It is used to bind keysymbols, actions and
   realmodifiers to keycodes.  */
symbolssect:
	/* Empty */
| symbolssect vmods_def
| symbolssect NAME '[' group ']' '=' STR ';'
| symbolssect "key" KEYCODE 
  { 
    key_new ($3);
    current_group = 0;
  } '{' keydescs '}' ';'
| symbolssect "replace" "key" KEYCODE 
  { 
    key_delete ($4);
    key_new ($4);
    current_group = 0;
  } '{' keydescs '}' ';'
| symbolssect "override" "key" KEYCODE 
  { 
    key_delete ($4);
    key_new ($4);
    current_group = 0;
  } '{' keydescs '}' ';'
| symbolssect "modifier_map" rmod { current_rmod = $3; } '{' key_list '}' ';'
| symbolssect include STR
   { include_sections ($3, XKBSYMBOLS, "symbols", $2); }
  symbolinclude
| symbolssect actiondef
| symbolssect "key" '.' {debug_printf ("working on default key.\n"); current_key = default_key; } keydesc ';'
| symbolssect error ';' { yyerror ("Error in symbol section\n"); }
;

/* Returns a keysymbols, the numberic representation.  */
symbolname:
  IDENTIFIER { $$ = XStringToKeysym ($1); }
| NUM
  {
    if (($1 >= 0) && ($1 < 10))
      $$ = $1 + '0';
    else
      $$ = $1;
  }
| HEX { $$ = $1; }
;

/* None or more keysyms, assigned to a single group of the current
   key.  */
groupsyms:
	/* empty */
| groupsyms ',' symbolname  
   { key_set_keysym (current_key, current_group, symbolcnt++, $3); }
| { symbolcnt = 0; } symbolname
   { 
     symbolcnt = 0;
     key_set_keysym (current_key, current_group, symbolcnt++, $2);
   }
;

/* A list of actions.  */
actions:
  actions ',' action
   { key_set_action (current_key, current_group, actioncnt++, $3); }
|  { actioncnt = 0; } action
   { key_set_action ( current_key, current_group, actioncnt++, $2); }
;

keydescs:
  keydesc
| keydescs ',' keydesc
;

/* A single key and everything assigned to it.  */
keydesc:
  "type" '[' group ']' '=' STR 
   {
     current_key->groups[$3].keytype = keytype_find ($6);
   }
| "type" '[' error ']' 	{ yyerror ("Invalid group.\n"); }
| "type" '=' STR
   { current_key->groups[current_group].keytype = keytype_find ($3); }
| { symbolcnt = 0; } "symbols" '[' group ']' { current_group = $4; } '=' '[' groupsyms ']'
   {
     current_key->numgroups = ($4 + 1) > current_key->numgroups ?
       ($4 + 1) : current_key->numgroups;
   }
| {actioncnt = 0; } "actions" '[' group ']' { current_group = $4; } '=' '[' actions ']'
   {
     current_key->numgroups = ($4 + 1) > current_key->numgroups ?
       ($4 + 1) : current_key->numgroups;   
   }
| "virtualmods" '=' mods
   { current_key->mods.vmods = $3.vmods; }
| '[' groupsyms ']'
  {
     current_group++;
     current_key->numgroups = current_group > current_key->numgroups ? 
       current_group : current_key->numgroups;   
  }
| '[' actions ']' 
   {
     current_group++;
     current_key->numgroups = current_group > current_key->numgroups ?
       current_group : current_key->numgroups;   
   }
| locking {}/* This is not implemented - YET.  */
/* XXX: There 3 features are described in Ivan Pascals docs about XKB,
   but aren't used in the standard keymaps and cannot be used because it
   cannot be stored in the XKM dataformat.  */
| groupswrap {}
| groupsclamp {}
| "groupsredirect" '=' NUM
| "overlay" '=' KEYCODE  /* If you _REALLY_ need overlays, mail me!!!!  */
| repeat  
  {
    current_key->flags &= ~(KEYREPEAT | KEYNOREPEAT);
    current_key->flags |= $1;
  }
;

/* The geometry map is ignored.  */

/* The header of a geometry section.  */
geometry:
  flags "xkb_geometry" '{' { skipsection (); } '}' ';'
| flags "xkb_geometry" STR '{' { skipsection (); } '}' ';'
;

%%
/* Skip all tokens until a section of the type SECTIONSYMBOL with the
   name SECTIONNAME is found.  */
static int
skip_to_sectionname (char *sectionname, int sectionsymbol)
{
  int symbol;

  do 
    {
      do 
	{
	  symbol = yylex ();
	} while ((symbol != YY_NULL) && (symbol != sectionsymbol));

      if (symbol != YY_NULL)
        symbol = yylex ();

      if (symbol == YY_NULL) {
        return 1;
      } else if (symbol != STR)
	continue;

    } while (strcmp (yylval.str, sectionname));
    return 0;
}

/* Skip all tokens until the first section of the type SECTIONSYMBOL
   is found.  */
static int
skip_to_firstsection (int sectionsymbol)
{
  int symbol;

  do 
    {
      do 
        {
          symbol = yylex ();
        } while ((symbol != YY_NULL) && (symbol != sectionsymbol));

      if (symbol != YY_NULL)
        symbol = yylex ();

      if (symbol == YY_NULL)
        return 1;
    } while (symbol != STR);
  return 0;
}

/* Skip all tokens until the default section is found.  */
static int
skip_to_defaultsection (void)
{
  int symbol;

  /* Search the default section.  */
  do
    {
      if ((symbol = yylex ()) == YY_NULL)
          return 1;
    } while (symbol != DEFAULT);

  do
    {
      if ((symbol = yylex ()) == YY_NULL)
          return 1;
    } while (symbol != '{');
  scanner_unput ('{');
  return 0;
}

/* Include a single file. INCL is the filename. SECTIONSYMBOL is the
   token that marks the beginning of the section. DIRNAME is the name
   of the directory from where the includefiles must be loaded. NEW_MM
   is the mergemode that should be used.  */
static error_t
include_section (char *incl, int sectionsymbol, char *dirname,
		 mergemode new_mm)
{
  void include_file (FILE *, mergemode, char *);
  int scanner_get_current_location ();
  const char* scanner_get_current_file ();

  char *filename;
  char *sectionname = NULL;
  FILE *includefile;

  int current_location = scanner_get_current_location ();
  char* current_file = strdup (scanner_get_current_file ());
  
  sectionname = strchr (incl, '(');
  if (sectionname)
    {
      int snlen;

      snlen = strlen (sectionname);
      if (sectionname[snlen-1] != ')')
        {
          free(current_file);
          return 0;
        }
      sectionname[snlen-1] = '\0';
      sectionname[0] = '\0';
      sectionname++;

      if (asprintf (&filename, "%s/%s", dirname, incl) < 0)
        {
          free (current_file);
          return ENOMEM;
        }
    }
  else
    {
      if (asprintf (&filename, "%s/%s", dirname, incl) < 0)
        {
          free (current_file);
          return ENOMEM;
        }
    }

  includefile = fopen (filename, "r");
  
  if (includefile == NULL)
    {
      fprintf (stderr, "Couldn't open include file \"%s\"\n", filename);
      free (current_file);
      free (filename);
      exit (EXIT_FAILURE);
    }
  
  include_file (includefile, new_mm, strdup (filename));
  debug_printf ("skipping to section %s\n", (sectionname ? sectionname : "default"));
  /* If there is a sectionname not the entire file should be included,
     the scanner should be positioned at the required section.  */
  int err;
  if (sectionname)
      err = skip_to_sectionname (sectionname, sectionsymbol);
  else
      if ((err = skip_to_defaultsection ()) != 0)
        {
          /* XXX: after skip_to_defaultsection failed the whole file was
             consumed and it is required to include it here, too. */
          include_file (includefile, new_mm, strdup (filename));
          err =  skip_to_firstsection (sectionsymbol);
        }
  if (err != 0) {
     char* tmpbuf = malloc (sizeof(char)*1024);
     if (tmpbuf) {
         snprintf (tmpbuf, 1023, "cannot find section %s in file %s included from %s:%d.\n"
             , (sectionname ? sectionname : "DEFAULT")
             , filename, current_file, current_location);
	 yyerror (tmpbuf);
	 free (tmpbuf);
     }
     free (current_file);
     free (filename);
     exit (err);
  }
  free (current_file);
  free (filename);
  return 0;
}

/* Include multiple file sections, separated by '+'. INCL is the
   include string. SECTIONSYMBOL is the token that marks the beginning
   of the section. DIRNAME is the name of the directory from where the
   includefiles must be loaded. NEW_MM is the mergemode that should be
   used.  */
static error_t
include_sections (char *incl, int sectionsymbol, char *dirname,
		  mergemode new_mm)
{
  char *curstr;
  char *s;

  if (new_mm == defaultmm)
    new_mm = merge_mode;

/*   printf ("dir: %s - include: %s: %d\n", dirname, incl, new_mm); */
  /* Cut of all includes, starting with the first.  The includes are
     pushed on the stack in reversed order.  */
  do {
    curstr = strrchr (incl, '+');
    if (curstr)
      {
	curstr[0] = '\0';
	curstr++;

	s = strdup (curstr);
	if (s == NULL)
	  return ENOMEM;
	
	include_section (s, sectionsymbol, dirname, new_mm);
	free (s);
      }
  } while (curstr);
  
  s = strdup (incl);
  if (s == NULL)
      return ENOMEM;
  
  include_section (s, sectionsymbol, dirname, new_mm);
  free (s);

  return 0;
}

/* Skip all tokens until the end of the section is reached.  */
static void
skipsection (void)
{
  /* Pathesensis counter.  */
  int p = 0;
  while (p >= 0)
    {
      int symbol = yylex ();
      if (symbol == '{')
	p++;
      if (symbol == '}')
	p--;
    }
  scanner_unput ('}');
}

/* Initialize the default action with the default DEF.  */
static error_t
set_default_action (struct xkb_action *def, 
		    struct xkb_action **newact)
{
  struct xkb_action *newaction;
  newaction = malloc (sizeof (struct xkb_action));
  if (newaction == NULL)
    return ENOMEM;
  memcpy (newaction, def, sizeof (struct xkb_action));  
  
  *newact = newaction;
  
  return 0;
}

/* Remove all keysyms bound to the group GROUP or the key KEY.  */
static void
remove_symbols (struct key *key, group_t group)
{
  //  printf ("rem: group: %d\n", group);
  if (key->groups[group].symbols)
    {
      free (key->groups[group].symbols);
      key->groups[group].symbols = NULL;
      key->groups[group].width = 0;
    }
}

/* Set the keysym KS for key KEY on group GROUP and level LEVEL.  */
static void
key_set_keysym (struct key *key, group_t group, int level, symbol ks)
{
  symbol *keysyms = key->groups[group].symbols;

  if ((level + 1) > key->groups[group].width)
    {
      keysyms = realloc (keysyms, (level + 1)*sizeof(symbol));

      if (!keysyms)
	{
	  fprintf (stderr, "No mem\n");
	  exit (EXIT_FAILURE);
	}
	 
      key->groups[group].symbols = keysyms;
      key->groups[group].width++;
    }
  else
    /* For NoSymbol leave the old symbol intact.  */
    if (!ks) {
      debug_printf ("symbol %d was not added to key.\n", ks);
      return;
    }

  debug_printf ("symbol '%c'(%d) added to key for group %d and level %d.\n", ks, ks, group, level);
  keysyms[level++] = ks;
}

/* Set the action ACTION for key KEY on group GROUP and level LEVEL.  */
void
key_set_action (struct key *key, group_t group, int level, xkb_action_t *action)
{
  xkb_action_t **actions = key->groups[group].actions;
  size_t width = key->groups[group].actionwidth;

  if ((size_t) (level + 1) > width)
    {
      actions = realloc (actions, (level + 1)*sizeof(xkb_action_t *));
      /* Levels between 'width' and 'level' have no actions defined. */
      memset (&actions[width], 0, (level - width)*sizeof(xkb_action_t *));

      if (!actions)
	{
	  fprintf (stderr, "No mem\n");
	  exit (EXIT_FAILURE);
	}
	 
      key->groups[group].actions = actions;
      key->groups[group].actionwidth += level - width + 1;
    }

  actions[level++] = action;
}

/* Delete keycode to keysym mapping.  */
void
key_delete (char *keyname)
{
  group_t group;
  keycode_t kc = keyname_find (keyname);
  
  current_key = &keys[kc];
  for (group = 0; group < current_key->numgroups; group++)
    remove_symbols (current_key, group);
  memset (current_key, 0, sizeof (struct key));

}

/* Create a new keycode to keysym mapping, check if the old one should
   be removed or preserved.  */
static void
key_new (char *keyname)
{
  group_t group;

  int isempty (char *mem, int size)
    {
      int i;
      for (i = 0; i < size; i++)
	if (mem[i])
	  return 0;
      return 1;
    }

  keycode_t kc = keyname_find (keyname);

  if (merge_mode == augment)
    {
      if (!isempty ((char *) &keys[kc], sizeof (struct key)))
	{
	  current_key = &dummy_key;
          debug_printf ("working on dummy key due to merge mode.\n");
	  return;
	}
      else
	current_key = &keys[kc];
    }
    
  if (merge_mode == override)
      current_key = &keys[kc];

  if (merge_mode == replace)
    {
      key_delete (keyname);
      current_key = &keys[kc];
    }

  debug_printf ("working on key %s(%d)", keyname, kc);

  if (current_key->numgroups == 0 || merge_mode == replace)
    {
      debug_printf (" cloned default key");
      /* Clone the default key.  */
      memcpy (current_key, default_key, sizeof (struct key));
      for (group = 0; group < 3; group++)
	{
	  current_key->groups[group].symbols = NULL;
	  current_key->groups[group].actions = NULL;
	  current_key->groups[group].actionwidth = 0;
	  current_key->groups[group].width = 0;
	}
    }
  debug_printf ("\n");
}

/* Load the XKB configuration from the section XKBKEYMAP in the
   keymapfile XKBKEYMAPFILE. Use XKBDIR as root directory for relative
   pathnames.  */
error_t
parse_xkbconfig (char *xkbdir, char *xkbkeymapfile, char *xkbkeymap)
{
  error_t err;
  char *cwd = getcwd (NULL, 0);
  extern FILE *yyin;
  extern char *filename;

  debug_printf ("Dir: %s, file: %s sect: %s\n", xkbdir, xkbkeymapfile, xkbkeymap);

  if (xkbkeymapfile)
    {
      filename = xkbkeymapfile;

      if (chdir (xkbdir) == -1)
	{
	  fprintf (stderr, "Could not set \"%s\" as the active directory\n", 
		   xkbdir);
          free (cwd);
	  return errno;
	}

      yyin = fopen (xkbkeymapfile, "r");
      if (yyin == NULL)
	{
	  fprintf (stderr, "Couldn't open keymap file\n");
          free (cwd);
	  return errno;
	}

      if (xkbkeymap)
	skip_to_sectionname (xkbkeymap, XKBKEYMAP);
      else
        skip_to_defaultsection();
    } 
  else
    {
      free (cwd);
      return EINVAL;
    }
  err = yyparse ();
  fclose (yyin);

  if (err || yynerrs > 0)
    {
      free (cwd);
      return EINVAL;
    }

  if (xkbkeymapfile)
    {
      if (chdir (cwd) == -1)
	{
	  fprintf (stderr, 
		   "Could not set \"%s\" as the active directory\n", cwd);
          free (cwd);
	  return errno;
	}
    }

  /* Apply keysym to realmodifier mappings.  */
  ksrm_apply ();

  free (cwd);
  return 0;
}
