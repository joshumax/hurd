hurddir = $(prefix)/hurd
serversdir = $(prefix)/servers

.PHONY: all install
all: exec core bootexec
install: $(hurddir)/exec $(hurddir)/core $(serversdir)/exec $(serversdir)/core

exec bootexec core: hostarch.o
exec bootexec: exec_machdep.o
exec: transexec.o

bootexec: bootexec.o exec.o
	$(LD) $(LDFLAGS) -r -o $@ $^

$(serversdir)/core: core.text $(hurddir)/core
	@rm -f $@
	cp $< $@
	settrans $(word 2,$^) $@

$(serversdir)/exec: exec.text
	@rm -f $@
	cp $< $@
