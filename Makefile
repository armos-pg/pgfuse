all: pgfuse

# name and version of package
PACKAGE_NAME = pgfuse
PACKAGE_VERSION = 0.0.1

# installation dirs
DESTDIR=
prefix=/usr

# standard directories following FHS
execdir=$(DESTDIR)$(prefix)
bindir=$(execdir)/bin
datadir=$(execdir)/share

# for debugging
#CFLAGS = -Wall -Werror -g -O0 -pthread
# for releasing
CFLAGS = -Wall -O2

# redhat has libpq-fe.h and fuse.h in /usr/include, ok

# suse has libpq-fe.h in
CFLAGS += -I/usr/include/pgsql

# debianish systems have libpg-fe.h in
CFLAGS += -I/usr/include/postgresql

# declare version of FUSE API we want to program against
CFLAGS += -DFUSE_USE_VERSION=26

CFLAGS += -D_FILE_OFFSET_BITS=64

# debug
#CFLAGS += -I/usr/local/include/fuse
#LDFLAGS = -lpq /usr/local/lib/libfuse.a -pthread -ldl -lrt

# release
# use pkg-config to detemine compiler/linker flags for libfuse
CFLAGS += `pkg-config fuse --cflags`
LDFLAGS = `pkg-config fuse --libs` -lpq -pthread

PG_CONNINFO = ""

clean:
	rm -f pgfuse pgfuse.o pgsql.o pool.o
	rm -f testfsync testfsync.o
	rm -f testpgsql testpgsql.o
	psql < clean.sql

test: pgfuse testfsync testpgsql
	psql < clean.sql
	psql < schema.sql
	test -d mnt || mkdir mnt
	./pgfuse -s -v "$(PG_CONNINFO)" mnt
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
	# expect success on chmod
	-chmod 777 mnt/dir/dir2/bfile
	-ls -al mnt/dir/dir2/bfile
	# expect success on symlink creation
	-ln -s bfile mnt/dir/dir2/clink
	-ls -al mnt/dir/dir2/clink
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
	# show times of dirs, files and symlinks
	-stat mnt/dir/dir2/afile
	-stat mnt/dir/dir3
	-stat mnt/dir/dir2/clink
	# show filesystem stats (statvfs)
	-stat -f mnt
	# expect success, truncate a file (grow and shrink)
	-touch mnt/trunc
	-ls -al mnt/trunc
	-truncate --size 2049 mnt/trunc
	-ls -al mnt/trunc
	-dd if=/dev/zero of=mnt/trunc bs=512 count=10
	-truncate --size 513 mnt/trunc
	-ls -al mnt/trunc
	# END: unmount FUSE file system
	fusermount -u mnt
	
pgfuse: pgfuse.o pgsql.o pool.o
	$(CC) -o pgfuse pgfuse.o pgsql.o pool.o $(LDFLAGS) 

pgfuse.o: pgfuse.c pgsql.h pool.h config.h
	$(CC) -c $(CFLAGS) -o pgfuse.o pgfuse.c

pgsql.o: pgsql.c pgsql.h config.h
	$(CC) -c $(CFLAGS) -o pgsql.o pgsql.c

pool.o: pool.c pool.h
	$(CC) -c $(CFLAGS) -o pool.o pool.c
	
testfsync: testfsync.o
	$(CC) -o testfsync testfsync.o

testfsync.o: testfsync.c
	$(CC) -c $(CFLAGS) -o testfsync.o testfsync.c
	
testpgsql: testpgsql.o
	$(CC) -o testpgsql testpgsql.o $(LDFLAGS)

testpgsql.o: testpgsql.c
	$(CC) -c $(CFLAGS) -o testpgsql.o testpgsql.c

install: all
	test -d "$(bindir)" || mkdir -p "$(bindir)"
	cp pgfuse "$(bindir)"
	test -d "$(datadir)/man/man1" || mkdir -p "$(datadir)/man/man1"
	cp pgfuse.1 "$(datadir)/man/man1"
	gzip "$(datadir)/man/man1/pgfuse.1"
	test -d "$(datadir)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)" || \
		mkdir -p "$(datadir)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)"
	cp schema.sql "$(datadir)/$(PACKAGE_NAME)-$(PACKAGE_VERSION)"
	
dist:
	rm -rf /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	mkdir /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	cp -r * /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION)/.
	cd /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION); \
		$(MAKE) clean; \
		cd .. ; \
		tar cvf $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar \
			$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	rm -rf /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION)
	mv /tmp/$(PACKAGE_NAME)-$(PACKAGE_VERSION).tar .

dist-gz: dist
	rm -f $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar.gz
	gzip $(PACKAGE_NAME)-$(PACKAGE_VERSION).tar
