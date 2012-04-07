all: pgfuse

# for debugging
CFLAGS = -Wall -g -O0
# for releasing
#CFLAGS = -Wall -O2

# declare version of FUSE API we want to program against
CFLAGS += -DFUSE_USE_VERSION=26

# use pkg-config to detemine compiler/linker flags for libfuse
CFLAGS += `pkg-config fuse --cflags`
LDFLAGS = `pkg-config fuse --libs` -lpq

clean:
	rm -f pgfuse pgfuse.o

test: pgfuse
	psql < test.sql
	-./pgfuse -s -v "" mnt
	mount | grep pgfuse
	-mkdir mnt/dir
	-mkdir mnt/dir/dir2
	-echo "hello" > mnt/dir/dir2/afile
	-cp Makefile mnt/dir/dir2/bfile
	-cat mnt/dir/dir2/afile
	-ls -al mnt
	-ls -al mnt/dir/dir2
	fusermount -u mnt
	
pgfuse: pgfuse.o
	gcc -o pgfuse $(LDFLAGS) pgfuse.o

pgfuse.o: pgfuse.c
	gcc -c $(CFLAGS) -o pgfuse.o pgfuse.c
