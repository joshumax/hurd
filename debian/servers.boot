# GNU Mach boot script for Debian GNU/Hurd.  Each line specifies a
# file for serverboot to load (the first word), and actions to be done
# with it.

# First, the bootstrap filesystem.  It needs several ports as arguments,
# as well as the user flags from the boot loader.
/hurd/ext2fs.static --bootflags=${boot-args} --host-priv-port=${host-port} --device-master-port=${device-port} --exec-server-task=${exec-task} -Tdevice ${root-device} $(task-create) $(task-resume)

# Now the exec server; to load the dynamically-linked exec server
# program, we have serverboot in fact load and run ld.so, which in
# turn loads and runs /hurd/exec.  This task is created, and its task
# port saved in ${exec-task} to be passed to the fs above, but it is
# left suspended; the fs will resume the exec task once it is ready.
/lib/ld.so.1 /hurd/exec $(exec-task=task-create)

# To swap to a Linux swap partition, use something like the following.
# You can also add swap partitions to /etc/fstab.
#/dev/hd0s2 $(add-linux-paging-file)

# Don't make serverboot the default pager.  The real default pager will
# we started early in /libexec/rc.
die $(serverboot)
