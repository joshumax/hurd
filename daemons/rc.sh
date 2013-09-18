#!/bin/bash

PATH=/bin:/sbin

# Set up swap space.  This will complain if no default pager is functioning.
swapon -a

# Check filesystems.
if [ -r /fastboot ]
then
	# ... or don't.
	rm -f /fastboot
	echo Fast boot ... skipping disk checks
elif [ $1x = autobootx ]
then
	echo Automatic boot in progress...
	date

	/sbin/fsck --preen --writable

	case $? in
	# Successful completion
	0)
		;;
	# Filesystem modified (but ok now)
	1)
		;;
	# Filesystem modified, filesystem should be restarted
	# Ideally we would only restart the filesystem
	2 | 3)
		/sbin/reboot
		;;
	# Fsck couldn't fix it.
	4 | 5 | 8 | 9)
		echo "Automatic boot failed... help!"
		exit 1
		;;
	# Signal that really interrupted something
	20 | 130 | 131)
		echo "Boot interrupted"
		exit 1
		;;
	# Special `let fsck finish' interruption (SIGQUIT)
	12)
		echo "Boot interrupted (filesystem checks complete)"
		exit 1
		;;
	# Oh dear.
	*)
		echo "Unknown error during fsck (exit status $?)"
		exit 1
		;;
	esac
fi

echo -n cleaning up left over files...
rm -f /etc/nologin
rm -f /var/lock/LCK.*
if test -d /tmp; then

  # Forcibly remove all translators in the directory.
  # It is then safe to attempt to remove files and descend directories.
  # All parameters must begin with "./".
  function remove_translators() {
    local f
    for f; do
      settrans -pagfS "$f"
      if [ -L "$f" ] || [ ! -d "$f" ]; then
	rm "$f"
      else
	remove_translators "$f"/* "$f"/.[!.] "$f"/.??*
	rmdir "$f"
      fi
    done
  }

  (cd /tmp
   shopt -s nullglob
   for f in * .[!.] .??*; do
     case "$f" in
     'lost+found'|'quotas') ;;
     *) remove_translators "./$f"
     esac
   done)

  unset -f remove_translators  # because it relies on nullglob

fi
if test -d /var/run; then
  (cd /var/run && { rm -rf -- *; cp /dev/null utmp; chmod 644 utmp; })
fi
echo done

# This file must exist for e2fsck to work. XXX
touch /var/run/mtab

#echo -n restoring pty permissions...
#chmod 666 /dev/tty[pqrs]*
#echo done

#echo -n updating /etc/motd...
#echo GNU\'s Not Unix Version `uname --release` > /tmp/newmotd
#egrep -v 'GNU|Version' /etc/motd >> /tmp/newmotd
#mv /tmp/newmotd /etc/motd
#echo done

chmod 664 /etc/motd

echo -n starting daemons:

/sbin/syslogd	&& echo -n ' syslogd'
/sbin/inetd	&& echo -n ' inetd'

if test -x /sbin/sendmail -a -r /etc/sendmail.cf; then
  /sbin/sendmail -bd -q30m	&& echo -n ' sendmail'
fi

echo .

date
