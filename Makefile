TARGET = procfs
OBJS = procfs.o netfs.o procfs_dir.o \
       process.o proclist.o rootdir.o dircat.o main.o
LIBS = -lnetfs -lps -lfshelp

CC = gcc
CFLAGS = -Wall -g
CPPFLAGS =
LDFLAGS =

ifdef PROFILE
CFLAGS= -g -pg
CPPFLAGS= -DPROFILE
LDFLAGS= -static
LIBS= -lnetfs -lfshelp -liohelp -lps -lports -lthreads -lihash -lshouldbeinlibc
endif

CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) $(TARGET) $(OBJS)
