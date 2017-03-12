# Boot script file for booting GNU Hurd.  Each line specifies a file to be
# loaded by the boot loader (the first word), and actions to be done with it.

# First, the bootstrap filesystem.  It needs several ports as arguments,
# as well as the user flags from the boot loader.
/hurd/ext2fs.static --multiboot-command-line=${kernel-command-line} --host-priv-port=${host-port} --device-master-port=${device-port} --exec-server-task=${exec-task} --kernel-task=${kernel-task} -T typed ${root} $(task-create) $(task-resume)


# Now the exec server; to load the dynamically-linked exec server program,
# we have the boot loader in fact load and run ld.so, which in turn
# loads and runs /hurd/exec.  This task is created, and its task port saved
# in ${exec-task} to be passed to the fs above, but it is left suspended;
# the fs will resume the exec task once it is ready.
/lib/ld.so.1 /hurd/exec $(exec-task=task-create)
