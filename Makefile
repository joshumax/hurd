.PHONY: all install
all: exec core
install: $(hurddir)/exec $(hurddir)/core $(serversdir)/exec $(serversdir)/core

exec core: hostarch.o
exec: exec_machdep.o

$(serversdir)/%: %.text $(hurddir)/%
	@rm -f $@
	cp $< $@
	settrans $(word 2,$^) $@
