#!/bin/sh
# A unix-like su (one which invokes a sub-shell).
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

ARGS_DOC="[USER|- [COMMAND [ARG...]]]"
USAGE="Usage: $0 $ARGS_DOC"
DOC="Start a new shell, or COMMAND, as USER"

LOGIN=${LOGIN-/bin/login}
FMT=${FMT-/bin/fmt}

needs_arg=""
for I; do
  case $needs_arg in
    ?*) needs_arg="";;
    "")
      case "$I" in
	-e|-E|-g|-G|-u|-U|--envar|--envva|--env|--en|--e|--envvar-default|--envvar-defaul|--envvar-defau|--envvar-defa|--envvar-def|--envvar-def|--envvar-de|--envvar-d|--envvar-|--group|--grou|--gro|--gr|--g|--avail-group|--avail-grou|--avail-gro|--avail-gr|--avail-g|--user|--use|--us|--u|--avail-user|--avail-use|--avail-us|--avail-u)
	  needs_arg="$I";;
	-e*|-E*|-g*|-G*|-u*|-U*|--envar=*|--envva=*|--env=*|--en=*|--e=*|--envvar-default=*|--envvar-defaul=*|--envvar-defau=*|--envvar-defa=*|--envvar-def=*|--envvar-def=*|--envvar-de=*|--envvar-d=*|--envvar-=*|--group=*|--grou=*|--gro=*|--gr=*|--g=*|--avail-group=*|--avail-grou=*|--avail-gro=*|--avail-gr=*|--avail-g=*|--user=*|--use=*|--us=*|--u=*|--avail-user=*|--avail-use=*|--avail-us=*|--avail-u=*)
	  :;;
	--avail-|--avail|--avai|--ava|--av|--a|--avail-=*|--avail=*|--avai=*|--ava=*|--av=*|--a=*)
	  echo 1>&2 "$0: option \`$1' is ambiguous"
	  echo 1>&2 "Try \`$0 --help' or \`$0 --usage' for more information";
	  exit 1;;
	--help|"-?" |--hel|--he|--h)
	  echo "$USAGE"
	  echo "$DOC"
	  echo ""
	  echo "  -?, --help                 Give this help list"
	  echo "  -e ENTRY, --envvar=ENTRY   Add ENTRY to the environment"
	  echo "  -E ENTRY, --envvar-default=ENTRY"
	  echo "                             Use ENTRY as a default environment variable"
	  echo "  -g GROUP, --group=GROUP    Add GROUP to the effective groups"
	  echo "  -G GROUP, --avail-group=GROUP   Add GROUP to the available groups"
	  echo "  -u USER, --user=USER       Add USER to the effective uids"
	  echo "  -U USER, --avail-user=USER Add USER to the available uids"
	  echo "      --usage                Give a short usage message"
	  echo "  -V, --version              Print program version"
	  echo ""
	  echo "Unlike the traditional unix \`su' command, if USER is not specified,"
	  echo "then the result is *no* user-ids, not uid 0."
	  exit 0;;
	--usage |--usag|--usa|--us|--u)
	  (echo "Usage: $0 [-V?]"
	   echo "            [-e ENTRY] [-E ENTRY] [-g GROUP] [-G GROUP] [-u USER] [-U USER]  [--envvar=ENTRY] [--envvar-default=ENTRY] [--group=GROUP] [--avail-group=GROUP][--group=GROUP] [--avail-group=GROUP] [--user=USER] [--avail-user=USER] [--help] [--usage] [--version] $ARGS_DOC") |$FMT -t
	  exit 0;;
	--version|-V |--versio|--versi|--vers|--ver|--ve|--v)
	  echo "STANDARD_HURD_VERSION_sush_"; exit 0;;
	-)
	  : ;;
	--)
	  break;;
	-*)
	  echo 1>&2 "$0: unrecognized option \`$1'"
	  echo 1>&2 "Try \`$0 --help' or \`$0 --usage' for more information";
	  exit 1;;
	*)
	  break;;
      esac;;
  esac
done

case "$needs_arg" in ?*)
  echo 1>&2 "$0: option \`$1' requires an argument"
  echo 1>&2 "Try \`$0 --help' or \`$0 --usage' for more information";
  exit 1;;
esac

exec $LOGIN --program-name="$0" -pxSLf -aHOME -aMOTD -aUMASK -aBACKUP_SHELLS "$@"
