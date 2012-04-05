all: pgfuse

# for debugging
CFLAGS = -Wall -Wextra -pedantic -g
# for releasing
CFLAGS = -Wall

# declare version of FUSE API we want to program against
CFLAGS += -DFUSE_USE_VERSION=26

# use pkg-config to detemine compiler/linker flags for libfuse
CFLAGS += `pkg-config fuse --cflags`
LDFLAGS = `pkg-config fuse --libs`

clean:
	rm -f pgfuse pgfuse.o

pgfuse: pgfuse.o pgfuse_cmdline.o
	gcc -o pgfuse $(LDFLAGS) pgfuse.o pgfuse_cmdline.o

pgfuse_cmdline.o: pgfuse_cmdline.c pgfuse_cmdline.h
	gcc -c $(CFLAGS) -o pgfuse_cmdline.o pgfuse_cmdline.c

pgfuse.o: pgfuse.c pgfuse_cmdline.h
	gcc -c $(CFLAGS) -o pgfuse.o pgfuse.c

pgfuse_cmdline.h: pgfuse.ggo
	gengetopt -F pgfuse_cmdline --conf-parser -i pgfuse.ggo

pgfuse_cmdline.c: pgfuse.ggo
	gengetopt -F pgfuse_cmdline --conf-parser -i pgfuse.ggo 
