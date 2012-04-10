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
	rm -f pgfuse pgfuse.o pgsql.o hash.o testhash testhash.o

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
	
pgfuse: pgfuse.o pgsql.o hash.o
	gcc -o pgfuse $(LDFLAGS) pgfuse.o pgsql.o hash.o

pgfuse.o: pgfuse.c pgsql.h
	gcc -c $(CFLAGS) -o pgfuse.o pgfuse.c

pgsql.o: pgsql.c pgsql.h
	gcc -c $(CFLAGS) -o pgsql.o pgsql.c

hash.o: hash.c hash.h
	gcc -c $(CFLAGS) -o hash.o hash.c

testhash: testhash.o hash.o
	gcc -o testhash $(LDFLAGS) testhash.o hash.o

testhash.o: testhash.c hash.h
	gcc -c $(CFLAGS) -o testhash.o testhash.c
