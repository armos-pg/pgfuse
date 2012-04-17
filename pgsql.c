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

#include "pgsql.h"

#include <string.h>		/* for strlen, memcpy, strcmp */
#include <stdlib.h>		/* for atoi */

#include <syslog.h>		/* for ERR_XXX */
#include <errno.h>		/* for ENOENT and friends */
#include <arpa/inet.h>		/* for htonl, ntohl */
#include <stdint.h>		/* for uint64_t */
#include <endian.h>		/* for be64toh (GNU/BSD-ism, but easy to port if necessary) */

/* --- helper functions --- */

/* January 1, 2000, 00:00:00 UTC (in Unix epoch seconds) */
#define POSTGRES_EPOCH_DATE 946684800

static uint64_t convert_to_timestamp( struct timespec t )
{
	return htobe64( ( (uint64_t)t.tv_sec - POSTGRES_EPOCH_DATE ) * 1000000 + t.tv_nsec / 1000 );
}

static struct timespec convert_from_timestamp( uint64_t raw )
{
	uint64_t t;
	struct timespec ts;
	
	t = be64toh( raw );
		
	ts.tv_sec = POSTGRES_EPOCH_DATE + t / 1000000;
	ts.tv_nsec = ( t % 1000000 ) * 1000;
	
	return ts;
}

/* --- postgresql implementation --- */

int psql_get_meta( PGconn *conn, const char *path, PgMeta *meta )
{
	PGresult *res;
	int idx;
	char *data;
	int id;
	
	const char *values[1] = { path };
	int lengths[1] = { strlen( path ) };
	int binary[1] = { 1 };
	
	res = PQexecParams( conn, "SELECT id, size, mode, uid, gid, ctime, mtime, atime FROM dir WHERE path = $1::varchar",
		1, NULL, values, lengths, binary, 1 );
	
	if( PQresultStatus( res ) != PGRES_TUPLES_OK ) {
		syslog( LOG_ERR, "Error in psql_get_meta for path '%s'", path );
		PQclear( res );
		return -EIO;
	}
	
	if( PQntuples( res ) == 0 ) {
		PQclear( res );
		return -ENOENT;
	}
	
	if( PQntuples( res ) > 1 ) {
		syslog( LOG_ERR, "Expecting exactly one inode for path '%s' in psql_get_meta, data inconsistent!", path );
		PQclear( res );
		return -EIO;
	}
	
	idx = PQfnumber( res, "id" );
	data = PQgetvalue( res, 0, idx );
	id = ntohl( *( (uint32_t *)data ) );

	idx = PQfnumber( res, "size" );
	data = PQgetvalue( res, 0, idx );
	meta->size = ntohl( *( (uint32_t *)data ) );
	
	idx = PQfnumber( res, "mode" );
	data = PQgetvalue( res, 0, idx );
	meta->mode = ntohl( *( (uint32_t *)data ) );

	idx = PQfnumber( res, "uid" );
	data = PQgetvalue( res, 0, idx );
	meta->uid = ntohl( *( (uint32_t *)data ) );

	idx = PQfnumber( res, "gid" );
	data = PQgetvalue( res, 0, idx );
	meta->gid = ntohl( *( (uint32_t *)data ) );
	
	idx = PQfnumber( res, "ctime" );
	data = PQgetvalue( res, 0, idx );
	meta->ctime = convert_from_timestamp( *( (uint64_t *)data ) );

	idx = PQfnumber( res, "mtime" );
	data = PQgetvalue( res, 0, idx );
	meta->mtime = convert_from_timestamp( *( (uint64_t *)data ) );

	idx = PQfnumber( res, "atime" );
	data = PQgetvalue( res, 0, idx );
	meta->atime = convert_from_timestamp( *( (uint64_t *)data ) );
	
	PQclear( res );
	
	return id;
}

int psql_write_meta( PGconn *conn, const int id, const char *path, PgMeta meta )
{
	int param1 = htonl( id );
	int param2 = htonl( meta.size );
	int param3 = htonl( meta.mode );
	int param4 = htonl( meta.uid );
	int param5 = htonl( meta.gid );
	uint64_t param6 = convert_to_timestamp( meta.ctime );
	uint64_t param7 = convert_to_timestamp( meta.mtime );
	uint64_t param8 = convert_to_timestamp( meta.atime );
	const char *values[8] = { (const char *)&param1, (const char *)&param2, (const char *)&param3, (const char *)&param4, (const char *)&param5, (const char *)&param6, (const char *)&param7, (const char *)&param8 };
	int lengths[8] = { sizeof( param1 ), sizeof( param2 ), sizeof( param3 ), sizeof( param4 ), sizeof( param5 ), sizeof( param6 ), sizeof( param7 ), sizeof( param8 ) };
	int binary[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
	PGresult *res;
	
	res = PQexecParams( conn, "UPDATE dir SET size=$2::int4, mode=$3::int4, uid=$4::int4, gid=$5::int4, ctime=$6::timestamp, mtime=$7::timestamp, atime=$8::timestamp WHERE id=$1::int4",
		8, NULL, values, lengths, binary, 1 );

	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_write_meta for file '%s': %s", path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}

	PQclear( res );
	
	return 0;
}

int psql_create_file( PGconn *conn, const int parent_id, const char *path, const char *new_file, PgMeta meta )
{
	int param1 = htonl( parent_id );
	int param2 = htonl( meta.size );
	int param3 = htonl( meta.mode );
	int param4 = htonl( meta.uid );
	int param5 = htonl( meta.gid );
	uint64_t param6 = convert_to_timestamp( meta.ctime );
	uint64_t param7 = convert_to_timestamp( meta.mtime );
	uint64_t param8 = convert_to_timestamp( meta.atime );
	const char *values[10] = { (const char *)&param1, new_file, path, (const char *)&param2, (const char *)&param3, (const char *)&param4, (const char *)&param5, (const char *)&param6, (const char *)&param7, (const char *)&param8 };
	int lengths[10] = { sizeof( param1 ), strlen( new_file ), strlen( path ), sizeof( param2 ), sizeof( param3 ), sizeof( param4 ), sizeof( param5 ), sizeof( param6 ), sizeof( param7 ), sizeof( param8 ) };
	int binary[10] = { 1, 0, 0, 1, 1, 1, 1, 1, 1, 1 };
	PGresult *res;
	
	res = PQexecParams( conn, "INSERT INTO dir( parent_id, name, path, size, mode, uid, gid, ctime, mtime, atime ) VALUES ($1::int4, $2::varchar, $3::varchar, $4::int4, $5::int4, $6::int4, $7::int4, $8::timestamp, $9::timestamp, $10::timestamp )",
		10, NULL, values, lengths, binary, 1 );

	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_create_file for path '%s': %s",
			path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}
	
	PQclear( res );
	
	return 0;
}

int psql_read_buf( PGconn *conn, const int id, const char *path, char **buf, const size_t len )
{
	int param1 = htonl( id );
	const char *values[1] = { (char *)&param1 };
	int lengths[2] = { sizeof( param1 ) };
	int binary[2] = { 1, 1 };
	PGresult *res;
	int i_data;
	int read;
	
	res = PQexecParams( conn, "SELECT data FROM DATA WHERE id=$1::int4",
		1, NULL, values, lengths, binary, 1 );
	
	if( PQresultStatus( res ) != PGRES_TUPLES_OK ) {
		syslog( LOG_ERR, "Error in psql_read_buf for path '%s'", path );
		PQclear( res );
		return -EIO;
	}
	
	if( PQntuples( res ) != 1 ) {
		syslog( LOG_ERR, "Expecting exactly one data entry for path '%s' in psql_read_buf, data inconsistent!", path );
		PQclear( res );
		return -EIO;
	}
	
	i_data = PQfnumber( res, "data" );
	read = PQgetlength( res, 0, i_data );
	memcpy( *buf, PQgetvalue( res, 0, i_data ), read );

	PQclear( res );
	
	return read;
}

int psql_readdir( PGconn *conn, const int parent_id, void *buf, fuse_fill_dir_t filler )
{
	int param1 = htonl( parent_id );
	const char *values[1] = { (char *)&param1 };
	int lengths[1] = { sizeof( param1 ) };
	int binary[1] = { 1 };
	PGresult *res;
	int i_name;
	int i;
	char *name;
	
	res = PQexecParams( conn, "SELECT name FROM dir WHERE parent_id = $1::int4",
		1, NULL, values, lengths, binary, 1 );
	
	if( PQresultStatus( res ) != PGRES_TUPLES_OK ) {
		syslog( LOG_ERR, "Error in psql_readdir for dir with id '%d': %s",
			parent_id, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}
	
	i_name = PQfnumber( res, "name" );
	for( i = 0; i < PQntuples( res ); i++ ) {
		name = PQgetvalue( res, i, i_name );
		if( strcmp( name, "/" ) == 0 ) continue;
		filler( buf, name, NULL, 0 );
        }
        
	PQclear( res );
        	
	return 0;
}

int psql_create_dir( PGconn *conn, const int parent_id, const char *path, const char *new_dir, PgMeta meta )
{
	int param1 = htonl( parent_id );
	int param2 = htonl( meta.mode );
	int param3 = htonl( meta.uid );
	int param4 = htonl( meta.gid );
	uint64_t param5 = convert_to_timestamp( meta.ctime );
	uint64_t param6 = convert_to_timestamp( meta.mtime );
	uint64_t param7 = convert_to_timestamp( meta.atime );
	const char *values[9] = { (const char *)&param1, new_dir, path, (const char *)&param2, (const char *)&param3, (const char *)&param4, (const char *)&param5, (const char *)&param6, (const char *)&param7 };
	int lengths[9] = { sizeof( param1 ), strlen( new_dir ), strlen( path ), sizeof( param2 ), sizeof( param3 ), sizeof( param4 ), sizeof( param5 ), sizeof( param6 ), sizeof( param7 ) };
	int binary[9] = { 1, 0, 0, 1, 1, 1, 1, 1, 1 };
	PGresult *res;
	
	res = PQexecParams( conn, "INSERT INTO dir( parent_id, name, path, mode, uid, gid, ctime, mtime, atime ) VALUES ($1::int4, $2::varchar, $3::varchar, $4::int4, $5::int4, $6::int4, $7::timestamp, $8::timestamp, $9::timestamp )",
		9, NULL, values, lengths, binary, 1 );

	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_create_dir for path '%s': %s", path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}
	
	PQclear( res );
	
	return 0;
}

int psql_delete_dir( PGconn *conn, const int id, const char *path )
{
	int param1 = htonl( id );
	const char *values[1] = { (char *)&param1 };
	int lengths[1] = { sizeof( param1 ) };
	int binary[1] = { 1 };
	PGresult *res;
	char *iptr;
	int count;
	
	res = PQexecParams( conn, "SELECT COUNT(*) FROM dir where parent_id=$1::int4",
		1, NULL, values, lengths, binary, 0 );
		
	if( PQresultStatus( res ) != PGRES_TUPLES_OK ) {
		syslog( LOG_ERR, "Error in psql_delete_dir for path '%s': %s", path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}

	if( PQntuples( res ) != 1 ) {
		syslog( LOG_ERR, "Expecting COUNT(*) to return 1 tupe, weird!" );
		PQclear( res );
		return -EIO;
	}

	iptr = PQgetvalue( res, 0, 0 );
	count = atoi( iptr );
	
	if( count > 0 ) {
		PQclear( res );
		return -ENOTEMPTY;
	}

	PQclear( res );
		
	res = PQexecParams( conn, "DELETE FROM dir where id=$1::int4",
		1, NULL, values, lengths, binary, 1 );

	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_delete_dir for path '%s': %s", path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}
	
	PQclear( res );
	
	return 0;
}

int psql_delete_file( PGconn *conn, const int id, const char *path )
{
	int param1 = htonl( id );
	const char *values[1] = { (char *)&param1 };
	int lengths[1] = { sizeof( param1 ) };
	int binary[1] = { 1 };
	PGresult *res;
	
	res = PQexecParams( conn, "DELETE FROM dir where id=$1::int4",
		1, NULL, values, lengths, binary, 1 );

	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_delete_dir for path '%s': %s", path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}
	
	PQclear( res );
	
	return 0;
}

int psql_write_buf( PGconn *conn, const int id, const char *path, const char *buf, const size_t len )
{
	int param1 = htonl( id );
	const char *values[2] = { (char *)&param1, buf };
	int lengths[2] = { sizeof( param1 ), len };
	int binary[2] = { 1, 1 };
	PGresult *res;
	
	res = PQexecParams( conn, "UPDATE data SET data=$2::bytea WHERE id=$1::int4",
		2, NULL, values, lengths, binary, 1 );

	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_write_buf for file '%s': %s", path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}
	
	PQclear( res );
	
	return len;
}
