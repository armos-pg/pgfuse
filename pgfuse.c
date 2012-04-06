/*
    Copyright (C) 2012 Andreas Baumann <abaumann@yahoo.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <unistd.h>		/* for exit */
#include <stdlib.h>		/* for EXIT_FAILURE, EXIT_SUCCESS */
#include <string.h>		/* for strdup, memset */
#include <stdio.h>		/* for fprintf */
#include <stddef.h>		/* for offsetof */
#include <libgen.h>		/* for basename */

#include <fuse.h>		/* for user-land filesystem */
#include <fuse_opt.h>		/* fuse command line parser */

/* --- fuse callbacks --- */

static struct fuse_operations pgfuse_oper = {
	.getattr	= NULL,
	.readlink	= NULL,
	.mknod		= NULL,
	.mkdir		= NULL,
	.unlink		= NULL,
	.rmdir		= NULL,
	.symlink	= NULL,
	.rename		= NULL,
	.link		= NULL,
	.chmod		= NULL,
	.chown		= NULL,
	.utime		= NULL,
	.open		= NULL,
	.read		= NULL,
	.write		= NULL,
	.statfs		= NULL,
	.flush		= NULL,
	.release	= NULL,
	.fsync		= NULL,
	.setxattr	= NULL,
	.listxattr	= NULL,
	.removexattr	= NULL,
	.opendir	= NULL,
	.readdir	= NULL,
	.fsyncdir	= NULL,
	.init		= NULL,
	.destroy	= NULL,
	.access		= NULL,
	.create		= NULL,
	.ftruncate	= NULL,
	.fgetattr	= NULL,
	.lock		= NULL,
	.utimens	= NULL,
	.bmap		= NULL,
	.ioctl		= NULL,
	.poll		= NULL
};

/* --- parse arguments --- */

struct pgfuse {
	int print_help;		/* whether we should print a help page */
	int print_version;	/* whether we should print the version */
	int verbose;		/* whether we should be verbose */
	char *conninfo;		/* connection info as used in PQconnectdb */
	char *mountpoint;	/* where we mount the virtual filesystem */
	int read_only;		/* whether to mount read-only */
} pgfuse;

#define PGFUSE_OPT( t, p, v ) { t, offsetof( struct pgfuse, p ), v }

enum {
	KEY_HELP,
	KEY_VERBOSE,
	KEY_VERSION
};

static struct fuse_opt pgfuse_opts[] = {
	PGFUSE_OPT( 	"read_only",	read_only, 0 ),
	FUSE_OPT_KEY( 	"-h",		KEY_HELP ),
	FUSE_OPT_KEY( 	"--help",	KEY_HELP ),
	FUSE_OPT_KEY( 	"-v",		KEY_VERBOSE ),
	FUSE_OPT_KEY( 	"--verbose",	KEY_VERBOSE ),
	FUSE_OPT_KEY( 	"-V",		KEY_VERSION ),
	FUSE_OPT_KEY( 	"--version",	KEY_VERSION ),
	FUSE_OPT_END
};

static int pgfuse_opt_proc( void* data, const char* arg, int key,
                            struct fuse_args* outargs )
{                            
	switch( key ) {
		case FUSE_OPT_KEY_OPT:
			return 1;
		
		case FUSE_OPT_KEY_NONOPT:
			if( pgfuse.conninfo == NULL ) {
				pgfuse.conninfo = strdup( arg );
				return 0;
			} else {
				pgfuse.mountpoint = strdup( arg );
			}
			return 1;
			
		case KEY_HELP:
			pgfuse.print_help = 1;
			return -1;
		
		case KEY_VERBOSE:
			pgfuse.verbose = 1;
			return 0;
			
		case KEY_VERSION:
			pgfuse.print_version = 1;
			return -1;
		
		default:
			return -1;
	}
}
	
static void print_usage( char* progname )
{
	printf(
		"Usage: %s <Postgresql Connection String> <mountpoint>\n"
		"\n"
		"Postgresql Connection String (key=value separated with whitespaces) :\n"
		"\n"
		"    host                   optional (ommit for Unix domain sockets), e.g. 'localhost'\n"
		"    port                   default is 5432\n"
		"    dbname                 database to connect to\n"
		"    user                   database user to connect with\n"
		"    password               for password credentials (or rather use ~/.pgpass)\n"
		"    ...\n"
		"    for more options see libpq, PQconnectdb\n"
		"\n"
		"Example: \"dbname=test user=test password=xx\"\n"
		"\n"
		"Options:\n"
		"    -o opt,[opt...]        pgfuse options\n"
		"    -v   --verbose         make libcurl print verbose debug\n"
		"    -h   --help            print help\n"
		"    -V   --version         print version\n"
		"\n"
		"PgFuse options:\n"
		"    read_only           mount filesystem read-only, do not change data in database\n"
		"\n",
		progname
	);
}
		
static int parse_args( int argc, char *argv[] )
{
	struct fuse_args args = FUSE_ARGS_INIT( argc, argv );
	if( fuse_opt_parse( &args, &pgfuse, pgfuse_opts, pgfuse_opt_proc ) == -1 ) {
		if( pgfuse.print_help ) {
			/* print our options */
			print_usage( basename( argv[0] ) );
			/* print options of FUSE itself */
			argv[1] = "-ho";
			argv[2] = "mountpoint";
			fuse_main( 2, argv, &pgfuse_oper, NULL);
			return EXIT_SUCCESS;
		}
		if( pgfuse.print_version ) {
			fprintf( stderr, "0.0.1\n" );
			return EXIT_SUCCESS;
		}
		return EXIT_FAILURE;
	}
		
	return EXIT_SUCCESS;
}

/* --- main --- */

int main( int argc, char *argv[] )
{		
	int res;
	
	memset( &pgfuse, 0, sizeof( pgfuse ) );
	
	res = parse_args( argc, argv );
	
	exit( res );
}
