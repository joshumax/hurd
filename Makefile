TARGET = procfs
OBJS = procfs.o netfs.o procfs_dir.o \
       process.o proclist.o rootdir.o dircat.o main.o mach_debugUser.o
LIBS = -lnetfs -lps -lfshelp -lpthread

CC = gcc
CFLAGS = -Wall -g
CPPFLAGS =
LDFLAGS =

ifdef PROFILE
CFLAGS= -g -pg
CPPFLAGS= -DPROFILE
LDFLAGS= -static
LIBS= -lnetfs -lfshelp -liohelp -lps -lports -lpthread -lihash -lshouldbeinlibc
endif

CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64

all: $(TARGET)

rootdir.o: rootdir.c mach_debug_U.h

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) $(TARGET) $(OBJS)

# This is the gist of the MIG user stub handling from Hurd's build
# system:

# Where to find .defs files.
vpath %.defs /usr/include/mach_debug

MIG = mig
MIGCOM = $(MIG) -cc cat - /dev/null
MIGCOMFLAGS := -subrprefix __

%.udefsi: %.defs
	$(CPP) -x c $(CPPFLAGS) $(MIGUFLAGS) $($*-MIGUFLAGS) \
	  $< -o $*.udefsi

%_U.h %User.c: %.udefsi
	$(MIGCOM) $(MIGCOMFLAGS) $(MIGCOMUFLAGS) $($*-MIGCOMUFLAGS) < $< \
		  -user $*User.c -server /dev/null -header $*_U.h
