#!/bin/sh
#
# Make standard devices
#

PATH=/bin:/usr/bin

ECHO=:		# Change to "echo" to echo commands
EXEC=""		# Change to ":" to suppress command execution
export ECHO EXEC

while :; do
  case "$1" in
    --help|"-?")
      echo "\
Usage: $0 [OPTION...] DEVNAME...
Make filesystem nodes for accessing standard system devices

  -D, --devdir=DIR           Use DIR when a device node name must be
                             embedded in a translator; default is the cwd
  -n, --dry-run              Don't actually execute any commands
  -v, --verbose              Show what commands are executed to make the devices
  -?, --help                 Give this help list
      --usage                Give a short usage message
  -V, --version              Print program version"
      exit 0;;
    --devdir)   DEVDIR="$2"; shift 2;;
    --devdir=*) DEVDIR="`echo "$1" | sed 's/^--devdir=//'`"; shift 1;;
    -D)         DEVDIR="$2"; shift 2;;
    -D*)        DEVDIR="`echo "$1" | sed 's/^-D//'`"; shift 1;;
    --verbose|-v) ECHO=echo; shift;;
    --dry-run|-n) EXEC=:; shift;;
    -nv|-vn)      ECHO=echo; EXEC=:; shift;;
    --usage)
      echo "Usage: $0 [-V?] [-D DIR] [--help] [--usage] [--version] [--devdir=DIR] DEVNAME..."
      exit 0;;
    --version|-V)
      echo "STANDARD_HURD_VERSION_MAKEDEV_"; exit 0;;
    -*)
      echo 1>&2 "$0: unrecognized option \`$1'"
      echo 1>&2 "Try \`$0 --help' or \`$0 --usage' for more information";
      exit 1;;
    *)
      break;;
  esac
done

case  "$#" in 0)
  echo 1>&2 "Usage: $0 [OPTION...] DEVNAME..."
  echo 1>&2 "Try \`$0 --help' or \`$0 --usage' for more information"
  exit 1;;
esac

function cmd {
  eval $ECHO "$@"
  eval $EXEC "$@"
}

function st {
  local NODE="$1"
  local OWNER="$2"
  local PERM="$3"
  shift 3
  if cmd settrans -cg "$NODE"; then
    cmd chown "$OWNER" "$NODE"
    cmd chmod "$PERM" "$NODE"
    cmd settrans "$NODE" "$@"
  fi
}

case ${DEVDIR+set} in
  set) export DEVDIR;;
  *)   _CWD="`pwd`";;
esac

function mkdev {
  local I
  for I; do
    local B="${I##*/}"
    case "$B" in
      std)
        local dir="`dirname $I`"
	mkdev $dir/console $dir/tty $dir/null $dir/zero $dir/fd $dir/time
	;;
      console|tty[0-9][0-9a-f]|tty[0-9a-f]|com[0-9])
	local dn	# runtime device name
	case "${DEVDIR+set}" in
	  set) dn="$DEVDIR/$B";;
	  "")  case "$I" in
		 /*)  dn="$I";;
		 *)   dn="$_CWD/$I";;
	       esac;;
        esac
	st $I root 600 /hurd/term $dn device $B;;
      null)
	st $I root 666 /hurd/null;;
      zero)
	st $I root 666 /hurd/storeio -Tzero;;
      tty)
	st $I root 666 /hurd/magic tty;;
      fd)
        local dir="`dirname $I`"
	st $I root 666 /hurd/magic fd
	cmd ln -f -s fd/0 $dir/stdin
	cmd ln -f -s fd/1 $dir/stdout
	cmd ln -f -s fd/2 $dir/stderr
	;;
      'time')
	st $I root 644 /hurd/storeio time ;;

      # ptys
      [pt]ty[pqrstuvwxyzPQRST]?)
	# Make one pty, both the master and slave halves
	local id="${B:3}"
        local dir="`dirname $I`"
	local dd
	case "${DEVDIR+set}" in
	  set) dd="$DEVDIR";;
	  "")  case "$I" in
		 /*)  dd="$dir";;
		 */*) dd="$_CWD/$dir";;
		 *)   dd="$_CWD";;
	       esac;;
        esac
	st $dir/pty$id root 640 /hurd/term $dd/pty$id pty-master $dd/tty$id
	st $dir/tty$id root 640 /hurd/term $dd/tty$id pty-slave $dd/pty$id
	;;
      [pt]ty[pqrstuvwxyzPQRST])
	# Make a bunch of ptys
	mkdev ${I}0 ${I}1 ${I}2 ${I}3 ${I}4 ${I}5 ${I}6 ${I}7
	mkdev ${I}8 ${I}9 ${I}a ${I}b ${I}c ${I}d ${I}e ${I}f
	;;

      fd*|mt*)
	st $I root 640 /hurd/storeio $B
	;;

      [hrsc]d*)
	case "$B" in
	[a-z][a-z][0-9][a-z] | [a-z][a-z][0-9]s[1-9] | [a-z][a-z][0-9]s[1-9][a-z] | [a-z][a-z][0-9])
	  st $I root 640 /hurd/storeio $B
	  ;;
	*)
	  echo 1>&2 $0: $B: Invalid device name: must supply a device number
	  exit 1
	  ;;
	esac
	;;

      *)
	echo >&2 $0: $B: Unknown device name
	exit 1
	;;
    esac
  done
}

mkdev "$@"
