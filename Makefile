all: pgfuse

# for debugging
CFLAGS = -Wall -Werror -g -O0 -pthread
# for releasing
#CFLAGS = -Wall -O2

# declare version of FUSE API we want to program against
CFLAGS += -DFUSE_USE_VERSION=29

CFLAGS += -D_FILE_OFFSET_BITS=64

# debug
#CFLAGS += -I/usr/local/include/fuse
#LDFLAGS = -lpq /usr/local/lib/libfuse.a -pthread -ldl -lrt

# release
# use pkg-config to detemine compiler/linker flags for libfuse
CFLAGS += `pkg-config fuse --cflags`
LDFLAGS = `pkg-config fuse --libs` -lpq

clean:
	rm -f pgfuse pgfuse.o pgsql.o
	rm -f testfsync testfsync.o
	psql < clean.sql

test: pgfuse testfsync
	psql < clean.sql
	psql < schema.sql
	test -d mnt || mkdir mnt
	./pgfuse -s -v "" mnt
	mount | grep pgfuse
	# expect success for making directories
	-mkdir mnt/dir
	-mkdir mnt/dir/dir2
	-mkdir mnt/dir/dir3
	# expect success on open and file write
	-echo "hello" > mnt/dir/dir2/afile
	-cp Makefile mnt/dir/dir2/bfile
	# expect success on open and file read
	-cat mnt/dir/dir2/afile
	-ls -al mnt
	-ls -al mnt/dir/dir2
	# expect success on rmdir
	-rmdir mnt/dir/dir3
	# expect success on file removal
	-rm mnt/dir/dir2/bfile
	# expect success on rename 
	-mkdir mnt/dir/dir3
	-mv mnt/dir/dir3 mnt/dir/dir4
	-mv mnt/dir/dir2/afile mnt/dir/dir4/bfile
	# expect fail (directory not empty)
	-rmdir mnt/dir
	# expect fail (not a directory)
	-rmdir mnt/dir/dir2/bfile
	# test fdatasync and fsync
	./testfsync
	sleep 2
	# show filesystem stats (statvfs)
	df -k mnt
	df -i mnt
	fusermount -u mnt
	
pgfuse: pgfuse.o pgsql.o
	$(CC) -o pgfuse pgfuse.o pgsql.o $(LDFLAGS) 

pgfuse.o: pgfuse.c pgsql.h
	$(CC) -c $(CFLAGS) -o pgfuse.o pgfuse.c

pgsql.o: pgsql.c pgsql.h
	$(CC) -c $(CFLAGS) -o pgsql.o pgsql.c

testfsync: testfsync.o
	$(CC) -o testfsync testfsync.o

testfsync.o: testfsync.c
	$(CC) -c $(CFLAGS) -o testfsync.o testfsync.c
	
	
