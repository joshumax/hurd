#!/bin/sh
#
# Make standard devices
#

PATH=/bin:/usr/bin

ECHO=:		# Change to "echo" to echo commands.
EXEC=""		# Change to ":" to suppress command execution.
DEVDIR=`pwd`	# Reset below by -D/--devdir command line option.
STFLAGS="-g"	# Set to -k if active translators are to be kept.
KEEP=		# Set to something if existing files are to be left alone.
USE_PARTSTORE=	# Whether to use the newer part: stores

while :; do
  case "$1" in
    --help|"-?")
      echo "\
Usage: $0 [OPTION...] DEVNAME...
Make filesystem nodes for accessing standard system devices

  -D, --devdir=DIR           Use DIR when a device node name must be
                             embedded in a translator; default is the cwd
  -k, --keep-active          Leave any existing active translator running
  -K, --keep-all             Don't overwrite existing files
  -p, --parted               Prefer user-space parted stores to kernel devices
                             for partition devices
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
    --keep-active|-k) STFLAGS="-k"; shift;;
    --keep-all|-K) KEEP=1; shift;;
    --parted|-p) USE_PARTSTORE=1; shift;;
    --verbose|-v) ECHO=echo; shift;;
    --dry-run|-n) EXEC=:; shift;;
    -nv|-vn)      ECHO=echo; EXEC=:; shift;;
    --usage)
      echo "Usage: $0 [-V?] [-D DIR] [--help] [--usage] [--version] [--parted]"
      echo "                [--devdir=DIR] [--keep-active] [--keep-all] DEVNAME..."
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

cmd() {
  eval $ECHO "$@"
  eval $EXEC "$@"
}

st() {
  local NODE="$1"
  local OWNER="$2"
  local PERM="$3"
  shift 3
  if [ "$KEEP" ] && showtrans "$NODE" > /dev/null 2>&1 ; then
    return;
  fi
  if cmd settrans $STFLAGS -c "$NODE"; then
    cmd chown "$OWNER" "$NODE"
    cmd chmod "$PERM" "$NODE"
    cmd settrans $STFLAGS "$NODE" "$@"
  fi
}

lose() {
  local line
  for line; do
    echo 1>&2 "$0: $line"
  done
  exit 1
}

mkdev() {
  local I
  for I; do
    case $I in
      /* | */*)
        lose "Device names cannot contain directories" \
	     "Change to target directory and run $0 from there."
	;;

      std)
	mkdev console tty random urandom null zero full fd time mem klog shm
	;;
      console|com[0-9])
	st $I root 600 /hurd/term ${DEVDIR}/$I device $I;;
      vcs)
        st $I root 600 /hurd/console;;
      tty[1-9][0-9]|tty[1-9])
        st $I root 600 /hurd/term ${DEVDIR}/$I hurdio \
	   ${DEVDIR}/vcs/`echo $I | sed -e s/tty//`/console;;
      lpr[0-9])
        st $I root 660 /hurd/streamio "$I";;
      random)
	st $I root 644 /hurd/random --seed-file /var/lib/random-seed;;
      urandom)
	# Our /dev/random is both secure and non-blocking.  Create a
	# link for compatibility with Linux.
	cmd ln -f -s random $I;;
      null)
	st $I root 666 /hurd/null;;
      full)
	st $I root 666 /hurd/null --full;;
      zero)
	st $I root 666 /bin/nullauth -- /hurd/storeio -Tzero;;
      tty)
	st $I root 666 /hurd/magic tty;;
      fd)
	st $I root 666 /hurd/magic --directory fd
	cmd ln -f -s fd/0 stdin
	cmd ln -f -s fd/1 stdout
	cmd ln -f -s fd/2 stderr
	;;
      'time')
	st $I root 644 /hurd/storeio --no-cache time ;;
      mem)
	st $I root 660 /hurd/storeio --no-cache mem ;;
      klog)
        st $I root 660 /hurd/streamio kmsg;;
      # ptys
      [pt]ty[pqrstuvwxyzPQRS]?)
	# Make one pty, both the master and slave halves.
	local id="${I#???}"
	st pty$id root 666 /hurd/term ${DEVDIR}/pty$id \
				      pty-master ${DEVDIR}/tty$id
	st tty$id root 666 /hurd/term ${DEVDIR}/tty$id \
				      pty-slave ${DEVDIR}/pty$id
	;;
      [pt]ty[pqrstuvwxyzPQRS])
	# Make a bunch of ptys.
	local n
        for n in 0 1 2 3 4 5 6 7 8 9 \
		 a b c d e f g h i j k l m n o p q r s t u v; do
	  mkdev ${I}${n}
	done
	;;

      fd*|mt*)
	st $I root 640 /hurd/storeio $I
	;;

      [hrsc]d*)
	local sliceno=
        local n="${I#?d}"
	local major="${n%%[!0-9]*}"
	if [ -z "$major" ]; then
	  lose "$I: Invalid device name: must supply a device number"
	fi
	local minor="${n##$major}"
	case "$minor" in
	'') ;;		# Whole disk
	[a-z]) ;;	# BSD partition syntax, no slice syntax
	s[1-9]*)	# Slice syntax.
	  local slicestuff="${minor#s}"
	  local slice="${slicestuff%%[!0-9]*}"
	  local rest="${slicestuff##$slice}"
	  case "$slice" in
	  [1-9] | [1-9][0-9]) ;;
	  *)
	    lose "$I: Invalid slice number \`$slice'"
	    ;;
	  esac
	  case "$rest" in
	  '')		# Whole slice, can use parted stores
	    sliceno=$slice
	    ;;
	  [a-z]) ;;	# BSD partition after slice
	  *)
	    lose "$I: Invalid partition \`$rest'"
	    ;;
	  esac
	  ;;
	*)
	  lose "$I: Invalid slice or partition syntax"
	  ;;
	esac

	# The device name passed all syntax checks, so finally use it!
	if [ "$USE_PARTSTORE" ] && [ -z "$rest" ] && [ "$sliceno" ]; then
	  local dev=${I%s[0-9]*}
	  st $I root 640 /hurd/storeio -T typed part:$sliceno:device:$dev
	else
	  st $I root 640 /hurd/storeio $I
	fi
	;;

      netdde)
	st $I root 660 /hurd/netdde;;
      eth*)
	st $I root 660 /hurd/devnode -M /dev/netdde $I;;

      # /dev/shm is used by the POSIX.1 shm_open call in libc.
      # We don't want the underlying node to be written by randoms,
      # but the filesystem presented should be writable by anyone
      # and have the sticky bit set so others' files can't be removed.
      # tmpfs requires an arbitrary size limitation here.  To be like
      # Linux, we tell tmpfs to set the size to half the physical RAM
      # in the machine.
      shm)
        st $I root 644 /hurd/tmpfs --mode=1777 50%
        ;;

      # Linux compatibility
      loop*)
        # In Linux an inactive "/dev/loopN" device acts like /dev/null.
	# The `losetup' script changes the translator to "activate" the device.
        st $I root 640 /hurd/null
	;;

      *)
	lose "$I: Unknown device name"
	;;
    esac
  done
}

mkdev "$@"
