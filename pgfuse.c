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
#include <syslog.h>		/* for openlog, syslog */
#include <errno.h>		/* for ENOENT and friends */
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>		/* for htonl, ntohl */

#include <fuse.h>		/* for user-land filesystem */
#include <fuse_opt.h>		/* fuse command line parser */

#include <libpq-fe.h>		/* for Postgresql database access */

/* --- fuse callbacks --- */

typedef struct PgFuseData {
	int verbose;		/* whether we should be verbose */
	char *conninfo;		/* connection info as used in PQconnectdb */
	char *mountpoint;	/* where we mount the virtual filesystem */
	PGconn *conn;		/* the database handle to operate on */
} PgFuseData;

static void *pgfuse_init( struct fuse_conn_info *conn )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Mounting file system on '%s' (%s)",
			data->mountpoint, data->conninfo );
	}
	
	return data;
}

static void pgfuse_destroy( void *userdata )
{
	PgFuseData *data = (PgFuseData *)userdata;
	if( data->verbose ) {
		syslog( LOG_INFO, "Unmounting file system on '%s' (%s)",
			data->mountpoint, data->conninfo );
	}
}

static int pgfuse_getattr( const char *path, struct stat *stbuf )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "GetAttrs '%s' on '%s'", path, data->mountpoint );
	}
	
	memset( stbuf, 0, sizeof( struct stat ) );
	
	if( strcmp( path, "/" ) == 0 ) {
		/* show contents of the root path */
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		return 0;
	} else if( strcmp( path, "/README" ) == 0 ) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen( "This is a PgFuse mount point" );
		return 0;
	} else {
		return -ENOENT;
	}
}

static int pgfuse_readdir( const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Readdir '%s'", path );
	}
	
	filler( buf, ".", NULL, 0 );
	filler( buf, "..", NULL, 0 );
	filler( buf, "README", NULL, 0 );
	
	return 0;
}

static int psql_get_parent_id( PGconn *conn, const char *path )
{
	PGresult *res;
	int i_fnum;
	char *iptr;
	int parent_id;
	
	const char *values[1] = { path };
	int lengths[1] = { strlen( path ) };
	int binary[1] = { 1 };
	
	res = PQexecParams( conn, "SELECT parent_id FROM dir WHERE path = $1::varchar",
		1, NULL, values, lengths, binary, 1 );
	
	if( PQresultStatus( res ) != PGRES_TUPLES_OK ) {
		syslog( LOG_ERR, "Error in psql_get_parent_id for path '%s'", path );
		return -EIO;
	}
	
	if( PQntuples( res ) != 1 ) {
		syslog( LOG_ERR, "Expecting exactly one inode for path '%s' in psql_get_parent_id, data inconsistent!", path );
		return -EIO;
	}
	
	i_fnum = PQfnumber( res, "parent_id" );
	iptr = PQgetvalue( res, 0, i_fnum );
	parent_id = ntohl( *( (uint32_t *)iptr ) );
	
	return parent_id;
}

static int pgfuse_mkdir( const char *path, mode_t mode )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	char *parent_path;
	int parent_id;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Mkdir '%s' in mode '%o'", path, (unsigned int)mode );
	}
	
	parent_path = strdup( path );
	if( parent_path == NULL ) {
		syslog( LOG_ERR, "Out of memory in Mkdir '%s'!", path );
		return -EIO;
	}
	
	dirname( parent_path );

	parent_id = psql_get_parent_id( data->conn, parent_path );
	if( parent_id < 0 ) {
		return parent_id;
	}
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Parent_id for new dir '%s' is %d", path, parent_id );
	}

	
	free( parent_path );
	
	return 0;
}

static struct fuse_operations pgfuse_oper = {
	.getattr	= pgfuse_getattr,
	.readlink	= NULL,
	.mknod		= NULL,
	.mkdir		= pgfuse_mkdir,
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
	.readdir	= pgfuse_readdir,
	.fsyncdir	= NULL,
	.init		= pgfuse_init,
	.destroy	= pgfuse_destroy,
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

typedef struct PgFuse {
	int print_help;		/* whether we should print a help page */
	int print_version;	/* whether we should print the version */
	int verbose;		/* whether we should be verbose */
	char *conninfo;		/* connection info as used in PQconnectdb */
	char *mountpoint;	/* where we mount the virtual filesystem */
	int read_only;		/* whether to mount read-only */
} PgFuse;

static PgFuse pgfuse;

#define PGFUSE_OPT( t, p, v ) { t, offsetof( struct PgFuse, p ), v }

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
			} else if( pgfuse.mountpoint == NULL ) {
				pgfuse.mountpoint = strdup( arg );
				return 1;
			} else {
				fprintf( stderr, "%s, only two arguments allowed: Postgresql connection data and mountpoint\n", basename( outargs->argv[0] ) );
				return -1;
			}
			
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
		
/* --- main --- */

int main( int argc, char *argv[] )
{		
	int res;
	PGconn *conn;
	struct fuse_args args = FUSE_ARGS_INIT( argc, argv );
	PgFuseData userdata;
	
	memset( &pgfuse, 0, sizeof( pgfuse ) );
	
	if( fuse_opt_parse( &args, &pgfuse, pgfuse_opts, pgfuse_opt_proc ) == -1 ) {
		if( pgfuse.print_help ) {
			/* print our options */
			print_usage( basename( argv[0] ) );
			/* print options of FUSE itself */
			argv[1] = "-ho";
			argv[2] = "mountpoint";
			fuse_main( 2, argv, &pgfuse_oper, NULL);
			exit( EXIT_SUCCESS );
		}
		if( pgfuse.print_version ) {
			fprintf( stderr, "0.0.1\n" );
			exit( EXIT_SUCCESS );
		}
		exit( EXIT_FAILURE );
	}
	
	if( pgfuse.conninfo == NULL ) {
		fprintf( stderr, "Missing Postgresql connection data\n" );
		fprintf( stderr, "see '%s -h' for usage\n", basename( argv[0] ) );
		exit( EXIT_FAILURE );
	}
	
	conn = PQconnectdb( pgfuse.conninfo );
	if( PQstatus( conn ) != CONNECTION_OK ) {
		fprintf( stderr, "Connection to database failed: %s",
			PQerrorMessage( conn ) );
		PQfinish( conn );
		exit( EXIT_FAILURE );
	}
	
	openlog( basename( argv[0] ), LOG_PID, LOG_USER );
	
	memset( &userdata, 0, sizeof( PgFuseData ) );
	userdata.conninfo = pgfuse.conninfo;
	userdata.conn = conn;
	userdata.mountpoint = pgfuse.mountpoint;
	userdata.verbose = pgfuse.verbose;
	
	res = fuse_main( args.argc, args.argv, &pgfuse_oper, &userdata );

	PQfinish( conn );
	
	exit( res );
}
