#!/bin/sh
PATH=/bin:/sbin

if [ -r /fastboot ]
then
	rm -f /fastboot
	echo Fast boot ... skipping disk checks
elif [ $1x = autobootx ]
then
	echo Automatic boot in progress...

	# Find the filesystem by a kludge, and extract the root device name.
	fsargs=`ps -MaxHww --fmt=%command | grep exec-server-task | head -1`
	dev=`echo $fsargs | sed 's/^.* \([^ ]*\)$/\1/' `
	type=`echo $fsargs | sed 's/^\/hurd\/\(.*\)\.static.*$/\1/' `

	fsysopts / -r
	fsck.$type -p /dev/r$dev

	case $? in
	# Successful completion
	0)
		;;
	# Filesystem modified
	1 | 2)
		fsysopts / -uw
		;;
	# Fsck couldn't fix it. 
	4 | 8)
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
		echo "Unknown error during fsck"
		exit 1
		;;
	esac
fi

date

# Until new hostname functions are in place
test -r /etc/hostname && hostname `cat /etc/hostname`

echo -n cleaning up left over files...
rm -f /etc/nologin
rm -f /var/lock/LCK.*
(cd /tmp; find . ! -name . ! -name lost+found ! -name quotas -exec rm -r {} \; )
(cd /var/run && { rm -rf -- *; cp /dev/null utmp; chmod 644 utmp; })
echo done

echo -n restoring pty permissions...
chmod 666 /dev/tty[pqrs]*
echo done

#echo -n updating /etc/motd...
#echo GNU\'s Not Unix Version `uname --release` > /tmp/newmotd
#egrep -v 'GNU|Version' /etc/motd >> /tmp/newmotd
#mv /tmp/newmotd /etc/motd
#echo done

chmod 664 /etc/motd

echo -n starting daemons:
/sbin/syslogd;		echo -n ' syslogd'
/sbin/inetd;		echo -n ' inet'
echo .	


date
