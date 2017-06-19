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
#include <X11/keysymdef.h>
#include "xkb.h"
#include <ctype.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <locale.h>
#include <assert-backtrace.h>

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

struct map_entry
{
  const char *left;
  const char *right;
};

enum callback_result
  {
    NEXT,
    DONE
  };

typedef enum callback_result (*map_callback) (void *context, struct map_entry *entry);

static error_t
map_iterate(const char *map_path, map_callback action, void *context)
{
  FILE *map;
  char *buffer = NULL;
  size_t buffer_size = 0;
  size_t line_length = 0;

  assert_backtrace (map_path != NULL);
  assert_backtrace (action != NULL);

  map = fopen (map_path, "r");

  if (map == NULL)
    return errno;

  while ( (line_length = getline (&buffer, &buffer_size, map)) != -1)
    {
      /* skips empty lines and comments */
      if (line_length < 1 || buffer[0] == '#')
        continue;
      else
        {
          struct map_entry entry = {NULL, NULL};
          char *end = buffer + line_length;
          char *p = buffer;

          while (p != end && isspace(*p)) p++;

          if (p == end)
            continue;

          entry.left = p;

          while (p != end && !isspace(*p)) p++;

          if (p != end)
            {
              *(p++) = 0;
              while (p != end && isspace(*p)) p++;

              if (p != end)
                {
                  entry.right = p;
                  while (p != end && !isspace(*p)) p++;
                  if (p != end)
                    *p = 0;
                }
            }

          if (action (context, &entry) == DONE)
            break;
        }
    }
  free (buffer);
  fclose (map);
  return 0;
}

struct matcher_context
{
  char *value;
  char *result;
};

static enum callback_result
match_left_set_right (void *context, struct map_entry *entry)
{
  struct matcher_context *ctx = (struct matcher_context *) context;

  if (strcmp (ctx->value, entry->left) == 0)
    {
      ctx->result = strdup (entry->right);
      return DONE;
    }
  return NEXT;
}

static enum callback_result
match_right_set_left (void *context, struct map_entry *entry)
{
  struct matcher_context *ctx = (struct matcher_context *) context;

  if (strcmp (ctx->value, entry->right) == 0)
    {
      ctx->result = strdup (entry->left);
      return DONE;
    }
  return NEXT;
}

/* Search for a compose file.

   According to Compose(5) man page the compose file searched in the
   following locations:
     - XCOMPOSEFILE variable.
     - .XCompose at $HOME.
     - System wide compose file for the current locale. */
static char *
get_compose_file_for_locale()
{
  struct matcher_context context = { NULL };
  char *xcomposefile;
  char *to_be_freed;
  char *home;
  int err;

  xcomposefile = getenv ("XCOMPOSEFILE");
  if (xcomposefile != NULL)
    return strdup (xcomposefile);

  home = getenv ("HOME");
  if (home != NULL)
    {
      err = asprintf (&xcomposefile, "%s/.XCompose", home);
      if (err != -1)
        {
          if (faccessat(AT_FDCWD, xcomposefile, R_OK, AT_EACCESS) == 0)
            return xcomposefile;
          else
            {
              free (xcomposefile);
              /* TODO: check and report whether the compose file doesn't exist or
                 read permission was not granted to us. */
            }
        }
    }

  context.value = setlocale (LC_ALL, NULL);
  map_iterate (X11_PREFIX "/share/X11/locale/locale.alias", match_left_set_right, &context);
  to_be_freed = context.result;

  if (context.result != NULL)
    {
      /* Current locale is an alias. Use the real name to index the database. */
      context.value = context.result;
    }
  context.result = NULL;
  map_iterate (X11_PREFIX "/share/X11/locale/compose.dir", match_right_set_left, &context);
  free (to_be_freed);

  /* compose.dir contains relative paths to compose files. */
  to_be_freed = context.result;
  err = asprintf (&context.result, X11_PREFIX "/share/X11/locale/%s", context.result);
  if (err == -1)
    context.result = NULL;

  free (to_be_freed);
  return context.result;
}

/* Read a Compose file.  */
error_t
read_composefile (char *composefn)
{
  FILE *cf;

  error_t err;

  if (composefn == NULL)
    composefn = get_compose_file_for_locale ();

  cf = fopen (composefn, "r");
  if (cf == NULL)
    return errno;
  
  err = parse_composefile (cf);
  fclose (cf);
  
  return err;
}
