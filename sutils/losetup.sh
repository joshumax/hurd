#!/bin/sh
#
# This script is roughly compatible with the Linux `losetup' utility.
# The Hurd's `storeio' translator provides the equivalent functionality
# (and a whole lot more), and of course works on any old file you want
# to translate, not just magical "/dev/loopN" device files.
#

PATH=/bin

usage() {
  echo >&2 ...
  exit 1
}

offset=0
while [ $# -gt 0 ]; do
  case "$arg" in
  -d)
    [ $# -eq 2 ] || usage
    exec settrans -g -- "$2" /hurd/null
    ;;
  -e)
    echo >&2 "$0: encryption not supported"
    exit 3
    ;;
  -o)
    [ $# -gt 1 ] || usage
    offset="$1"
    shift
    ;;
  --)
    shift
    break
    ;;
  -*)
    usage
    ;;
  *)
    break
    ;;
  esac
done

[ $# -eq 2 ] || usage
device="$1"
file="$2"

# If the device name is "/dev/loopN", then create it if necessary. (?)
create=
case "$device" in
'/dev/loop[0-9]*') ;; # smarty pants
/dev/loop[0-9]*) create=--create ;;
esac

type='-Tfile '
if [ "$offset" != 0 ]; then
  blksz=`storeinfo -B -- "$file"`
  if [ $[ $offset % $blksz ] -ne 0 ]; then
    echo >&2 "$0: offset $offset is not a multiple of device block size $blksz"
    exit 1
  fi
  type="-Tremap $[ $offset / $blksz ]+:file:"
fi

exec settrans $create -gap -- "${device}" /hurd/storeio ${type}"${file}"
