#!/bin/sh
# Set/get the `creator_os' field of an ext2fs partition
#
# Copyright (C) 1996, 1997 Free Software Foundation, Inc.
#
# Written by Miles Bader <miles@gnu.ai.mit.edu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

USAGE="Usage: $0 DEVICE [OS]"

DD=${DD-/bin/dd}
OD=${OD-/bin/od}
SED=${SED-/bin/sed}
AWK=${AWK-/bin/gawk}

# Hack to allow this script to work well under linux too.
test ! -x "$OD" -a -x /usr/bin/od && OD=/usr/bin/od
test ! -x "$AWK" -a -x /usr/bin/gawk && AWK=/usr/bin/gawk

while :; do
  case "$1" in
    --help|"-?")
      echo "$USAGE"
      echo "Get or set the creator_os parameter of an ext2fs partition."
      echo ""
      echo "  -?, --help                 Give this help list"
      echo "      --usage                Give a short usage message"
      echo "  -V, --version              Print program version"
      exit 0;;
    --usage)
      echo "Usage: $0 [-V?] [--help] [--usage] [--version] DEVICE [OS]"
      exit 0;;
    --version|-V)
      echo "STANDARD_HURD_VERSION_e2os_"; exit 0;;
    -*)
      echo 1>&2 "$0: unrecognized option \`$1'"
      echo 1>&2 "Try \`$0 --help' for more information";
      exit 1;;
    *)
      break;;
  esac
done

case  "$#" in 1 | 2) ;; # ok
  *) echo 1>&2 "$USAGE"
     echo 1>&2 "Try \`--help' for more information";
     exit 1;;
esac

DEVICE="$1"; shift
OS="$1"

# Superblock fields (format is "BYTE_OFFS SIZE")
SB_MAGIC="56 2"
SB_OS="72 4"
# Ext2fs magic numbers
MAGIC_EXT2=ef53
MAGIC_EXT2_OLD=ef53
# Symbolic names for os types we know about
OS_LINUX=0
OS_HURD=1
OS_MASIX=2
OS_FREEBSD=3
OS_LITES=4

# Superblock
SB=/tmp/,e2os-sb.$$

# We have to store error output in a file so that we can filter it (for all
# unix's stressing of pipelines, /bin/sh sure works hard to prevent you using
# them).
ERRS=/tmp/,e2os-errs.$$

trap "/bin/rm -f $SB $ERRS" 0

# Read the superblock
$DD 2>"$ERRS" if="$DEVICE" of="$SB" bs=1k skip=1 count=1 \
|| { $SED 1>&2 "s;^$DD:;$0:;" "$ERRS"; exit 2; }

# Extract a word of SZ bytes from byte offset POS in the superblock
# Optional arg FMT is what format to use (x = hex, d = decimal)
sbget ()
{
  local pos="$1" sz="$2" fmt="${3-d}"
  pos=$(($pos / $sz))
  $DD 2>/dev/null if="$SB" bs="$sz" skip="$pos" count=1 \
  | $OD -An -t"$fmt$sz" \
  | $SED 's;^[ 0]*\(.\);\1;'
}

# Set a word of SZ bytes at byte offset POS in the superblock to VAL
sbset ()
{
  local pos="$1" sz="$2" val="$3"
  pos=$(($pos / $sz))
  echo "$val" \
  | $AWK '{ n=$1+0; printf ("%c%c%c%c", n, n/256, n/(2^16), n/(2^24)); }' \
  | $DD 2>/dev/null of="$SB" bs="$sz" seek="$pos" count=1 conv=notrunc
}

# Check the magic number
magic="`sbget $SB_MAGIC x`"
case "$magic" in
  $MAGIC_EXT2)     ;; # ok
  $MAGIC_EXT2_OLD) echo "$0: $DEVICE: Old-format ext2 filesystem"; exit 3;;
  *) echo "$0: $DEVICE: Not an ext2 filesystem (magic = 0x$magic)"; exit 4;;
esac

if test "$OS"; then
  # Set the os field
  case "$OS" in
    linux) OS=$OS_LINUX;;
    hurd)  OS=$OS_HURD;;
    masix) OS=$OS_MASIX;;
    freebsd) OS=$OS_FREEBSD;;
    lites) OS=$OS_LITES;;
    "*[!0-9]*") 
      echo 1>&2 "$0: $OS: Unknown ext2 creator_os value"; exit 5;;
  esac

  # Frob the superlock
  sbset $SB_OS "$OS"

  # Write the superblock
  $DD 2>"$ERRS" if="$SB" of="$DEVICE" bs=1k seek=1 count=1 conv=notrunc \
  || { $SED 1>&2 "s;^$DD:;$0:;" "$ERRS"; exit 6; }
else
  # Print the os field.
  OS="`sbget $SB_OS`"
  case "$OS" in
    "") exit 2;;
    $OS_LINUX) OS=linux;;
    $OS_HURD)  OS=hurd;;
    $OS_MASIX) OS=masix;;
    $OS_FREEBSD) OS=freebsd;;
    $OS_LITES) OS=lites;;
  esac
  echo "$OS"
fi
