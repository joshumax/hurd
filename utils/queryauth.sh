#!/bin/bash
# Query whether to grant authorization when a process accesses a file guarded by the checkperms translator.
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

USAGE="Usage: $0 [OPTION...] GROUP"
DOC="Query whether to grant authorization when a process accesses a file guarded by the checkperms translator for the GROUP."

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
      echo "STANDARD_HURD_VERSION_queryauth_"; exit 0;;
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

if [ $# -eq 0 ]; then
  echo missing GROUP
  echo $USAGE
  exit 1
fi

USER=$(whoami)
GROUP=$1

# create the controlling FIFOs, if needed
if [ ! -e /run/$USER/request-permission/$GROUP ]; then
    mkdir -p /run/$USER/request-permission 2>/dev/null
    mkfifo /run/$USER/request-permission/$GROUP
fi
if [ ! -e /run/$USER/grant-permission/$GROUP ]; then
    mkdir -p /run/$USER/grant-permission 2>/dev/null
    mkfifo /run/$USER/grant-permission/$GROUP
fi    

while true; do
    PID="$(cat /run/$USER/request-permission/$GROUP)"
    echo Process "'"$PID"'" tries to access file guarded by the checkperms translator, but is not in the required group "'"$GROUP"'".
    ps-hurd -p $PID -aeux
    if [[ "$(read -e -p 'Grant permission and add group "'$GROUP'" for 5 minutes? [y/N]> '; echo $REPLY)" == [Yy]* ]]; then
        addauth -p $PID -g $GROUP
        echo 0 > /run/$USER/grant-permission/$GROUP
        (sleep 300 && rmauth -p $PID -g $GROUP 2>/dev/null) &
    else
        echo 1 > /run/$USER/grant-permission/$GROUP
    fi
done
