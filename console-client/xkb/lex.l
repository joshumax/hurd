/*  XKB scanner.

    Copyright (C) 2002, 03  Marco Gerards
   
    Written by Marco Gerards <marco@student.han.nl>
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.  */


	#include "xkb.h"
	#include "parser.tab.h"
	#include <string.h>
	#include <unistd.h>
	#include <stdlib.h>
	#include <stdio.h>
	#include <error.h>

	int close_include (void);
	int lineno = 1;
	char *filename = "foo";

%option nodebug

%option UNPUT
KEYCODE		"<"[A-Z][-+A-Z0-9]*">"
DIGIT		[0-9]
NUM		{DIGIT}{DIGIT}*
FLOAT		{DIGIT}{DIGIT}*\.{DIGIT}{DIGIT}*
HEX		0x[A-Za-z0-9]*
IDENTIFIER	[a-zA-Z][a-zA-Z0-9_]*
CPPCOMMENT	"//".*"\n"
%%
			/* When the end of a file is reached, close the
			   current one and pop the next file to process
		           of the stack.  */

			/* Filter out comment.  */
{CPPCOMMENT}		{ lineno++;   }
     "/*"    {
             int c;
     
             while((c = input()) != 0)
                 {
                 if(c == '\n')
                     lineno++;
     
                 else if(c == '*')
                     {
                     if((c = input()) == '/')
                         break;
                     else
                         unput(c);
                     }
                 }
             }

\n			{ lineno++;		}

			/* Beginning of sections.  */
xkb_keymap		{ return XKBKEYMAP;	}
xkb_symbols		{ return XKBSYMBOLS;	}
xkb_keycodes		{ return XKBKEYCODES; }
xkb_types	  	{ return XKBTYPES; 	}
xkb_compatibility	{ return XKBCOMPAT; 	}
xkb_geometry		{ return XKBGEOMETRY; }


			/* Real modifiers.  */
shift			{ return SHIFT;   }
lock			{ return LOCK;    }
control			{ return CONTROL; }
ctrl			{ return CONTROL; }
mod1			{ return MOD1;    }
mod2			{ return MOD2;    }
mod3			{ return MOD3;    }
mod4			{ return MOD4;    }
mod5			{ return MOD5;    }

			/* Levels.  */
anylevel		{ yylval.val = 0; return LEVEL; }
levelone		{ yylval.val = 1; return LEVEL; }
level1			{ yylval.val = 1; return LEVEL; }
level2			{ yylval.val = 2; return LEVEL; }
level3			{ yylval.val = 3; return LEVEL; }
level4			{ yylval.val = 4; return LEVEL; }
level[1-9][0-9]*	{ yylval.val = atoi(yytext + 5); return LEVEL; }

			/* Groups.  */
group1			{ yylval.val = 1; return GROUPNUM; }
group2			{ yylval.val = 2; return GROUPNUM; }
group3			{ yylval.val = 3; return GROUPNUM; }
group4			{ yylval.val = 4; return GROUPNUM; }

			/* Booleans */
true			{ yylval.val = 1; return BOOLEAN; }
false			{ yylval.val = 0; return BOOLEAN; }

			/* Interpretation */
interpret		{ return INTERPRET; }
usemodmap	  	{ return USEMODMAP; }
usemodmapmods		{ return USEMODMAP; }
modmapmods		{ return USEMODMAP; }
repeat			{ return REPEAT;    }
locking			{ return LOCKING;   }
locks			{ return LOCKING; }
virtualmodifier		{ return VIRTUALMODIFIER; }
virtualmod		{ return VIRTUALMODIFIER; }

			/* Interpretation match.  */
noneof			{ yylval.val = 0; return INTERPMATCH ;}
anyofornone		{ yylval.val = 1; return INTERPMATCH ;}
anyof			{ yylval.val = 2; return INTERPMATCH ;}
allof			{ yylval.val = 3; return INTERPMATCH ;}
exactly			{ yylval.val = 4; return INTERPMATCH ;}

			/* SetMods action. */
clearlocks		{ return CLEARLOCKS; }
mods			{ return MODS;	     }

unlock			{ return UNLOCK; }
both			{ return BOTH; }
neither			{ return NEITHER; }

			/* Actions.  */
setmods			{ return SETMODS;      }
latchmods		{ return LATCHMODS;    }
lockmods		{ return LOCKMODS;     }
setgroup		{ return SETGROUP;     }
latchgroup		{ return LATCHGROUP;   }
lockgroup		{ return LOCKGROUP;    }
setptrdflt		{ return PTRDFLT;      }
setpointerdefault	{ return PTRDFLT;      }
lockptrbtn		{ return LOCKPTRBTN;   }
lockpointerbutton	{ return LOCKPTRBTN;   }
lockptrbutton		{ return LOCKPTRBTN;   }
lockpointerbtn		{ return LOCKPTRBTN;   }
setcontrols		{ return SETCONTROLS;  }
lockcontrols		{ return LOCKCONTROLS; }
terminateserver		{ return TERMINATE;    }
terminate		{ return TERMINATE;    }
switchscreen		{ return SWITCHSCREEN; }
consscroll		{ return CONSSCROLL;   }
consolescroll		{ return CONSSCROLL;   }
moveptr			{ return MOVEPTR;      }
private			{ return PRIVATE;      }
			/* Action parameters.  */
latchtolock		{ return LATCHTOLOCK;  }
group			{ return GROUP;	       }
groups			{ return GROUPS;       }
accel			{ return ACCEL;	       }
accelerate		{ return ACCEL;	       }
default			{ return DEFAULT;      }
count			{ return COUNT;        }
controls		{ return CONTROLS;     }
same			{ return SAMESERVER;   }
sameserver		{ return SAMESERVER;   }
screen			{ return SCREEN;       }
line			{ return LINE;         }
percentage		{ return PERCENT;      }
ptrbtn			{ return PTRBTN ;      }
pointerbutton		{ return PTRBTN ;      }

action			{ return ACTION;       }
actions			{ return ACTIONS;      }

whichmodstate		{ return WHICHMODSTATE; }
whichmodifierstate	{ return WHICHMODSTATE; }
whichgroupstate		{ return WHICHGROUPSTATE; }

			/* Match state for indicator.  */
base			{ yylval.val = 0; return WHICHSTATE; }
latched			{ yylval.val = 1; return WHICHSTATE; }
locked			{ yylval.val = 4; return WHICHSTATE; }
effective		{ yylval.val = 8; return WHICHSTATE; }

			/* Bits for binary controls.  */
repeatkeys		{ yylval.val = 0; return CONTROLFLAG; }
autorepeat		{ yylval.val = 0; return CONTROLFLAG; }
accessxkeys		{ yylval.val = 0; return CONTROLFLAG; }
slowkeys		{ yylval.val = 0; return CONTROLFLAG; }
bouncekeys		{ yylval.val = 0; return CONTROLFLAG; }
stickykeys		{ yylval.val = 0; return CONTROLFLAG; }
accessxtimeout		{ yylval.val = 0; return CONTROLFLAG; }
accessxfeedback		{ yylval.val = 0; return CONTROLFLAG; }
mousekeys		{ yylval.val = 0; return CONTROLFLAG; }
mousekeysaccel		{ yylval.val = 0; return CONTROLFLAG; }
audiblebell		{ yylval.val = 0; return CONTROLFLAG; }
ignoregrouplock		{ yylval.val = 0; return CONTROLFLAG; }

index			{ return INDEX;		  }
name			{ return NAME;		  }
symbols			{ return SYMBOLS;	  }
key			{ return KEY;		  }

			/* Mouse buttons.  */
button1			{ yylval.val = 1; return BUTTONNUM; }
button2			{ yylval.val = 2; return BUTTONNUM; }
button3			{ yylval.val = 3; return BUTTONNUM; }
button4			{ yylval.val = 4; return BUTTONNUM; }
button5			{ yylval.val = 5; return BUTTONNUM; }
button			{ return BUTTON;		    }

			/* Fuzzyness.  */
any			{ return ANY;        }
all			{ return ALL;        }
none			{ return NONE;       }

defaultbutton		{ return DEFAULTBTN; }
affect			{ return AFFECT;     }

allowexplicit		{ return ALLOWEXPLICIT; }

			/* Feedback to the keyboard.  */
driveskeyboard		{ return DRIVESKBD;	}
driveskbd		{ return DRIVESKBD;	}
ledsdrivekbd		{ return DRIVESKBD;	}
ledsdrivekeyboard	{ return DRIVESKBD;	}
indicatordriveskbd	{ return DRIVESKBD;	}
indicatordriveskeyboard	{ return DRIVESKBD;	}


modifier_map		{ return MODMAP;	}
minimum			{ return MINIMUM; 	}
maximum			{ return MAXIMUM; 	}
virtual			{ return VIRTUAL; 	}
alias			{ return ALIAS;	  	}
indicator		{ return INDICATOR; 	}
virtual_modifiers	{ return VMODS; 	}
virtualmods		{ return VMODS; 	}
vmods			{ return VMODS; 	}
type			{ return TYPE;    	}
data                    { return DATA;          }
modifiers		{ return MODS; 		}
map			{ return MAP;	  	}
level_name		{ return LEVEL_NAME; 	}
preserve		{ return PRESERVE;	}

			/* Section flags.  */
partial			{ yylval.val = 1; return FLAGS; }
complete		{ yylval.val = 2; return FLAGS; }
fc			{ yylval.val = 4; return FLAGS; }
fd		    	{ yylval.val = 8; return FLAGS; }
cp			{ return XKBCOMPAT;		}

			/* Section merge modes.  */
include			{ yylval.mergemode = defaultmm; return INCLUDE;  }
augment			{ yylval.mergemode = augment; return AUGMENT;	 }
replace			{ yylval.mergemode = replace; return REPLACE;	 }
override		{ yylval.mergemode = override; return OVERRIDE;	 }

isolock			{ return ISOLOCK; }
pointers		{ return POINTERS;}
ptr			{ return POINTERS;}
noaction		{ return NOACTION;}
groupswrap		{ return GROUPSWRAP; }
wrapgroups		{ return GROUPSWRAP; }
clampgroups		{ return GROUPSCLAMP; }
groupsredirect		{ return GROUPSREDIRECT; }
overlay1		{ yylval.val = 1; return OVERLAY; }
overlay2		{ yylval.val = 2; return OVERLAY; }

			/* String.  */
\"([^"]|\\\")*\"	{ 
			  yytext[strlen (yytext) - 1] = '\0';
			  yylval.str = strdup (yytext + 1);
			  return STR;
			}
			/* Ignore whitespace.  */
[ \t]*
			/* A keycode.  */
{KEYCODE}		{ yylval.str = strdup (yytext) + 1; yylval.str[strlen (yylval.str) - 1] = 0; return KEYCODE; }
			/* A float vlaue.  */
{FLOAT}			{ yylval.dbl = atof (yytext); return FLOAT; }
			/* An integer.  */
{NUM}			{ yylval.val = atoi (yytext); return NUM; }
			/* A hexadecimal value.  */
{HEX}			{ sscanf (yytext, "0x%X", &yylval.val); return HEX; }
			/* An identifier.  */
{IDENTIFIER}		{ yylval.str = strdup (yytext); return IDENTIFIER; }
			/* All unrecognized characters.  */
.			{ return yytext[0]; }
%%
	/* Stupid hack, the current version of flex is fucked.  */
	#define yytext_ptr yytext

	void
        scanner_unput (int c)
	  {
	    unput (c);
	  }

	int
	yywrap (void)
	 {
           if (close_include () == 0)
             return 0;
           else
             return 1;
	 }

	#define	MAX_INCLUDE_DEPTH	50

	/* An include file on the stack.  */
	struct include_stack
	  {
	    YY_BUFFER_STATE buffer;
	    mergemode merge_mode;
	    int currline;
	    char *filename;
	  } include_stack[MAX_INCLUDE_DEPTH];
	int include_stack_ptr = 0;

	/* Add an file to the stack.  */
	void
	include_file (FILE *file, mergemode new_mm, char *fname)
	{
	  YY_BUFFER_STATE buffer;

          debug_printf ("including file %s\n.", fname);

	  if (include_stack_ptr >= MAX_INCLUDE_DEPTH)
	    {
  	      fprintf (stderr, "Includes nested too deeply\n");
	      exit (EXIT_FAILURE);
	    }
	
	  include_stack[include_stack_ptr].filename = filename;
	  include_stack[include_stack_ptr].currline = lineno;
	  include_stack[include_stack_ptr].merge_mode = merge_mode;
	  include_stack[include_stack_ptr++].buffer = YY_CURRENT_BUFFER;
	  filename = fname;
	  lineno = 1;
	  merge_mode = new_mm;

	  buffer = yy_create_buffer (file, YY_BUF_SIZE);
  	  yy_switch_to_buffer (buffer);
	}

	/* Close an includefile. returns 0 on success */
	int
	close_include (void)
	{
	  if ( --include_stack_ptr < 0 )
	    {
	      fprintf (stderr, "Unexpected end of file at %s:%d.\n", filename, lineno);
	      return (1);
	    }
	  else
	    {
	      fclose (yyin);
	      yy_delete_buffer (YY_CURRENT_BUFFER);
	      merge_mode = include_stack[include_stack_ptr].merge_mode;
	      lineno = include_stack[include_stack_ptr].currline;
	      filename = include_stack[include_stack_ptr].filename;
	      yy_switch_to_buffer (include_stack[include_stack_ptr].buffer);
              debug_printf ("closing file. going back to %s.\n", filename);
              return (0);
	    }
	}

	void
	yyerror (char *s)
	{
	   fprintf (stderr, "%s:%d: %s\n", filename, lineno, s);
 //	   error_at_line (0, 1, filename, lineno, "foo %d\n", 2);
	}

	int
	scanner_get_current_location()
	{
	  return lineno;
	}

	const char*
	scanner_get_current_file()
	{
	  return filename;
	}
