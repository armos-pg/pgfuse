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

#include <string.h>		/* for strdup, strlen, memset */
#include <libgen.h>		/* for POSIX compliant basename */
#include <unistd.h>		/* for exit, geteuid, getegid */
#include <stdlib.h>		/* for EXIT_FAILURE, EXIT_SUCCESS */
#include <stdio.h>		/* for fprintf */
#include <stddef.h>		/* for offsetof */
#include <syslog.h>		/* for openlog, syslog */
#include <errno.h>		/* for ENOENT and friends */
#include <sys/types.h>		/* size_t */
#include <sys/stat.h>		/* mode_t */
#include <values.h>		/* for INT_MAX */

#include <fuse.h>		/* for user-land filesystem */
#include <fuse_opt.h>		/* fuse command line parser */

#if FUSE_VERSION < 28
#error Currently only written for newest FUSE  APIversion (FUSE_VERSION 28)
#endif

#include "config.h"		/* compiled in defaults */
#include "pgsql.h"		/* implements Postgresql accessers */
#include "pool.h"		/* implements the connection pool */

/* --- fuse callbacks --- */

typedef struct PgFuseData {
	int verbose;		/* whether we should be verbose */
	char *conninfo;		/* connection info as used in PQconnectdb */
	char *mountpoint;	/* where we mount the virtual filesystem */
	PGconn *conn;		/* the database handle to operate on (single-thread) */
	PgConnPool pool;	/* the database pool to operate on (multi-thread) */
	int read_only;		/* whether the mount point is read-only */
	int multi_threaded;	/* whether we run multi-threaded */
} PgFuseData;

/* --- helper functions --- */

static struct timespec now( void )
{
	int res;
	struct timeval t;
	struct timezone tz;
	struct timespec s;
	
	res = gettimeofday( &t, &tz );
	if( res != 0 ) {
		s.tv_sec = 0;
		s.tv_nsec = 0;
		return s;
	}
	
	s.tv_sec = t.tv_sec;
	s.tv_nsec = t.tv_usec * 1000;
	
	return s;
}

/* --- implementation of FUSE hooks --- */

static void *pgfuse_init( struct fuse_conn_info *conn )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	
	syslog( LOG_INFO, "Mounting file system on '%s' ('%s', %s), thread #%d",
		data->mountpoint, data->conninfo,
		data->read_only ? "read-only" : "read-write",
		fuse_get_context( )->uid );
	
	/* in single-threaded case we just need one shared PostgreSQL connection */
	if( !data->multi_threaded ) {
		data->conn = PQconnectdb( data->conninfo );
		if( PQstatus( data->conn ) != CONNECTION_OK ) {
			syslog( LOG_ERR, "Connection to database failed: %s",
				PQerrorMessage( data->conn ) );
			PQfinish( data->conn );
			exit( EXIT_FAILURE );
		}
	} else {
		int res;
		res = psql_pool_init( &data->pool, data->conninfo, MAX_DB_CONNECTIONS );
		if( res < 0 ) {
			syslog( LOG_ERR, "Allocating database connection pool failed!" );
			exit( EXIT_FAILURE );
		}
	}
	
	return data;
}

static void pgfuse_destroy( void *userdata )
{
	PgFuseData *data = (PgFuseData *)userdata;

	syslog( LOG_INFO, "Unmounting file system on '%s' (%s), thread #%d",
		data->mountpoint, data->conninfo, fuse_get_context( )->uid );

	if( !data->multi_threaded ) {
		PQfinish( data->conn );
	} else {
		(void)psql_pool_destroy( &data->pool );
	}
}

static PGconn *psql_acquire( PgFuseData *data )
{
	if( !data->multi_threaded ) {
		return data->conn;
	}
	
	return psql_pool_acquire( &data->pool, fuse_get_context( )->pid );
}

static void psql_release( PgFuseData *data )
{
	if( !data->multi_threaded ) return;
	
	(void)psql_pool_release( &data->pool, fuse_get_context( )->pid );
}

#define ACQUIRE( C ) \
	C = psql_acquire( data ); \
	if( C == NULL ) return -EIO;
	
#define RELEASE( C ) \
	(void)psql_release( data )

static int pgfuse_fgetattr( const char *path, struct stat *stbuf, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;
	PGconn *conn;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "FgetAttrs '%s' on '%s', thread #%d",
			path, data->mountpoint, fuse_get_context( )->uid );
	}

	ACQUIRE( conn );
	PSQL_BEGIN( conn );
	
	memset( stbuf, 0, sizeof( struct stat ) );

	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}

	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id for %s '%s' is %d, thread #%d",
			S_ISDIR( meta.mode ) ? "dir" : "file", path, id,
			fuse_get_context( )->uid );
	}
	
	stbuf->st_ino = id;
	stbuf->st_blocks = 0;
	stbuf->st_mode = meta.mode;
	stbuf->st_size = meta.size;
	stbuf->st_blksize = STANDARD_BLOCK_SIZE;
	stbuf->st_blocks = ( meta.size + STANDARD_BLOCK_SIZE - 1 ) / STANDARD_BLOCK_SIZE;
	/* TODO: set correctly from table */
	stbuf->st_nlink = 2;
	/* set rights to the user running 'pgfuse' */
	stbuf->st_uid = meta.uid;
	stbuf->st_gid = meta.gid;

	PSQL_COMMIT( conn );
	
	return 0;
}

static int pgfuse_getattr( const char *path, struct stat *stbuf )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "GetAttrs '%s' on '%s', thread #%d",
			path, data->mountpoint, fuse_get_context( )->uid );
	}

	ACQUIRE( conn );
	PSQL_BEGIN( conn );
	
	memset( stbuf, 0, sizeof( struct stat ) );

	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}

	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id for %s '%s' is %d, thread #%d",
			S_ISDIR( meta.mode ) ? "dir" : "file", path, id,
			fuse_get_context( )->uid );
	}
	
	stbuf->st_ino = id;
	stbuf->st_blocks = 0;
	stbuf->st_mode = meta.mode;
	stbuf->st_size = meta.size;
	stbuf->st_blksize = STANDARD_BLOCK_SIZE;
	stbuf->st_blocks = ( meta.size + STANDARD_BLOCK_SIZE - 1 ) / STANDARD_BLOCK_SIZE;
	/* TODO: set correctly from table */
	stbuf->st_nlink = 2;
	/* set rights to the user running 'pgfuse' */
	stbuf->st_uid = meta.uid;
	stbuf->st_gid = meta.gid;
	stbuf->st_atime = meta.atime.tv_sec;
	stbuf->st_mtime = meta.mtime.tv_sec;
	stbuf->st_ctime = meta.ctime.tv_sec;

	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_access( const char *path, int mode )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;

	if( data->verbose ) {
		syslog( LOG_INFO, "Access on '%s' and mode '%o, thread #%d",
			path, (unsigned int)mode, fuse_get_context( )->uid );
	}
	
	/* TODO: check access, but not now. grant always access */
	return 0;
}

static char *flags_to_string( int flags )
{
	char *s;
	char *mode_s;
	
	if( ( flags & O_ACCMODE ) == O_WRONLY ) mode_s = "O_WRONLY";
	else if( ( flags & O_ACCMODE ) == O_RDWR ) mode_s = "O_RDWR";
	else if( ( flags & O_ACCMODE ) == O_RDONLY ) mode_s = "O_RDONLY";
	
	s = (char *)malloc( 100 );
	if( s == NULL ) return "<memory allocation failed>";

	snprintf( s, 100, "access_mode=%s, flags=%s%s%s%s",
		mode_s,
		( flags & O_CREAT ) ? "O_CREAT " : "",
		( flags & O_TRUNC ) ? "O_TRUNC " : "",
		( flags & O_EXCL ) ? "O_EXCL " : "",
		( flags & O_APPEND ) ? "O_APPEND " : "");
	
	return s;
}

static int pgfuse_create( const char *path, mode_t mode, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;
	char *copy_path;
	char *parent_path;
	char *new_file;
	int parent_id;
	int res;
	PGconn *conn;

	if( data->verbose ) {
		char *s = flags_to_string( fi->flags );
		syslog( LOG_INFO, "Create '%s' in mode '%o' on '%s' with flags '%s', thread #%d",
			path, mode, data->mountpoint, s, fuse_get_context( )->uid );
		if( *s != '<' ) free( s );
	}
	
	ACQUIRE( conn );		
	PSQL_BEGIN( conn );
	
	if( data->read_only ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EROFS;
	}
	
	id = psql_get_meta( conn, path, &meta );
	if( id < 0 && id != -ENOENT ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	
	if( id >= 0 ) {
		if( data->verbose ) {
			syslog( LOG_DEBUG, "Id for dir '%s' is %d, thread #%d",
				path, id, fuse_get_context( )->uid );
		}
		
		if( S_ISDIR(meta.mode ) ) {
			PSQL_ROLLBACK( conn ); RELEASE( conn );
			return -EISDIR;
		}
		
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EEXIST;
	}
	
	copy_path = strdup( path );
	if( copy_path == NULL ) {
		syslog( LOG_ERR, "Out of memory in Create '%s'!", path );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOMEM;
	}
	
	parent_path = dirname( copy_path );

	parent_id = psql_get_meta( conn, parent_path, &meta );
	if( parent_id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return parent_id;
	}
	if( !S_ISDIR(meta.mode ) ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOENT;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Parent_id for new file '%s' in dir '%s' is %d, thread #%d",
			path, parent_path, parent_id, fuse_get_context( )->uid );
	}
	
	free( copy_path );
	copy_path = strdup( path );
	if( copy_path == NULL ) {
		free( parent_path );
		syslog( LOG_ERR, "Out of memory in Create '%s'!", path );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOMEM;
	}
	
	new_file = basename( copy_path );
	
	meta.size = 0;
	meta.mode = mode;
	/* TODO: use FUSE context */
	meta.uid = geteuid( );
	meta.gid = getegid( );
	meta.ctime = now( );
	meta.mtime = meta.ctime;
	meta.atime = meta.ctime;
	meta.ref_count = 1;
	
	res = psql_create_file( conn, parent_id, path, new_file, meta );
	if( res < 0 ) {
		free( copy_path );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		free( copy_path );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id for new file '%s' is %d, thread #%d",
			path, id, fuse_get_context( )->uid );
	}
	
	fi->fh = id;
	
	free( copy_path );

	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return res;
}


static int pgfuse_open( const char *path, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	PgMeta meta;
	int id;
	int res;
	PGconn *conn;

	if( data->verbose ) {
		char *s = flags_to_string( fi->flags );
		syslog( LOG_INFO, "Open '%s' on '%s' with flags '%s', thread #%d",
			path, data->mountpoint, s, fuse_get_context( )->uid );
		if( *s != '<' ) free( s );
	}

	ACQUIRE( conn );
	PSQL_BEGIN( conn );

	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	
	/* currently don't allow parallel access */
	if( meta.ref_count > 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ETXTBSY;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id for file '%s' to open is %d, thread #%d",
			path, id, fuse_get_context( )->uid );
	}
		
	if( S_ISDIR( meta.mode ) ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EISDIR;
	}
	
	if( data->read_only ) {
		if( ( fi->flags & O_ACCMODE ) != O_RDONLY ) {
			PSQL_ROLLBACK( conn ); RELEASE( conn );
			return -EROFS;
		}
	}
		
	meta.ref_count = 1;
	
	res = psql_write_meta( conn, id, path, meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}	
		
	fi->fh = id;

	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_opendir( const char *path, struct fuse_file_info *fi )
{
	/* nothing to do, everything is done in pgfuse_readdir currently */
	return 0;
}

static int pgfuse_readdir( const char *path, void *buf, fuse_fill_dir_t filler,
                           off_t offset, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	int res;
	PgMeta meta;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Readdir '%s' on '%s', thread #%d",
			path, data->mountpoint, fuse_get_context( )->uid );
	}
	
	ACQUIRE( conn );	
	PSQL_BEGIN( conn );
	
	filler( buf, ".", NULL, 0 );
	filler( buf, "..", NULL, 0 );
	
	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	
	res = psql_readdir( conn, id, buf, filler );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_releasedir( const char *path, struct fuse_file_info *fi )
{
	/* nothing to do, everything is done in pgfuse_readdir currently */
	return 0;
}

static int pgfuse_fsyncdir( const char *path, int datasync, struct fuse_file_info *fi )
{
	/* nothing to do, everything is done in pgfuse_readdir currently */
	return 0;
}

static int pgfuse_mkdir( const char *path, mode_t mode )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	char *copy_path;
	char *parent_path;
	char *new_dir;
	int parent_id;
	int res;
	PgMeta meta;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Mkdir '%s' in mode '%o' on '%s', thread #%d",
			path, (unsigned int)mode, data->mountpoint,
			fuse_get_context( )->uid );
	}

	ACQUIRE( conn );
	PSQL_BEGIN( conn );
	
	if( data->read_only ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EROFS;
	}
	
	copy_path = strdup( path );
	if( copy_path == NULL ) {
		syslog( LOG_ERR, "Out of memory in Mkdir '%s'!", path );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOMEM;
	}
	
	parent_path = dirname( copy_path );

	parent_id = psql_get_meta( conn, parent_path, &meta );
	if( parent_id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return parent_id;
	}
	if( !S_ISDIR( meta.mode ) ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOENT;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Parent_id for new dir '%s' is %d, thread #%d",
			path, parent_id, fuse_get_context( )->uid );
	}
	
	free( copy_path );
	copy_path = strdup( path );
	if( copy_path == NULL ) {
		free( parent_path );
		syslog( LOG_ERR, "Out of memory in Mkdir '%s'!", path );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOMEM;
	}
	
	new_dir = basename( copy_path );

	meta.size = 0;
	meta.mode = mode | S_IFDIR; /* S_IFDIR is not set by fuse */
	/* TODO: use FUSE context */
	meta.uid = geteuid( );
	meta.gid = getegid( );
	meta.ctime = now( );
	meta.mtime = meta.ctime;
	meta.atime = meta.ctime;
	
	res = psql_create_dir( conn, parent_id, path, new_dir, meta );
	if( res < 0 ) {
		free( copy_path );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}

	free( copy_path );

	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_rmdir( const char *path )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	int res;
	PgMeta meta;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Rmdir '%s' on '%s', thread #%d",
			path, data->mountpoint, fuse_get_context( )->uid );
	}

	ACQUIRE( conn );	
	PSQL_BEGIN( conn );
	
	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	if( !S_ISDIR( meta.mode ) ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOTDIR;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id of dir '%s' to be removed is %d, thread #%d",
			path, id, fuse_get_context( )->uid );
	}

	if( data->read_only ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EROFS;
	}
				
	res = psql_delete_dir( conn, id, path );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_unlink( const char *path )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	int res;
	PgMeta meta;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Remove file '%s' on '%s', thread #%d",
			path, data->mountpoint, fuse_get_context( )->uid );
	}
	
	ACQUIRE( conn );
	PSQL_BEGIN( conn );
	
	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	if( S_ISDIR( meta.mode ) ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EPERM;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id of file '%s' to be removed is %d, thread #%d",
			path, id, fuse_get_context( )->uid );
	}

	if( data->read_only ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EROFS;
	}

	/* currently don't allow parallel access */
	if( meta.ref_count > 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ETXTBSY;
	}
			
	res = psql_delete_file( conn, id, path );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_flush( const char *path, struct fuse_file_info *fi )
{
	/* nothing to do, data is always persistent in database */

	return 0;
}

static int pgfuse_fsync( const char *path, int isdatasync, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "%s on file '%s' on '%s', thread #%d",
			isdatasync ? "FDataSync" : "FSync", path, data->mountpoint,
			fuse_get_context( )->uid );
	}

	if( data->read_only ) {
		return -EROFS;
	}

	if( fi->fh == 0 ) {
		return -EBADF;
	}
	
	/* nothing to do, data is always persistent in database */
	
	/* TODO: if we have a per transaction/file transaction policy, we must change this here! */
	
	return 0;
}

static int pgfuse_release( const char *path, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	int res;
	PgMeta meta;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Releasing '%s' on '%s', thread #%d",
			path, data->mountpoint, fuse_get_context( )->uid );
	}

	ACQUIRE( conn );		
	PSQL_BEGIN( conn );

	if( fi->fh == 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EBADF;
	}

	if( data->read_only ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return 0;
	}
	
	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	
	meta.ref_count = 0;
	
	res = psql_write_meta( conn, id, path, meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}

	PSQL_COMMIT( conn ); RELEASE( conn );

	return 0;
}

static int pgfuse_write( const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int res;
	PgMeta meta;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Write to '%s' from offset %d, size %d on '%s', thread #%d",
			path, (unsigned int)offset, (unsigned int)size, data->mountpoint,
			fuse_get_context( )->uid );
	}

	ACQUIRE( conn );
	PSQL_BEGIN( conn );
	
	if( fi->fh == 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EBADF;
	}

	if( data->read_only ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EBADF;
	}
		
	res = psql_get_meta( conn, path, &meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	if( offset + size > meta.size ) {
		meta.size = offset + size;
	}
	
	res = psql_write_buf( conn, fi->fh, path, buf, offset, size, data->verbose );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	if( res != size ) {
		syslog( LOG_ERR, "Write size mismatch in file '%s' on mountpoint '%s', expected '%d' to be written, but actually wrote '%d' bytes! Data inconistency!",
			path, data->mountpoint, size, res );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EIO;
	}
	
	res = psql_write_meta( conn, fi->fh, path, meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}

	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return size;
}

static int pgfuse_read( const char *path, char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int res;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Read to '%s' from offset %d, size %d on '%s', thread #%d",
			path, (unsigned int)offset, (unsigned int)size, data->mountpoint,
			fuse_get_context( )->uid );
	}

	ACQUIRE( conn );
	PSQL_BEGIN( conn );

	if( fi->fh == 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EBADF;
	}

	res = psql_read_buf( conn, fi->fh, path, buf, offset, size, data->verbose );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}

	PSQL_COMMIT( conn ); RELEASE( conn );
		
	return size;
}

static int pgfuse_truncate( const char* path, off_t offset )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;
	int res;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Truncate of '%s' to size '%d' on '%s', thread #%d",
			path, (unsigned int)offset, data->mountpoint, fuse_get_context( )->uid );
	}

	ACQUIRE( conn );
	PSQL_BEGIN( conn );

	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	
	if( S_ISDIR( meta.mode ) ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EISDIR;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id of file '%s' to be truncated is %d, thread #%d",
			path, id, fuse_get_context( )->uid );
	}

	if( data->read_only ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EROFS;
	}

	res = psql_truncate( conn, id, path, offset );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	meta.size = offset;
	res = psql_write_meta( conn, id, path, meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_ftruncate( const char *path, off_t offset, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int res;
	PgMeta meta;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Truncate of '%s' to size '%d' on '%s', thread #%d",
			path, (unsigned int)offset, data->mountpoint,
			fuse_get_context( )->uid );
	}

	ACQUIRE( conn );
	PSQL_BEGIN( conn );

	if( fi->fh == 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EBADF;
	}

	if( data->read_only ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EROFS;
	}
	
	res = psql_get_meta( conn, path, &meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	res = psql_truncate( conn, fi->fh, path, offset );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	meta.size = offset;
	
	res = psql_write_meta( conn, fi->fh, path, meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}

	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_statfs( const char *path, struct statvfs *buf )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;

	if( data->verbose ) {
		syslog( LOG_INFO, "Statfs called on '%s', thread #%d",
			data->mountpoint, fuse_get_context( )->uid );
	}
	
	/* Note: f_frsize, f_favail, f_fsid and f_flag are currently ignored by FUSE */
	
	memset( buf, 0, sizeof( struct statvfs ) );
	
	buf->f_bsize = STANDARD_BLOCK_SIZE;
	buf->f_frsize = STANDARD_BLOCK_SIZE;
	/* Note: it's hard to tell how much space is left in the database
	 * and how big it is
	 */
	buf->f_blocks = INT_MAX;
	buf->f_bfree = INT_MAX;
	buf->f_bavail = INT_MAX;
	buf->f_files = INT_MAX;
	buf->f_ffree = INT_MAX;
	buf->f_favail = INT_MAX;
	if( data->read_only ) {
		buf->f_flag |= ST_RDONLY;
	}
	buf->f_namemax = MAX_FILENAME_LENGTH;
	
	return 0;
}

static int pgfuse_chmod( const char *path, mode_t mode )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;	
	int res;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Chmod on '%s' to mode '%o' on '%s', thread #%d",
			path, (unsigned int)mode, data->mountpoint,
			fuse_get_context( )->uid );
	}
	
	ACQUIRE( conn );
	PSQL_BEGIN( conn );
	
	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	
	meta.mode = mode;
	
	res = psql_write_meta( conn, id, path, meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}

	PSQL_COMMIT( conn ); RELEASE( conn );

	return 0;
}

static int pgfuse_chown( const char *path, uid_t uid, gid_t gid )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;	
	int res;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Chown on '%s' to uid '%d' and gid '%d' on '%s', thread #%d",
			path, (unsigned int)uid, (unsigned int)gid, data->mountpoint,
			fuse_get_context( )->uid );
	}
	
	ACQUIRE( conn );	
	PSQL_BEGIN( conn );
	
	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	
	meta.uid = uid;
	meta.gid = gid;
	
	res = psql_write_meta( conn, id, path, meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return res;
}

static int pgfuse_symlink( const char *from, const char *to )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	char *copy_to;
	char *parent_path;
	char *symlink;
	int parent_id;
	int res;
	int id;
	PgMeta meta;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Symlink from '%s' to '%s' on '%s', thread #%d",
			from, to, data->mountpoint, fuse_get_context( )->uid );
	}

	ACQUIRE( conn );
	PSQL_BEGIN( conn );
	
	if( data->read_only ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EROFS;
	}
	
	copy_to = strdup( to );
	if( copy_to == NULL ) {
		syslog( LOG_ERR, "Out of memory in Symlink '%s'!", to );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOMEM;
	}
	
	parent_path = dirname( copy_to );

	parent_id = psql_get_meta( conn, parent_path, &meta );
	if( parent_id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return parent_id;
	}
	if( !S_ISDIR( meta.mode ) ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOENT;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Parent_id for symlink '%s' is %d, thread #%d",
			to, parent_id, fuse_get_context( )->uid );
	}
	
	free( copy_to );
	copy_to = strdup( to );
	if( copy_to == NULL ) {
		free( parent_path );
		syslog( LOG_ERR, "Out of memory in Symlink '%s'!", to );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOMEM;
	}
	
	symlink = basename( copy_to );

	meta.size = strlen( from );	/* size = length of path */
	meta.mode = 0777 | S_IFLNK; 	/* symlinks have no modes per se */
	/* TODO: use FUSE context */
	meta.uid = geteuid( );
	meta.gid = getegid( );
	meta.ctime = now( );
	meta.mtime = meta.ctime;
	meta.atime = meta.ctime;
	
	res = psql_create_file( conn, parent_id, to, symlink, meta );
	if( res < 0 ) {
		free( copy_to );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	id = psql_get_meta( conn, to, &meta );
	if( id < 0 ) {
		free( copy_to );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}

	res = psql_write_buf( conn, id, to, from, 0, strlen( from ), data->verbose );
	if( res < 0 ) {
		free( copy_to );
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	if( res != strlen( from ) ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -EIO;
	}

	free( copy_to );
	
	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_readlink( const char *path, char *buf, size_t size )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;
	int res;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Dereferencing symlink '%s' on '%s', thread #%d",
			path, data->mountpoint, fuse_get_context( )->uid );
	}
	
	ACQUIRE( conn );	
	PSQL_BEGIN( conn );

	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
	if( !S_ISLNK( meta.mode ) ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOENT;
	}
	
	if( size < meta.size + 1 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return -ENOMEM;
	}
	
	res = psql_read_buf( conn, id, path, buf, 0, meta.size, data->verbose );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	buf[meta.size] = '\0';

	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}

static int pgfuse_utimens( const char *path, const struct timespec tv[2] )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;	
	int res;
	PGconn *conn;

	if( data->verbose ) {
		syslog( LOG_INFO, "Utimens on '%s' to access time '%d' and modification time '%d' on '%s', thread #%d",
			path, (unsigned int)tv[0].tv_sec, (unsigned int)tv[1].tv_sec, data->mountpoint,
			fuse_get_context( )->uid );
	}
	
	ACQUIRE( conn );
	PSQL_BEGIN( conn );
	
	id = psql_get_meta( conn, path, &meta );
	if( id < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return id;
	}
		
	meta.atime = tv[0];
	meta.mtime = tv[1];
	
	res = psql_write_meta( conn, id, path, meta );
	if( res < 0 ) {
		PSQL_ROLLBACK( conn ); RELEASE( conn );
		return res;
	}
	
	PSQL_COMMIT( conn ); RELEASE( conn );
	
	return 0;
}


static struct fuse_operations pgfuse_oper = {
	.getattr	= pgfuse_getattr,
	.readlink	= pgfuse_readlink,
	.mknod		= NULL,		/* not used, we use 'create' */
	.mkdir		= pgfuse_mkdir,
	.unlink		= pgfuse_unlink,
	.rmdir		= pgfuse_rmdir,
	.symlink	= pgfuse_symlink,
	.rename		= NULL,
	.link		= NULL,
	.chmod		= pgfuse_chmod,
	.chown		= pgfuse_chown,
	.utime		= NULL,		/* deprecated in favour of 'utimes' */
	.open		= pgfuse_open,
	.read		= pgfuse_read,
	.write		= pgfuse_write,
	.statfs		= pgfuse_statfs,
	.flush		= pgfuse_flush,
	.release	= pgfuse_release,
	.fsync		= pgfuse_fsync,
	.setxattr	= NULL,
	.listxattr	= NULL,
	.removexattr	= NULL,
	.opendir	= pgfuse_opendir,
	.readdir	= pgfuse_readdir,
	.releasedir	= pgfuse_releasedir,
	.fsyncdir	= pgfuse_fsyncdir,
	.init		= pgfuse_init,
	.destroy	= pgfuse_destroy,
	.access		= pgfuse_access,
	.create		= pgfuse_create,
	.truncate	= pgfuse_truncate,
	.ftruncate	= pgfuse_ftruncate,
	.fgetattr	= pgfuse_fgetattr,
	.lock		= NULL,
	.utimens	= pgfuse_utimens,
	.bmap		= NULL,
	.ioctl		= NULL,
	.poll		= NULL
};

/* --- parse arguments --- */

typedef struct PgFuseOptions {
	int print_help;		/* whether we should print a help page */
	int print_version;	/* whether we should print the version */
	int verbose;		/* whether we should be verbose */
	char *conninfo;		/* connection info as used in PQconnectdb */
	char *mountpoint;	/* where we mount the virtual filesystem */
	int read_only;		/* whether to mount read-only */
	int multi_threaded;	/* whether we run multi-threaded */
} PgFuseOptions;

#define PGFUSE_OPT( t, p, v ) { t, offsetof( PgFuseOptions, p ), v }

enum {
	KEY_HELP,
	KEY_VERBOSE,
	KEY_VERSION
};

static struct fuse_opt pgfuse_opts[] = {
	PGFUSE_OPT( 	"ro",		read_only, 1 ),
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
	PgFuseOptions *pgfuse = (PgFuseOptions *)data;

	switch( key ) {
		case FUSE_OPT_KEY_OPT:
			if( strcmp( arg, "-s" ) == 0 ) {
				pgfuse->multi_threaded = 0;
			}
			return 1;
		
		case FUSE_OPT_KEY_NONOPT:
			if( pgfuse->conninfo == NULL ) {
				pgfuse->conninfo = strdup( arg );
				return 0;
			} else if( pgfuse->mountpoint == NULL ) {
				pgfuse->mountpoint = strdup( arg );
				return 1;
			} else {
				fprintf( stderr, "%s, only two arguments allowed: Postgresql connection data and mountpoint\n", basename( outargs->argv[0] ) );
				return -1;
			}
			
		case KEY_HELP:
			pgfuse->print_help = 1;
			return -1;
		
		case KEY_VERBOSE:
			pgfuse->verbose = 1;
			return 0;
			
		case KEY_VERSION:
			pgfuse->print_version = 1;
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
		"    ro                     mount filesystem read-only, do not change data in database\n"
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
	PgFuseOptions pgfuse;
	PgFuseData userdata;
	const char *value;
	
	memset( &pgfuse, 0, sizeof( pgfuse ) );
	pgfuse.multi_threaded = 1;
	
	if( fuse_opt_parse( &args, &pgfuse, pgfuse_opts, pgfuse_opt_proc ) == -1 ) {
		if( pgfuse.print_help ) {
			/* print our options */
			print_usage( basename( argv[0] ) );
			fflush( stdout );
			/* print options of FUSE itself */
			argv[1] = "-ho";
			argv[2] = "mountpoint";
			(void)dup2( STDOUT_FILENO, STDERR_FILENO ); /* force fuse help to stdout */
			fuse_main( 2, argv, &pgfuse_oper, NULL);
			exit( EXIT_SUCCESS );
		}
		if( pgfuse.print_version ) {
			printf( "%s\n", PGFUSE_VERSION );
			exit( EXIT_SUCCESS );
		}
		exit( EXIT_FAILURE );
	}
		
	if( pgfuse.conninfo == NULL ) {
		fprintf( stderr, "Missing Postgresql connection data\n" );
		fprintf( stderr, "See '%s -h' for usage\n", basename( argv[0] ) );
		exit( EXIT_FAILURE );
	}
		
	/* just test if the connection can be established, do the
	 * real connection in the fuse init function!
	 */
	conn = PQconnectdb( pgfuse.conninfo );
	if( PQstatus( conn ) != CONNECTION_OK ) {
		fprintf( stderr, "Connection to database failed: %s",
			PQerrorMessage( conn ) );
		PQfinish( conn );
		exit( EXIT_FAILURE );
	}

	/* test storage of timestamps (expecting uint64 as it is the
	 * standard for PostgreSQL 8.4 or newer). Otherwise bail out
	 * currently..
	 */
	value = PQparameterStatus( conn, "integer_datetimes" );
	if( value == NULL ) {
		fprintf( stderr, "PQ param integer_datetimes not available?\n"
		         "You use a too old version of PostgreSQL..can't continue.\n" );
		PQfinish( conn );
		return 1;
	}
	
	/* Make sure we have the more modern uint64 representation for timestamps,
	 * bail out otherwise
	 */
	if( strcmp( value, "on" ) != 0 ) {
		fprintf( stderr, "Expecting UINT64 for timestamps, not doubles. You may use an old version of PostgreSQL (<8.4)\n"
		         "or PostgreSQL has been compiled with the deprecated compile option '--disable-integer-datetimes'\n" );
		PQfinish( conn );
		return 1;
	}
		
	PQfinish( conn );
	
	openlog( basename( argv[0] ), LOG_PID, LOG_USER );	
	
	memset( &userdata, 0, sizeof( PgFuseData ) );
	userdata.conninfo = pgfuse.conninfo;
	userdata.mountpoint = pgfuse.mountpoint;
	userdata.verbose = pgfuse.verbose;
	userdata.read_only = pgfuse.read_only;
	userdata.multi_threaded = pgfuse.multi_threaded;
	
	res = fuse_main( args.argc, args.argv, &pgfuse_oper, &userdata );
	
	exit( res );
}
