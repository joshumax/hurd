#!/bin/bash
# Setup checkperms translator and authorization query program.
#
# Copyright (C) 2002, 2013 Free Software Foundation, Inc.
#
# This file is part of the GNU Hurd.
#
# The GNU Hurd is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2, or (at
# your option) any later version.
#
# The GNU Hurd is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

USAGE="Usage: $0 [OPTION...] FILE GROUP [PROGRAM]"
DOC="Setup checkperms translator on FILE for the current user, guarded by GROUP, using the authorization query PROGRAM (queryauth if not given)"

while :; do
  case "$1" in
    --help|"-?")
      echo "$USAGE"
      echo "$DOC"
      echo ""
      echo "  -?, --help                 Give this help list"
      echo "      --usage                Give a short usage message"
      echo "  -V, --version              Print program version"
      exit 0;;
    --usage)
      echo "Usage: $0 [-V?] [--help] [--usage] [--version] FILE GROUP [PROGRAM]"
      exit 0;;
    --version|-V)
      echo "STANDARD_HURD_VERSION_queryauth-setup_"; exit 0;;
    --)
      shift
      break;;
    -*)
      echo 1>&2 "$0: unrecognized option \`$1'"
      echo 1>&2 "Try \`$0 --help' or \`$0 --usage' for more information";
      exit 1;;
    *)
      break;;
  esac
done

if [ $# -lt 1 ]; then
    echo missing FILE
    if [ $# -lt 2 ]; then
        echo missing GROUP
    fi
    echo $USAGE
    exit 1
fi

USER=$(whoami)
FILE=$1
GROUP=$2
PROGRAM=$3
if [ -z $PROGRAM ]; then
    PROGRAM=queryauth
fi

# do not replace an exitsing translator
if [ -e $FILE ] && showtrans $FILE | grep -q checkperms; then
    echo there is already a passive checkperms translator running on $FILE.
    echo Remove it with
    echo settrans -g $FILE
    echo if you want to replace it. Existing translator:
    echo -n "    "
    showtrans $FILE
    exit 1
fi

# create the controlling group if needed
groupadd $GROUP 2>/dev/null

# setup the translator
settrans -cg $FILE /hurd/checkperms --groupname=$GROUP

echo Setting up interactive authorization granting program via "'$PROGRAM $GROUP'"

$PROGRAM $GROUP
