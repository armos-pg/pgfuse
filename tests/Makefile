include ../inc.mak

PG_CONNINFO = ""

CFLAGS += -I..

test: testfsync testpgsql testtypes testbigfile
	psql < clean.sql
	psql < ../schema.sql
	test -d mnt || mkdir mnt
	../pgfuse -s -v "$(PG_CONNINFO)" mnt
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
	-mv mnt/dir/dir2/clink mnt/dir/dir4/dlink
	# expect fail (directory not empty)
	-rmdir mnt/dir
	# expect fail (not a directory)
	-rmdir mnt/dir/dir2/bfile
	# test fdatasync and fsync
	./testfsync
	# show times of dirs, files and symlinks
	-stat mnt/dir/dir4/bfile
	-stat mnt/dir/dir4
	-stat mnt/dir/dir4/dlink
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
	# expect success, write a sparse big file
	-./testbigfile
	-ls -al mnt/testbigfile.data
	# END: unmount FUSE file system
	fusermount -u mnt

clean:
	rm -f testfsync testfsync.o
	rm -f testpgsql testpgsql.o
	rm -f testtypes testtypes.o
	rm -f testbigfile testbigfile.o
	
testfsync: testfsync.o
	$(CC) -o testfsync testfsync.o

testfsync.o: testfsync.c
	$(CC) -c $(CFLAGS) -o testfsync.o testfsync.c
	
testpgsql: testpgsql.o
	$(CC) -o testpgsql testpgsql.o $(LDFLAGS)

testpgsql.o: testpgsql.c
	$(CC) -c $(CFLAGS) -o testpgsql.o testpgsql.c

testtypes: testtypes.o
	$(CC) -o testtypes testtypes.o

testtypes.o: testtypes.c
	$(CC) -c $(CFLAGS) -o testtypes.o testtypes.c

testbigfile: testbigfile.o
	$(CC) -o testbigfile testbigfile.o

testbigfile.o: testbigfile.c
	$(CC) -c $(CFLAGS) -o testbigfile.o testbigfile.c
