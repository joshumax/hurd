#
# Copyright (c) 1994 The University of Utah and
# the Computer Systems Laboratory (CSL).  All rights reserved.
#
# Permission to use, copy, modify and distribute this software and its
# documentation is hereby granted, provided that both the copyright
# notice and this permission notice appear in all copies of the
# software, derivative works or modified versions, and any portions
# thereof, and that both notices appear in supporting documentation.
#
# THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
# IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
# ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
#
# CSL requests users of this software to return to csl-dist@cs.utah.edu any
# improvements that they make and grant CSL redistribution rights.
#
#      Author: Bryan Ford, University of Utah CSL
#

BEGIN {
	bogus_printed = "no"
}

# Start the bogus function just before the first sym directive,
# so that any #includes higher in the file don't get stuffed inside it.
/^[a-z]/ {
	if (bogus_printed == "no")
	{
		print "void bogus() {";
		bogus_printed = "yes";
	}
}

# Take an arbitrarily complex C symbol or expression and constantize it.
/^expr/ {
	printf "__asm (\"";
	if ($3 == "")
		printf "* %s mAgIc%%0\" : : \"i\" (%s));\n", $2, $2;
	else
		printf "* %s mAgIc%%0\" : : \"i\" (%s));\n", $3, $2;
}

# Output a symbol defining the size of a C structure.
/^size/ {
	printf "__asm (\"";
	if ($4 == "")
		printf "* %s_SIZE mAgIc%%0\" : : \"i\" (sizeof(struct %s)));\n",
			toupper($3), $2;
	else
		printf "* %s mAgIc%%0\" : : \"i\" (sizeof(struct %s)));\n",
			$4, $2;
}

# Output a symbol defining the byte offset of an element of a C structure.
/^offset/ {
	printf "__asm (\"";
	if ($5 == "")
	{
		printf "* %s_%s mAgIc%%0\" : : \"i\" (&((struct %s*)0)->%s));\n",
			toupper($3), toupper($4), $2, $4;
	}
	else
	{
		printf "* %s mAgIc%%0\" : : \"i\" (&((struct %s*)0)->%s));\n",
			toupper($5), $2, $4;
	}
}

# Copy through all preprocessor directives.
/^#/ {
	print
}

END {
	print "}"
}
