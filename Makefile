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
	psql < clean.sql

test: pgfuse
	psql < clean.sql
	psql < schema.sql
	-./pgfuse -s -v "" mnt
	mount | grep pgfuse
	# expect success for all
	-mkdir mnt/dir
	-mkdir mnt/dir/dir2
	-mkdir mnt/dir/dir3
	-echo "hello" > mnt/dir/dir2/afile
	-cp Makefile mnt/dir/dir2/bfile
	-cat mnt/dir/dir2/afile
	-ls -al mnt
	-ls -al mnt/dir/dir2
	-rmdir mnt/dir/dir3 
	# expect fail (directory not empty)
	-rmdir mnt/dir
	# expect fail (not a directory)
	-rmdir mnt/dir/dir2/bfile
	# expect success
	-rm mnt/dir/dir2/bfile
	fusermount -u mnt
	
pgfuse: pgfuse.o pgsql.o
	gcc -o pgfuse pgfuse.o pgsql.o $(LDFLAGS) 

pgfuse.o: pgfuse.c pgsql.h
	gcc -c $(CFLAGS) -o pgfuse.o pgfuse.c

pgsql.o: pgsql.c pgsql.h
	gcc -c $(CFLAGS) -o pgsql.o pgsql.c
