	rm -f *.o ufs

$(OBJS): ufs.h 
$(OBJS): $(addprefix $(headers)/hurd/,diskfs.h pager.h ioserver.h \
				      fshelp.h ports.h)
alloc.o: fs.h dinode.h
consts.o: dinode.h
dir.o: dir.h
hyper.o: fs.h
inode.o: dinode.h fs.h
main.o: fs.h
pager.o: fs.h dinode.h
sizes.o: fs.h dinode.h
subr.o: fs.h
tables.o: fs.h

$(foreach dir,mkbootfs exec,../$(dir)/%): FORCE
	$(MAKE) -C $(@D) $(@F)
