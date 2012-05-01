CC=gcc

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

# get compilation flags for filesystem
CFLAGS += `getconf LFS_CFLAGS`

# debug
#CFLAGS += -I/usr/local/include/fuse
#LDFLAGS = -lpq /usr/local/lib/libfuse.a -pthread -ldl -lrt

# release
# use pkg-config to detemine compiler/linker flags for libfuse
CFLAGS += `pkg-config fuse --cflags`
LDFLAGS = `pkg-config fuse --libs` -lpq -pthread
