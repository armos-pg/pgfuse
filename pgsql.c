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

int psql_get_meta( PGconn *conn, const char *path, PgMeta *meta )
{
	PGresult *res;
	int i_id;
	int i_size;
	int i_mode;
	char *iptr;
	int id;
	
	const char *values[1] = { path };
	int lengths[1] = { strlen( path ) };
	int binary[1] = { 1 };
	
	res = PQexecParams( conn, "SELECT id, size, mode FROM dir WHERE path = $1::varchar",
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
	
	i_id = PQfnumber( res, "id" );
	i_size = PQfnumber( res, "size" );
	i_mode = PQfnumber( res, "mode" );
	iptr = PQgetvalue( res, 0, i_id );
	id = ntohl( *( (uint32_t *)iptr ) );
	iptr = PQgetvalue( res, 0, i_size );
	meta->size = ntohl( *( (uint32_t *)iptr ) );
	iptr = PQgetvalue( res, 0, i_mode );
	meta->mode = ntohl( *( (uint32_t *)iptr ) );

	PQclear( res );
	
	return id;
}

int psql_write_meta( PGconn *conn, const int id, const char *path, PgMeta meta )
{
	int param1 = htonl( id );
	int param2 = htonl( meta.size );
	int param3 = htonl( meta.mode );
	const char *values[3] = { (char *)&param1, (char *)&param2, (char *)&param3 };
	int lengths[3] = { sizeof( param1 ), sizeof( param2 ), sizeof( param3 ) };
	int binary[3] = { 1, 1, 1 };
	PGresult *res;
	
	res = PQexecParams( conn, "UPDATE dir SET size=$2::int4, mode=$3::int4 WHERE id=$1::int4",
		3, NULL, values, lengths, binary, 1 );

	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_write_meta for file '%s': %s", path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}

	PQclear( res );
	
	return 0;
}

int psql_create_file( PGconn *conn, const int parent_id, const char *path, const char *new_file, const mode_t mode )
{
	int param1 = htonl( parent_id );
	int param2 = htonl( mode );
	const char *values[4] = { (const char *)&param1, new_file, path, (const char *)&param2 };
	int lengths[4] = { sizeof( param1 ), strlen( new_file ), strlen( path ), sizeof( param2 ) };
	int binary[4] = { 1, 0, 0, 1 };
	PGresult *res;
	
	res = PQexecParams( conn, "INSERT INTO dir( parent_id, name, path, mode ) VALUES ($1::int4, $2::varchar, $3::varchar, $4::int4 )",
		4, NULL, values, lengths, binary, 1 );

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

int psql_create_dir( PGconn *conn, const int parent_id, const char *path, const char *new_dir, const mode_t mode )
{
	int param1 = htonl( parent_id );
	int param2 = htonl( mode );
	const char *values[4] = { (char *)&param1, new_dir, path, (char *)&param2 };
	int lengths[4] = { sizeof( param1 ), strlen( new_dir ), strlen( path ), sizeof( param2 ) };
	int binary[4] = { 1, 0, 0, 1 };
	PGresult *res;
	
	res = PQexecParams( conn, "INSERT INTO dir( parent_id, name, path, mode ) VALUES ($1::int4, $2::varchar, $3::varchar, $4::int4 )",
		4, NULL, values, lengths, binary, 1 );

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
