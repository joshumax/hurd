#!/bin/sh
# Show system uptime, number of users, and load
#
# Copyright (C) 1996 Free Software Foundation, Inc.
#
# Written by Miles Bader <miles@gnu.ai.mit.edu>
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

USAGE="Usage: $0 [OPTION...]"
DOC="Show system uptime, number of users, and load"

W=${W-/bin/w}

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
      echo "Usage: $0 [-V?] [--help] [--usage] [--version]"
      exit 0;;
    --version|-V)
      echo "STANDARD_HURD_VERSION_uptime_"; exit 0;;
    -*)
      echo 1>&2 "$0: unrecognized option \`$1'"
      echo 1>&2 "Try \`$0 --help' or \`$0 --usage' for more information";
      exit 1;;
    *)
      break;;
  esac
done

case "$#" in
  0) :;;
  *) 
      echo 1>&2 "$0: too many arguments"
      echo 1>&2 "Try \`$0 --help' or \`$0 --usage' for more information";
      exit 1;;
esac

$W -u
