TARGET = procfs
OBJS = procfs.o netfs.o procfs_file.o procfs_dir.o \
       process.o proclist.o rootdir.o dircat.o main.o
LIBS = -lnetfs -lps

CC = gcc
CFLAGS = -Wall -g
CPPFLAGS =

CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	$(RM) $(TARGET) $(OBJS)
