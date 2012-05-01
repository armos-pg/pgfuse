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

#ifndef PGSQL_H
#define PGSQL_H

#include <sys/types.h>		/* size_t */
#include <sys/time.h>		/* for struct timespec */
#include <sys/stat.h>		/* mode_t */
#include <stdint.h>		/* for uint64_t */

#include <fuse.h>		/* for user-land filesystem */

#include <libpq-fe.h>		/* for Postgresql database access */

/* --- metadata stored about a file/directory/synlink --- */

typedef struct PgMeta {
	int64_t size;		/* the size of the file (naturally the bigint on PostgreSQL) */
	mode_t mode;		/* type and permissions of file/directory */
	uid_t uid;		/* owner of the file/directory */
	gid_t gid;		/* group owner of the file/directory */
	struct timespec ctime;	/* last status change time */
	struct timespec mtime;	/* last modification time */
	struct timespec atime;	/* last access time */
	int64_t parent_id;		/* id/inode_no of parenting directory */
} PgMeta;

/* --- transaction management and policies --- */
#define PSQL_BEGIN( T ) \
	{ \
		int __res; \
		__res = psql_begin( T ); \
		if( __res < 0 ) return __res; \
	}

#define PSQL_COMMIT( T ) \
	{ \
		int __res; \
		__res = psql_commit( T ); \
		if( __res < 0 ) return __res; \
	}

#define PSQL_ROLLBACK( T ) \
	{ \
		int __res; \
		__res = psql_rollback( T ); \
		if( __res < 0 ) return __res; \
	}

int psql_begin( PGconn *conn );

int psql_commit( PGconn *conn );

int psql_rollback( PGconn *conn );

/* --- the filesystem functions --- */

int64_t psql_path_to_id( PGconn *conn, const char *path );

int64_t psql_read_meta( PGconn *conn, const int64_t id, const char *path, PgMeta *meta );

int64_t psql_read_meta_from_path( PGconn *conn, const char *path, PgMeta *meta );

int psql_write_meta( PGconn *conn, const int64_t id, const char *path, PgMeta meta );

int psql_create_file( PGconn *conn, const int64_t parent_id, const char *path, const char *new_file, PgMeta meta );

int psql_read_buf( PGconn *conn, const int64_t id, const char *path, char *buf, const off_t offset, const size_t len, int verbose );

int psql_readdir( PGconn *conn, const int64_t parent_id, void *buf, fuse_fill_dir_t filler );

int psql_create_dir( PGconn *conn, const int64_t parent_id, const char *path, const char *new_dir, PgMeta meta );

int psql_delete_dir( PGconn *conn, const int64_t id, const char *path );

int psql_delete_file( PGconn *conn, const int64_t id, const char *path );

int psql_write_buf( PGconn *conn, const int64_t id, const char *path, const char *buf, const off_t offset, const size_t len, int verbose );

int psql_truncate( PGconn *conn, const int64_t id, const char *path, const off_t offset );

int psql_rename( PGconn *conn, const int64_t from_id, const int64_t from_parent_id, const int64_t to_parent_id, const char *rename_to, const char *from, const char *to );

#endif
