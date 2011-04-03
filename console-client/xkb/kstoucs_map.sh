#!/bin/sh

SED=${SED:-sed}
AWK=${AWK:-awk}

$SED -n -e 's/#define XK_[a-zA-Z0-9_]\+\s\+\(0x[0-9a-fA-F]\+\)\s*\/\*\s*U+\([0-9a-fA-F]\+\).*\*\//\1\t0x\2/p' \
  | $AWK '{ print strtonum($1) "\t" $2; }' \
  | sort -n -k1 \
  | $AWK '
      BEGIN { print "struct ksmap kstoucs_map[] = {" }
      {
        if (NR == 1)
          separator ="    ";
        else
          separator ="  , ";

        if (strtonum($1) < 0x1000000)
          print separator "{ " $1 ", " $2 " }"
      }
      END { print "};" }'
