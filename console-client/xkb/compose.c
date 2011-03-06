/*  compose.c -- Keysym composing

    Copyright (C) 2003  Marco Gerards
   
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

#include <stdio.h>
#include <stdlib.h>
#include "keysymdef.h"
#include "xkb.h"
#include <ctype.h>
#include <string.h>

#define	NoSymbol	0

/* Tokens that can be recognised by the scanner.  */
enum tokentype
  {
    UNKNOWN,
    EOL,
    REQKS,
    SEMICOL,
    STR,
    PRODKS,
    END
  };

/* The current token.  */
struct token
{
  enum tokentype toktype;
  char value[50];
} tok;

/* The linenumber of the line currently parsed, used for returning
   errors and warnings.  */
static int linenum;

/* Read a token from the file CF.  */
static void
read_token (FILE *cf)
{
  int c = fgetc (cf);
  int pos = 0;

  /* Remove whitespaces.  */
  while (c == ' ' || c == '\t')
    c = fgetc (cf);

  /* Comment, remove until end of line and return a EOL token.  */
  if (c == '#')
    {
      while (c != '\n')
	c = fgetc (cf);
      tok.toktype = EOL;
      linenum++;
      return;
    }

  /* End of file.  */
  if (c == EOF)
    {
      tok.toktype = END;
      return;
    }

  /* Semicolon.  */
  if (c == ':')
    {
      tok.toktype = SEMICOL;
      return;
    }

  /* End of line.  */
  if (c == '\n')
    {
      linenum++;
      tok.toktype = EOL;
      return;
    }
  

  /* Required keysym.  */
  if (c == '<')
    {
      while ((c = fgetc (cf)) != '>')
	tok.value[pos++] = c;
      tok.value[pos] = '\0';
      tok.toktype = REQKS;
      return;
    }

  /* Character string.  */
  if (c == '"')
    {
      while ((c = fgetc (cf)) != '"')
	{
 	  if (c == '\\')
	    c = fgetc (cf);

	  tok.value[pos++] = c;
	}
      tok.value[pos] = '\0';
      tok.toktype = STR;
      return;
    }

  /* Produced keysym.  */
  if (isalpha (c))
    {
      tok.value[pos++] = c;
      while (isgraph (c = fgetc (cf)))
	tok.value[pos++] = c;
      tok.value[pos] = '\0';
      tok.toktype = PRODKS;
      ungetc (c, cf);
      return;
    }

  /* Unknown token.  */
  tok.toktype = UNKNOWN;
  return;
}

/* Compose sequence.  */
struct compose
{
  struct compose *left;
  struct compose *right;
  symbol *expected;
  symbol produced;
} *compose_tree;


/* Compare symbol sequence s1 to symbol sequence s1. This function
   works just like the strcmp function.  */
static int
symbolscmp (symbol *s1, symbol *s2)
{
  while (*s1 && *s2 && (*s1 == *s2))
    {
      s1++;s2++;
    }
  if (*s1 < *s2)
    return -1;
  if (*s1 > *s2)
    return 1;
  return 0;  
}

/* Compare symbol sequence s1 to symbol sequence s1, compare a maximum
   of N symbols. This function works just like the strcmp function.
   */
static int
symbolsncmp (symbol *s1, symbol *s2, int n)
{
  int cnt = 0;
  while (*s1 && *s2 && (*s1 == *s2))
    {
      if (++cnt == n)
	break;
      s1++;s2++;
    }

  if (*s1 < *s2)
    return -1;
  if (*s1 > *s2)
    return 1;
  return 0;  
}


/* Add the compose sequence EXP to the binary tree, store RESULT as
   the keysym produced by EXP.  */
static struct compose *
composetree_add (struct compose *tree, symbol *exp, symbol result)
{
  int cmp;

  if (tree == NULL)
    {
      tree = malloc (sizeof (struct compose));
      tree->expected = exp;
      tree->produced = result;
      tree->left = tree->right = NULL;
      
      return tree;
    }
  
  cmp = symbolscmp (exp, tree->expected);
  if (cmp == 0)
    {
      printf ("Warning: line %d: Double sequence.\n", linenum);
      free (exp);
    }
  else if (cmp < 0)
    tree->left = composetree_add (tree->left, exp, result);
  else
    tree->right = composetree_add (tree->right, exp, result);
  return tree;
}

/* Parse the composefile CF and put all sequences in the binary tree
   COMPOSE_TREE. This function may never fail because of syntactical or
   lexalical errors, generate a warning instead.  */
static error_t
parse_composefile (FILE *cf)
{
  void skip_line (void)
    {
      while (tok.toktype != EOL && tok.toktype != END)
	read_token (cf);
    }

  for (;;)
    {  
      /* Expected keysyms.  */
      symbol exp[50];
      symbol *exps;
      size_t expcnt = 0;
      symbol sym;

      read_token (cf);
      /* Blank line.  */
      if (tok.toktype == EOL)
	continue;

      /* End of file, done parsing.  */
      if (tok.toktype == END)
	return 0;

      if (tok.toktype != REQKS)
	{
	  printf ("Warning: line %d: Keysym expected on beginning of line.\n",
		  linenum);
	  skip_line ();
	  continue;
	}

      /* Keysym was recognised, add it.  */
      sym = XStringToKeysym (tok.value);
      if (!sym)
	{
	  printf ("Warning: line %d: Unknown keysym \"%s\".\n", linenum, 
		  tok.value);
	  skip_line ();
	  continue;
	}
      exp[expcnt++] = sym;

      do
	{
	  read_token (cf);
	  /* If another required keysym is recognised, add it.  */
	  if (tok.toktype == REQKS)
	    {
	      sym = XStringToKeysym (tok.value);
	      if (!sym)
		{
		  printf ("Warning: line %d: Unknown keysym \"%s\".\n", 
			  linenum, tok.value);
		  skip_line ();
		  continue;
		}
	      exp[expcnt++] = sym;
	    }
	} while (tok.toktype == REQKS);

      if (tok.toktype != SEMICOL)
	{
	  printf ("Warning: line %d: Semicolon expected.\n", linenum);
	  skip_line ();
	  continue;
	}

      read_token (cf);
      /* Force token and ignore it.  */
      if (tok.toktype != STR)
	{
	  printf ("Warning: line %d: string expected.\n", linenum);
	  skip_line ();
	  continue;
	}

      read_token (cf);
      if (tok.toktype != PRODKS)
	{
	  printf ("Warning: line %d: keysym expected.\n", linenum);
	  skip_line ();
	  continue;
	}
      sym = XStringToKeysym (tok.value);
      if (!sym)
	{
	  printf ("Warning: line %d: Unknown keysym \"%s\".\n", linenum,
		  tok.value);
	  skip_line ();
	  continue;
	}

      read_token (cf);
      if (tok.toktype != EOL && tok.toktype != END)
	{
	  printf ("Warning: line %d: end of line or end of file expected.\n",
		  linenum);
	  skip_line ();
	  continue;
	}

      /* Add the production rule.  */
      exp[expcnt++] = 0;
      exps = malloc (sizeof (symbol) * expcnt);
      memcpy (exps, exp, sizeof (symbol) * expcnt);
      compose_tree = composetree_add (compose_tree, exps, sym);
    }
  return 0;
}

/* Read keysyms passed to this function by S until a keysym can be
   composed. If the first keysym cannot start a compose sequence return
   the keysym.  */
symbol
compose_symbols (symbol s)
{
  /* Current position in the compose tree.  */
  static struct compose *treepos = NULL;
  /* Current compose sequence.  */
  static symbol syms[100];
  /* Current position in the compose sequence.  */
  static int pos = 0;
  int cmp;

  if (!treepos)
    treepos = compose_tree;

  /* Maximum sequence length reached. Some idiot typed this many
     symbols and now we throw it all away, wheee!!!  */
  if (pos == 99)
    {
      treepos = compose_tree;
      pos = 0;
    }

  /* Put the keysym in the compose sequence array.  */
  syms[pos++] = s;
  syms[pos] = 0;

  /* Search the tree for a keysym sequence that can match the current one.  */
  while (treepos)
    {
      cmp = symbolsncmp (syms, treepos->expected, pos);
      if (cmp == 0)
	{
	  /* The keysym sequence was partially recognised, check if it
	     can completely match.  */
	  if (!symbolscmp (syms, treepos->expected))
	    {
	      symbol ret = treepos->produced;
	      treepos = compose_tree;
	      pos = 0;
	      return ret;
	    }

	  /* The sequence was partially recognised.  */
	  return -1;
	}

      if (cmp < 0)
	treepos = treepos->left;
      else
	treepos = treepos->right;
    }  

  /* Nothing can be found.  */
  treepos = compose_tree;

  /* This ks should've started a sequence but couldn't be found,
     just return it.  */
  if (pos == 1)
    {
      pos = 0;   
      return s;
    }

  debug_printf ("Invalid\n");
  /* Invalid keysym sequence.  */
  pos = 0;   
  return -1;
}

/* Read a Compose file.  */
error_t
read_composefile (char *composefn)
{
  FILE *cf;

  error_t err;
  cf = fopen (composefn, "r");
  if (cf == NULL)
    return errno;
  
  err = parse_composefile (cf);
  if (err)
    fclose (cf);
  
  return err;
}
