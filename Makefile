hurddir = $(prefix)/hurd
serversdir = $(prefix)/servers
libdir = $(prefix)/lib

.PHONY: all install
all: exec core bootexec
install: $(hurddir)/exec $(hurddir)/core $(serversdir)/exec $(serversdir)/core

vpath %.c ../$(machine)

exec bootexec core: hostarch.o $(libdir)/libc.a
exec bootexec: exec_machdep.o
exec: transexec.o

bootexec: bootexec.o exec.o
	$(LD) -X $(LDFLAGS) -r -o $@ $^

$(serversdir)/core: core.text $(hurddir)/core
	@rm -f $@
	cp $< $@
	settrans $(word 2,$^) $@

$(serversdir)/exec: exec.text
	@rm -f $@
	cp $< $@
	settrans '$(filter %/exec,$^)' $@

# This dependency makes the standard exec server be a translator.  Without
# it, /servers/exec has no translator, and bootexec is linked into the boot
# filesystem.  Uncomment the line to install the exec server as a
# translator.
#$(serversdir)/exec: $(hurddir)/exec
