#
# This awk script is used by the Makefile rules for generating
# Xioctl-proto.defs, see Makefile for details.
#

$1 == "SUBSYSTEM" { subsystem = $2 + 0; next }
$1 == "GROUP" { groupchar = $2; next }

$2 == "CMD"	{ cmd[tolower($1)] = $3;
                  c = $3 + 0;
                  if (c > highcmd) highcmd = c;
		  icmd[c] = tolower($1);
		  next }
$2 == "SUBID"	{ subid[tolower($1)] = $3;
                  c = $3 + 0;
                  if (c > highid) highid = c;
		  id2cmdname[c] = tolower($1);
		  next }
$2 == "TYPE"	{ type[tolower($1)] = $3; next }
$2 == "INOUT"	{ inout[tolower($1)] = $3; next }
$2 == "TYPE0"	{ type0[tolower($1)] = $3; next }
$2 == "TYPE1"	{ type1[tolower($1)] = $3; next }
$2 == "TYPE2"	{ type2[tolower($1)] = $3; next }
$2 == "COUNT0"	{ count0[tolower($1)] = $3; next }
$2 == "COUNT1"	{ count1[tolower($1)] = $3; next }
$2 == "COUNT2"	{ count2[tolower($1)] = $3; next }

END {
  group = sprintf("%cioctl", groupchar);

  printf "subsystem %s %d; /* see ioctls.defs for calculation */\n\n", \
    group, subsystem;

  typemap[0] = "char";
  typemap[1] = "char";
  typemap[2] = "short";
  typemap[3] = "int";
  typemap[4] = "???64 bits???";
  inoutmap[1] = "out";
  inoutmap[2] = "in";
  inoutmap[3] = "inout";

  print "";
  for (cmdname in type0) {
    if (count0[cmdname] > 1) {
      typecount = type0[cmdname] "," count0[cmdname];
      if (!tc[typecount]) {
	tc[typecount] = typemap[type0[cmdname]] "array_" count0[cmdname] "_t";
	print "type", tc[typecount], "=", ("array[" count0[cmdname] "]"), \
	  "of", (typemap[type0[cmdname]] ";"), "/* XXX rename this! */";
      }
      argtype["0," cmdname] = tc[typecount];
    }
    else if (count0[cmdname] == 1) {
      argtype["0," cmdname] = typemap[type0[cmdname]]
    }
  }

  for (cmdname in type1) {
    if (count1[cmdname] > 1) {
      typecount = type1[cmdname] "," count1[cmdname];
      if (!tc[typecount]) {
	tc[typecount] = typemap[type1[cmdname]] "array_" count1[cmdname] "_t";
	print "type", tc[typecount], "=", ("array[" count1[cmdname] "]"), \
	  "of", (typemap[type1[cmdname]] ";"), "/* XXX rename this! */";
      }
      argtype["1," cmdname] = tc[typecount];
    }
    else if (count1[cmdname] == 1) {
      argtype["1," cmdname] = typemap[type1[cmdname]]
    }
  }

  for (cmdname in type2) {
    if (count2[cmdname] > 1) {
      typecount = type2[cmdname] "," count2[cmdname];
      if (!tc[typecount]) {
	tc[typecount] = typemap[type2[cmdname]] "array_" count2[cmdname] "_t";
	print "type", tc[typecount], "=", ("array[" count2[cmdname] "]"), \
	  "of", (typemap[type2[cmdname]] ";"), "/* XXX rename this! */";
      }
      argtype["2," cmdname] = tc[typecount];
    }
    else if (count2[cmdname] == 1) {
      argtype["2," cmdname] = typemap[type2[cmdname]]
    }
  }
  print "";

  lastid = -1;
  for (i = 0; i <= highid; ++i)
    if (id2cmdname[i]) {
      cmdname = id2cmdname[i];

      if (lastid < 100 && i > 100) {
	if (lastid == 98)
	  print "\nskip; /* 99 unused */"
	else
	  printf "\nskip %d; /* %d-99 unused */\n", 100 - lastid, lastid + 1;
	print "\n\
/* Because MiG defines reply ports as 100 more than request ports, we\n\
   have to leave one hundred empty RPC's here. */\n\
skip 100;";
	lastid = 199;
      }

      if (i == lastid + 2)
	print "\nskip; /*", lastid + 1, "unused */";
      else if (i != lastid + 1)
	printf "\nskip %d; /* %d-%d unused */\n", \
	  i - lastid - 1, lastid + 1, i - 1;
      lastid = i;
      print "\n/*", i, toupper(cmdname), "*/";
      printf "routine %s_%s (\n\treqport: io_t", group, cmdname;
      if (inout[cmdname]) {
	io = inoutmap[inout[cmdname]];
	for (argidx = 0; argidx <= 2; ++argidx)
	  if (argtype[argidx "," cmdname])
	    printf ";\n\t%s\targ%d: %s", \
	      io, argidx, argtype[argidx "," cmdname];
      }
      else {
	printf ";\n\tin\trequest: int";
      }
      print ");"
    }
}
