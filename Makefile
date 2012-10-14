CC       = gcc
CFLAGS   = -g -Wall
OBJ      = crc64.o mkfs.o
FUSE_LIB = `pkg-config fuse --libs`
FUSE_INC = `pkg-config fuse --cflags`

all: mkfs

mkfs: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(FUSE_LIB)

.c.o:
	$(CC) -c $(CFLAGS) $(DEFS) $(FUSE_INC) $<

clean:
	rm -rf *~ mkfs *.o
