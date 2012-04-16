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

/* --- internal file handles */

typedef struct PgFuseFile {
	int id;			/* id as in database, also passed in FUSE context */
	char *buf;		/* buffer containing data */
	size_t size;		/* current size of the buffer (malloc/realloc) */
	size_t used;		/* used size in the buffer */
	int ref_count;		/* reference counter (for double opens, dup, etc.) */
} PgFuseFile;

static PgFuseFile pgfuse_files[MAX_NOF_OPEN_FILES];

/* --- fuse callbacks --- */

typedef struct PgFuseData {
	int verbose;		/* whether we should be verbose */
	char *conninfo;		/* connection info as used in PQconnectdb */
	char *mountpoint;	/* where we mount the virtual filesystem */
	PGconn *conn;		/* the database handle to operate on */
	int read_only;		/* whether the mount point is read-only */
} PgFuseData;

static void *pgfuse_init( struct fuse_conn_info *conn )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	
	syslog( LOG_INFO, "Mounting file system on '%s' ('%s', %s)",
		data->mountpoint, data->conninfo,
		data->read_only ? "read-only" : "read-write" );
	
	memset( pgfuse_files, 0, sizeof( PgFuseFile ) * MAX_NOF_OPEN_FILES );

	data->conn = PQconnectdb( data->conninfo );
	if( PQstatus( data->conn ) != CONNECTION_OK ) {
		syslog( LOG_ERR, "Connection to database failed: %s",
			PQerrorMessage( data->conn ) );
		PQfinish( data->conn );
	}
	
	return data;
}

static void pgfuse_destroy( void *userdata )
{
	PgFuseData *data = (PgFuseData *)userdata;

	syslog( LOG_INFO, "Unmounting file system on '%s' (%s)",
		data->mountpoint, data->conninfo );
	
	PQfinish( data->conn );
}


static int pgfuse_getattr( const char *path, struct stat *stbuf )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "GetAttrs '%s' on '%s'", path, data->mountpoint );
	}
	
	memset( stbuf, 0, sizeof( struct stat ) );

	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 ) {
		return id;
	}

	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id for %s '%s' is %d",
			S_ISDIR( meta.mode ) ? "dir" : "file", path, id );
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
	
	return 0;
}

static int pgfuse_access( const char *path, int mode )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;

	if( data->verbose ) {
		syslog( LOG_INFO, "Access on '%s' and mode '%o", path, (unsigned int)mode );
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
	PgFuseFile *f;
		
	if( data->verbose ) {
		char *s = flags_to_string( fi->flags );
		syslog( LOG_INFO, "Create '%s' in mode '%o' on '%s' with flags '%s'", path, mode, data->mountpoint, s );
		if( *s != '<' ) free( s );
	}
	
	if( data->read_only ) {
		return -EROFS;
	}
	
	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 && id != -ENOENT ) {
		return id;
	}
	
	if( id >= 0 ) {
		if( data->verbose ) {
			syslog( LOG_DEBUG, "Id for dir '%s' is %d", path, id );
		}
		
		if( S_ISDIR(meta.mode ) ) {
			return -EISDIR;
		}
		
		return -EEXIST;
	}
	
	copy_path = strdup( path );
	if( copy_path == NULL ) {
		syslog( LOG_ERR, "Out of memory in Create '%s'!", path );
		return -ENOMEM;
	}
	
	parent_path = dirname( copy_path );

	parent_id = psql_get_meta( data->conn, parent_path, &meta );
	if( parent_id < 0 ) {
		return parent_id;
	}
	if( !S_ISDIR(meta.mode ) ) {
		return -ENOENT;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Parent_id for new file '%s' in dir '%s' is %d", path, parent_path, parent_id );
	}
	
	free( copy_path );
	copy_path = strdup( path );
	if( copy_path == NULL ) {
		free( parent_path );
		syslog( LOG_ERR, "Out of memory in Create '%s'!", path );
		return -ENOMEM;
	}
	
	new_file = basename( copy_path );
	
	meta.size = 0;
	meta.mode = mode;
	/* TODO: use FUSE context */
	meta.uid = geteuid( );
	meta.gid = getegid( );
	
	res = psql_create_file( data->conn, parent_id, path, new_file, meta );
	if( res < 0 ) {
		free( copy_path );
		return res;
	}
	
	/* get id and store it, remember it in the hash of open files
	 * the hash function is currently the inode (i.e. the serial
	 * in the 'id' field module hashtable size, avoiding a much
	 * more complicated implementation for no good here
	 */
	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 ) {
		free( copy_path );
		return res;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id for new file '%s' is %d", path, id );
	}
		
	if( pgfuse_files[id % MAX_NOF_OPEN_FILES].id != 0 ) {
		free( copy_path );
		return -EMFILE;
	}
	
	f = &pgfuse_files[id % MAX_NOF_OPEN_FILES];
	f->id = id;
	f->size = STANDARD_BLOCK_SIZE;
	f->used = 0;
	f->buf = (char *)malloc( f->size );
	if( f->buf == NULL ) {
		f->id = 0;
		free( copy_path );
		return -ENOMEM;
	}
	f->ref_count = 1;

	fi->fh = id;
	
	free( copy_path );
	
	return res;
}


static int pgfuse_open( const char *path, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	PgMeta meta;
	int id;
	PgFuseFile *f;
	int res;

	if( data->verbose ) {
		char *s = flags_to_string( fi->flags );
		syslog( LOG_INFO, "Open '%s' on '%s' with flags '%s'", path, data->mountpoint, s );
		if( *s != '<' ) free( s );
	}

	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 ) {
		return id;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id for file '%s' to open is %d", path, id );
	}
		
	if( S_ISDIR( meta.mode ) ) {
		return -EISDIR;
	}
	
	if( data->read_only ) {
		if( ( fi->flags & O_ACCMODE ) != O_RDONLY ) {
			return -EROFS;
		}
	}
	
	if( meta.size > MAX_FILE_SIZE ) {
		return -EFBIG;
	}			
	
	f = &pgfuse_files[id % MAX_NOF_OPEN_FILES];
	
	if( f->id != 0 ) {
		return -EMFILE;
	}
	
	f->id = id;
	f->size = ( ( meta.size / STANDARD_BLOCK_SIZE ) + 1 ) * STANDARD_BLOCK_SIZE;
	f->used = meta.size;
	f->buf = (char *)malloc( f->size );
	if( f->buf == NULL ) {
		f->id = 0;
		return -ENOMEM;
	}
	f->ref_count = 1;
	
	res = psql_read_buf( data->conn, id, path, &f->buf, f->used );
	if( res != f->used ) {
		syslog( LOG_ERR, "Possible data corruption in file '%s', expected '%d' bytes, got '%d', on mountpoint '%s'!",
			path, (unsigned int)f->used, res, data->mountpoint );
		free( f->buf );
		f->id = 0;
		return -EIO;
	}

	fi->fh = id;
	
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
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Readdir '%s' on '%s'", path, data->mountpoint  );
	}
	
	filler( buf, ".", NULL, 0 );
	filler( buf, "..", NULL, 0 );
	
	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 ) {
		return id;
	}
	
	res = psql_readdir( data->conn, id, buf, filler );
	
	return res;
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
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Mkdir '%s' in mode '%o' on '%s'",
			path, (unsigned int)mode, data->mountpoint  );
	}

	if( data->read_only ) {
		return -EROFS;
	}
	
	copy_path = strdup( path );
	if( copy_path == NULL ) {
		syslog( LOG_ERR, "Out of memory in Mkdir '%s'!", path );
		return -ENOMEM;
	}
	
	parent_path = dirname( copy_path );

	parent_id = psql_get_meta( data->conn, parent_path, &meta );
	if( parent_id < 0 ) {
		return parent_id;
	}
	if( !S_ISDIR( meta.mode ) ) {
		return -ENOENT;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Parent_id for new dir '%s' is %d", path, parent_id );
	}
	
	free( copy_path );
	copy_path = strdup( path );
	if( copy_path == NULL ) {
		free( parent_path );
		syslog( LOG_ERR, "Out of memory in Mkdir '%s'!", path );
		return -ENOMEM;
	}
	
	new_dir = basename( copy_path );

	meta.size = 0;
	meta.mode = mode | S_IFDIR; /* S_IFDIR is not set by fuse */
	/* TODO: use FUSE context */
	meta.uid = geteuid( );
	meta.gid = getegid( );
	
	res = psql_create_dir( data->conn, parent_id, path, new_dir, meta );

	free( copy_path );
	
	return res;
}

static int pgfuse_rmdir( const char *path )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	int res;
	PgMeta meta;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Rmdir '%s' on '%s'", path, data->mountpoint  );
	}

	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 ) {
		return id;
	}
	if( !S_ISDIR( meta.mode ) ) {
		return -ENOTDIR;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id of dir '%s' to be removed is %d", path, id );
	}

	if( data->read_only ) {
		return -EROFS;
	}
				
	res = psql_delete_dir( data->conn, id, path );
	
	return res;
}

static int pgfuse_unlink( const char *path )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	int res;
	PgMeta meta;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Remove file '%s' on '%s'", path, data->mountpoint  );
	}
	
	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 ) {
		return id;
	}
	if( S_ISDIR( meta.mode ) ) {
		return -EPERM;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id of file '%s' to be removed is %d", path, id );
	}

	if( data->read_only ) {
		return -EROFS;
	}
			
	res = psql_delete_file( data->conn, id, path );
	
	return res;
}

static int pgfuse_flush( const char *path, struct fuse_file_info *fi )
{
	/* nothing to do currently as the temporary file buffer holds
	 * all the content in memory */
	return 0;
}

static int pgfuse_fsync( const char *path, int isdatasync, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	PgFuseFile *f;
	int res;
	PgMeta meta;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "%s on file '%s' on '%s'",
			isdatasync ? "FDataSync" : "FSync", path, data->mountpoint  );
	}

	if( data->read_only ) {
		return -EROFS;
	}

	if( fi->fh == 0 ) {
		return -EBADF;
	}

	f = &pgfuse_files[fi->fh % MAX_NOF_OPEN_FILES];
	
	
	if( !isdatasync ) {
		res = psql_get_meta( data->conn, path, &meta );
		if( res < 0 ) {
			return res;
		}
		meta.size = f->used;
		res = psql_write_meta( data->conn, f->id, path, meta );
	}
	
	if( res >= 0 ) {
		res = psql_write_buf( data->conn, f->id, path, f->buf, f->used );
	}
	
	if( res < 0 ) {
		return res;
	}
	
	return 0;
}

static int pgfuse_release( const char *path, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	PgFuseFile *f;
	int res;
	PgMeta meta;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Releasing '%s' on '%s'",
			path, data->mountpoint  );
	}

	if( fi->fh == 0 ) {
		return -EBADF;
	}

	f = &pgfuse_files[fi->fh % MAX_NOF_OPEN_FILES];
	
	f->ref_count--;
	if( f->ref_count > 0 ) {
		return 0;
	}

	if( data->read_only ) {
		f->id = 0;
		f->size = 0;
		free( f->buf );
		f->used = 0;
		
		return 0;
	}
	
	res = psql_get_meta( data->conn, path, &meta );
	if( res < 0 ) {
		syslog( LOG_CRIT, "Error reading metadata while closing the file '%s' on '%s'! Metadata may be corrupt now!",
			path, data->mountpoint );
		memset( &meta, 0, sizeof( meta ) );
	}
	
	meta.size = f->used;
	res = psql_write_meta( data->conn, f->id, path, meta );
	if( res >= 0 ) {
		res = psql_write_buf( data->conn, f->id, path, f->buf, f->used );
	}
	
	f->id = 0;
	f->size = 0;
	free( f->buf );
	f->used = 0;

	return res;
}

static int pgfuse_write( const char *path, const char *buf, size_t size,
                         off_t offset, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	PgFuseFile *f;

	if( data->verbose ) {
		syslog( LOG_INFO, "Write to '%s' from offset %d, size %d on '%s'",
			path, (unsigned int)offset, (unsigned int)size, data->mountpoint  );
	}

	if( fi->fh == 0 ) {
		return -EBADF;
	}

	if( data->read_only ) {
		return -EBADF;
	}
	
	f = &pgfuse_files[fi->fh % MAX_NOF_OPEN_FILES];
	
	if( offset + size > f->size ) {
		f->size = ( ( ( offset + size ) / STANDARD_BLOCK_SIZE ) + 1 ) * STANDARD_BLOCK_SIZE;
		if( f->size > MAX_FILE_SIZE ) {
			f->id = 0;
			free( f->buf );
			fi->fh = 0;
			return -EFBIG;
		}			
		f->buf = (char *)realloc( f->buf, f->size );
		if( f->buf == NULL ) {
			f->id = 0;
			fi->fh = 0;
			return -ENOMEM;
		}
	}
	
	memcpy( f->buf+offset, buf, size );
	if( offset + size > f->used ) {
		f->used = offset + size;
	}
	
	return size;
}

static int pgfuse_read( const char *path, char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	PgFuseFile *f;

	if( data->verbose ) {
		syslog( LOG_INFO, "Read to '%s' from offset %d, size %d on '%s'",
			path, (unsigned int)offset, (unsigned int)size, data->mountpoint  );
	}

	if( fi->fh == 0 ) {
		return -EBADF;
	}

	f = &pgfuse_files[fi->fh % MAX_NOF_OPEN_FILES];
	
	if( offset + size > f->used ) {
		size = f->used - offset;
	}
	
	memcpy( buf, f->buf+offset, size );
	
	return size;
}

static int pgfuse_truncate( const char* path, off_t offset )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;
	PgFuseFile *f;

	if( data->verbose ) {
		syslog( LOG_INFO, "Truncate of '%s' to size '%d' on '%s'",
			path, (unsigned int)offset, data->mountpoint  );
	}

	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 ) {
		return id;
	}
	
	if( S_ISDIR( meta.mode ) ) {
		return -EISDIR;
	}
	
	if( data->verbose ) {
		syslog( LOG_DEBUG, "Id of file '%s' to be truncated is %d", path, id );
	}

	if( data->read_only ) {
		return -EROFS;
	}

	f = &pgfuse_files[id % MAX_NOF_OPEN_FILES];

	if( f->id == 0 ) {
		return -EBADF;
	}

	f->used = offset;
	
	return 0;
}

static int pgfuse_ftruncate( const char *path, off_t offset, struct fuse_file_info *fi )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	PgFuseFile *f;

	if( data->verbose ) {
		syslog( LOG_INFO, "Truncate of '%s' to size '%d' on '%s'",
			path, (unsigned int)offset, data->mountpoint  );
	}

	if( fi->fh == 0 ) {
		return -EBADF;
	}

	if( data->read_only ) {
		return -EROFS;
	}
	
	f = &pgfuse_files[fi->fh % MAX_NOF_OPEN_FILES];
	
	f->used = offset;
	
	return 0;
}

static int pgfuse_utimens( const char *path, const struct timespec tv[2] )
{
	/* TODO: write tv to 'inode' as atime and mtime */

	return 0;
}

static int pgfuse_statfs( const char *path, struct statvfs *buf )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;

	if( data->verbose ) {
		syslog( LOG_INFO, "Statfs called on '%s'",
			data->mountpoint  );
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
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Chmod on '%s' to mode '%o' on '%s'",
			path, (unsigned int)mode, data->mountpoint  );
	}
	
	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 ) {
		return id;
	}
	
	meta.mode = mode;
	
	res = psql_write_meta( data->conn, id, path, meta );

	return res;
}

static int pgfuse_chown( const char *path, uid_t uid, gid_t gid )
{
	PgFuseData *data = (PgFuseData *)fuse_get_context( )->private_data;
	int id;
	PgMeta meta;	
	int res;
	
	if( data->verbose ) {
		syslog( LOG_INFO, "Chown on '%s' to uid '%d' and gid '%d' on '%s'",
			path, (unsigned int)uid, (unsigned int)gid, data->mountpoint  );
	}
	
	id = psql_get_meta( data->conn, path, &meta );
	if( id < 0 ) {
		return id;
	}
	
	meta.uid = uid;
	meta.gid = gid;
	
	res = psql_write_meta( data->conn, id, path, meta );

	return res;
}

static struct fuse_operations pgfuse_oper = {
	.getattr	= pgfuse_getattr,
	.readlink	= NULL,
	.mknod		= NULL,
	.mkdir		= pgfuse_mkdir,
	.unlink		= pgfuse_unlink,
	.rmdir		= pgfuse_rmdir,
	.symlink	= NULL,
	.rename		= NULL,
	.link		= NULL,
	.chmod		= pgfuse_chmod,
	.chown		= pgfuse_chown,
	.utime		= NULL,		/* deprecated in favour of utimes */
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
	.fgetattr	= NULL,
	.lock		= NULL,
	.utimens	= pgfuse_utimens,
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

#define PGFUSE_OPT( t, p, v ) { t, offsetof( PgFuse, p ), v }

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
	PgFuse *pgfuse = (PgFuse *)data;

	switch( key ) {
		case FUSE_OPT_KEY_OPT:
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
	PgFuse pgfuse;
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
	PQfinish( conn );
	
	openlog( basename( argv[0] ), LOG_PID, LOG_USER );	
	
	memset( &userdata, 0, sizeof( PgFuseData ) );
	userdata.conninfo = pgfuse.conninfo;
	userdata.mountpoint = pgfuse.mountpoint;
	userdata.verbose = pgfuse.verbose;
	userdata.read_only = pgfuse.read_only;
	
	res = fuse_main( args.argc, args.argv, &pgfuse_oper, &userdata );
	
	exit( res );
}
