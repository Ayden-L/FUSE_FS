CC=gcc
CFLAGS=-g -Wall -D_FILE_OFFSET_BITS=64
LDFLAGS=-lfuse
MOUNTDIR?=/tmp/aa2535/mountdir


RUFS?=rufs.o
FUSE_RUN_COMMAND?= ./rufs -s $(MOUNTDIR)



ifeq ($(DEBUG), true)
	RUFS = rufs_debugging.o
	FUSE_RUN_COMMAND= ./rufs -s -d $(MOUNTDIR)
endif

OBJ=$(RUFS) block.o



%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

rufs: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o rufs

.PHONY: clean

clean:
	rm -f *.o rufs

mount: 
	rm -f ./DISKFILE
	$(MAKE) clean
	clear 
	$(MAKE) 
	clear
	fusermount -u $(MOUNTDIR)
	$(FUSE_RUN_COMMAND)
remount: 
	rm -f ./DISKFILE
	$(MAKE) clean
	clear 
	$(MAKE) 
	clear
	$(FUSE_RUN_COMMAND)


mount-keep-diskfile: 
	$(MAKE) clean
	clear 
	$(MAKE) 
	clear
	$(FUSE_RUN_COMMAND)
