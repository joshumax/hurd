.PHONY: all install
all: exec core bootexec

install: $(hurddir)/exec $(hurddir)/core	\
	 $(serversdir)/exec $(serversdir)/core	\
	 $(bindir)/gcore

hurddir = $(prefix)/hurd
serversdir = $(prefix)/servers
libdir = $(prefix)/lib
bindir = $(prefix)/bin

INSTALL_DATA = $(INSTALL)
INSTALL = install -c

$(hurddir)/%: %
	$(INSTALL) $< $@

$(bindir)/%: %
	$(INSTALL) $< $@

vpath %_machdep.c ../$(machine)

exec bootexec core: hostarch.o $(libdir)/libc.a
exec bootexec: exec_machdep.o
exec: transexec.o exec_server.o exec_user.o
core: core_server.o
exec.o: exec_server.h
core.o: core_server.h

%_server.c %_server.h: %.defs
	$(MIG) $(MIGFLAGS) -server $(@:.h=.c) -sheader $(@:.c=.h)

bootexec: bootexec.o exec.o
	$(LD) -X $(LDFLAGS) -r -o $@ $^

$(serversdir)/core: core.text $(hurddir)/core
	@rm -f $@
	$(INSTALL_DATA) $< $@
	settrans $(word 2,$^) $@

$(serversdir)/exec: exec.text
	@rm -f $@
	$(INSTALL_DATA) $< $@
	settrans '$(word 2,$^)' $@

# This dependency makes the standard exec server be a translator.  Without
# it, /servers/exec has no translator, and bootexec is linked into the boot
# filesystem.  Uncomment the line to install the exec server as a
# translator.
#$(serversdir)/exec: $(hurddir)/exec
