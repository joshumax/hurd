#!/bin/sh
PATH=/bin:/sbin

if [ -r /fastboot ]
then
	rm -f /fastboot
	echo Fast boot ... skipping disk checks
elif [ $1x = autobootx ]
then
	echo Automatic reboot in progress...
	date

	# Find the filesystem by a kludge, and extract the root device name.
	fsargs=`ps -MaxHww --fmt=%command | grep exec-server-task | head -1`
	dev=`echo $fsargs | sed 's/^.* \([^ ]*\)$/\1/' `
	type=`echo $fsargs | sed 's/^\/hurd\/\(.*\)\.static.*$/\1/' `

	fsysopts / -r
	fsck.$type -p /dev/r$dev

	case $? in
	
# Successful completion
	0)
		date
		;;

# Filesystem modified
	1 | 2)
		fsysopts / -uw
		date
		;;

# Fsck couldn't fix it. 
	4 | 8)
		echo "Automatic reboot failed... help!"
		exit 1
		;;

# SIGINT
	130)  
		echo "Reboot interrupted"
		exit 1
		;;

# Oh dear.
	*)	
		echo "Unknown error in reboot"
		exit 1
		;;
	esac
else
	date
fi

# Until new hostname functions are in place
test -r /etc/hostname && hostname `cat /etc/hostname`

echo -n cleaning lock files...
rm -f /etc/nologin
rm -f /var/lock/LCK.*
echo done

echo -n clearing /tmp...
(cd /tmp; find . ! -name . ! -name lost+found ! -name quotas -exec rm -r {} \; )
echo done

echo -n restoring pty permissions...
chmod 666 /dev/tty[pqrs]*
echo done

echo -n updating /etc/motd...
echo GNU\'s Not Unix Version `uname --release` > /tmp/newmotd
egrep -v 'GNU|Version' /etc/motd >> /tmp/t1
mv /tmp/t1 /etc/motd
chmod 664 /etc/motd
echo done

echo -n starting daemons:
/sbin/syslogd;		echo -n ' syslogd'
/sbin/inetd;		echo -n ' inet'
echo .	


date
