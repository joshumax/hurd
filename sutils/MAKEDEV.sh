#!/bin/sh
#
# Make standard devices
#

PATH=/bin

while :; do
  case "$1" in
    --help|"-?")
      echo "Usage: $0 [OPTION...] DEVNAME..."
      echo "Make filesystem nodes for accessing standard system devices"
      echo ""
      echo "  -?, --help                 Give this help list"
      echo "      --usage                Give a short usage message"
      echo "  -V, --version              Print program version"
      exit 0;;
    --usage)
      echo "Usage: $0 [-V?] [--help] [--usage] [--version] DEVNAME..."
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

function st {
  NODE="$1"
  OWNER="$2"
  PERM="$3"
  shift 3
  settrans -cg "$NODE"
  chown "$OWNER" "$NODE"
  chmod "$PERM" "$NODE"
  settrans "$NODE" "$@"
}

_CWD=${_CWD:-`pwd`}
export _CWD

function mkdev {
  for I; do
    case "$I" in
      std)
	mkdev console tty null zero fd time
	;;
      console|tty[0-9][0-9a-f]|tty[0-9a-f]|com[0-9])
	st $I root 600 /hurd/term $_CWD/$I device $I;;
      null)
	st $I root 666 /hurd/null;;
      zero)
	st $I root 666 /hurd/storeio -Tzero;;
      tty)
	st $I root 666 /hurd/magic tty;;
      fd)
	st $I root 666 /hurd/magic fd
	ln -f -s fd/0 stdin
	ln -f -s fd/1 stdout
	ln -f -s fd/2 stderr
	;;
      time)
	st $I root 666 /hurd/devport time ;;

      # ptys
      [pt]ty[pqPQ]?)
	# Make one pty, both the master and slave halves
	ID="`expr substr $I 4 99`"
	st pty$ID root 640 /hurd/term $_CWD/pty$ID pty-master $_CWD/tty$ID
	st tty$ID root 640 /hurd/term $_CWD/tty$ID pty-slave $_CWD/pty$ID
	;;
      [pt]ty[pqPQ])
	# Make a bunch of ptys
	mkdev ${I}0 ${I}1 ${I}2 ${I}3 ${I}4 ${I}5 ${I}6 ${I}7
	mkdev ${I}8 ${I}9 ${I}a ${I}b ${I}c ${I}d ${I}e ${I}f
	;;

      fd*|mt*)
	st $I root 640 /hurd/storeio $I
	;;

      [hrs]d*)
	case "$I" in
	[a-z][a-z][0-9][a-z] | [a-z][a-z][0-9]s[1-9] | [a-z][a-z][0-9]s[1-9][a-z] | [a-z][a-z][0-9])
	  st $I root 640 /hurd/storeio $I
	  ;;
	*)
	  echo 1>&2 $0: $I: Invalid device name: must supply a device number
	  exit 1
	  ;;
	esac
	;;

      *)
	echo >&2 $0: $I: Unknown device name
	exit 1
	;;
    esac
  done
}

mkdev "$@"
