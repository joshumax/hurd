#!/bin/sh
#
# A simple version of mount for the hurd
#

usage="Usage: $0: [ -rnv | -o DEVOPTS | -t TYPE | -f FSTAB ] ( DEVICE NODE | DEVICE | NODE )"

PATH=/bin

default_type=ufs
type=""
fstab=/etc/fstab
exec=true
echo=false

while :; do
  case $1 in
    -v) echo=true; margs="$margs -v"; shift;;
    -n) exec=false; margs="$margs -n"; shift;;
    -r) targs="$targs -r"; shift;;
    -t) case "$type" in
	  ""|"$2") type="$2"; shift 2; margs="$margs -t $type";;
	  *)    echo 1>&2 $0: "$2": Filesystem type inconsistent with "$type"
	        exit 7;;
        esac;;
    -f) fstab=$2; shift 2;;
    -o) targs="$targs $2"; shift 2;;
    -*) echo 1>&2 $0: $1: unknown flag; echo 1>&2 "$usage"; exit 1;;
    *)  break;;
  esac
done

case "$targs" in ?*)
  # We embed quotes so that spaces are preserved in targs later on
  margs="$margs -o \"$targs\""
esac

case $# in
  1)
    # Lookup the given single arg in /etc/fstab for the rest of the args
    args=`awk -f - $fstab <<END
\\$1 == "$1" || \\$2 == "$1" {
  for (i = 4; i <= NF; i++)
    printf("%s ", \\$i);
  printf("-t %s %s %s", \\$3, \\$1, \\$2);
  exit(0);
}
END
`
    case "$args" in
      "") echo 1>&2 $0: $1: not found in $fstab; exit 3;;
      *)  eval $0 $margs $args;;
    esac
    ;;

  2)
    # Do the mount, by putting an active translator on the node

    case "$type" in "") type="$default_type";; esac

    if [ ! -x /hurd/$type ]; then
      echo 1>&2 $0: $type: unknown filesystem type
      exit 1
    fi

    $echo && echo settrans -a $2 /hurd/$type $targs $1
    $exec && settrans -a $2 /hurd/$type $targs $1
    ;;

  *)
    echo 1>&2 "$usage"; exit 1
    ;;
esac
