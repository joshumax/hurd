hurd = exec core
servers = exec core
bin = gcore

include ../Makerules

exec core: hostarch.o $(libdir)/libtrivfs.a $(libdir)/libc.a
exec: exec_server.o exec_user.o exec_machdep.o
core: core_server.o
exec.o: exec_server.h
core.o: core_server.h

# This dependency makes the standard exec server be a translator.  Without
# it, /servers/exec has no translator, and exec is inserted into the boot
# filesystem's load image with makeboot.  Uncomment the line to install the
# exec server as a translator.
#$(serversdir)/exec: $(hurddir)/exec

$(serversdir)/exec: exec.text
	@rm -f $@
	$(INSTALL_DATA) $< $@
	settrans '$(word 2,$^)' $@
