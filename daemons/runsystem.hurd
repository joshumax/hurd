#!/bin/bash
#
# This program is run by /hurd/init at boot time after the essential
# servers are up, and is responsible for running the "userland" parts of a
# normal system.  This includes running the single-user shell as well as a
# multi-user system.  This program is expected never to exit.
#


###
### Where to find programs, etc.
###

PATH=/bin:/sbin
export PATH

umask 022

# If we lose badly, try to exec each of these in turn.
fallback_shells='/bin/sh /bin/bash /bin/csh /bin/ash /bin/shd'

# Shell used for normal single-user startup.
SHELL=/bin/sh

# Programs that do multi-user startup.
RUNCOM=/libexec/rc
RUNTTYS=/libexec/runttys
# Signals that we should pass down to runttys.
runttys_sigs='TERM INT HUP TSTP'

###


# If we get a SIGLOST, attempt to reopen the console in case
# our console ports were revoked.  This lets us print messages.
function reopen_console ()
{
  exec 1>/dev/console 2>&1 || exit 3
}
trap 'reopen_console' SIGLOST


# Call this when we are losing badly enough that we want to punt normal
# startup entirely.  We exec a single-user shell, so we will not come back
# here.  The only way to get to multi-user from that shell will be
# explicitly exec this script or something like that.
function singleuser ()
{
  test $# -eq 0 || echo "$0: $*"
  for try in ${fallback_shells}; do
    SHELL=${try}
    exec ${SHELL}
  done
  exit 127
}


# See whether pflocal is set up already, and do so if not (install case)
#
# Normally this should be the case, but we better make sure since
# without the pflocal server, pipe(2) does not work.
if ! test -e /servers/socket/1 ; then
  # The root filesystem should be read-only at this point.
  if fsysopts / --update --writable ; then
    settrans -c /servers/socket/1 /hurd/pflocal
  else
    singleuser "Failed to create /servers/socket/1."
  fi
fi

# We expect to be started by console-run, which gives us no arguments and
# puts FALLBACK_CONSOLE=file-name in the environment if our console is
# other than a normal /dev/console.

if [ "${FALLBACK_CONSOLE+set}" = set ]; then
  singleuser "Running on fallback console ${FALLBACK_CONSOLE}"
fi


###
### Normal startup procedures
###

# Parse the multiboot command line.  We only pay attention to -s and -f.
# The first argument is the kernel file name; skip that.
shift
flags=
while [ $# -gt 0 ]; do
  arg="$1"
  shift
  case "$arg" in
  --*) ;;
  *=*) ;;
  -*)
    flags="${flags}${arg#-}"
    ;;
  'single'|'emergency') # Linux compat
    flags="${flags}s"
    ;;
  'fastboot')
    flags="${flags}f"
    ;;
  esac
done

# Check boot flags.
case "$flags" in
*s*)
  rc=false			# force single-user
  ;;
*f*)
  rc="${RUNCOM}"		# fastboot
  ;;
*)
  rc="${RUNCOM} autoboot"	# multi-user default
  ;;
esac

# Large infinite loop.  If this script ever exits, init considers that
# a serious bogosity and punts to a fallback single-user shell.
# We handle here the normal transitions between single-user and multi-user.
while : ; do

  # Run the rc script.  As long as it exits nonzero, punt to single-user.
  # After the single-user shell exits, we will start over attempting to
  # run rc; but later invocations strip the `autoboot' argument.
  until $rc; do
    rc=${RUNCOM}

    # Run single-user shell and repeat as long as it dies with a signal.
    until ${SHELL} || test $? -lt 128; do
      :
    done
  done

  # Now we are officially ready for normal multi-user operation.

  # Trap certain signals and send them on to runttys.  For this to work, we
  # must run it asynchronously and wait for it with the `wait' built-in.
  runttys_pid=0
  for sig in $runttys_sigs; do
    trap "kill -$sig \${runttys_pid}" $sig
  done

  # This program reads /etc/ttys and starts the programs it says to.
  ${RUNTTYS} &
  runttys_pid=$!

  # Wait for runttys to die, meanwhile handling trapped signals.
  wait

  # Go back to the top of the infinite loop, as if booting single-user.
  rc=false

done
