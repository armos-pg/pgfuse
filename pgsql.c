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

#include <syslog.h>		/* for ERR_XXX */
#include <errno.h>		/* for ENOENT and friends */
#include <arpa/inet.h>		/* for htonl, ntohl */

int psql_get_meta( PGconn *conn, const char *path, PgMeta *meta )
{
	PGresult *res;
	int i_id;
	int i_size;
	int i_isdir;
	char *iptr;
	char *s;
	int id;
	
	const char *values[1] = { path };
	int lengths[1] = { strlen( path ) };
	int binary[1] = { 1 };
	
	res = PQexecParams( conn, "SELECT id, size, isdir FROM dir WHERE path = $1::varchar",
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
	i_isdir = PQfnumber( res, "isdir" );
	iptr = PQgetvalue( res, 0, i_id );
	id = ntohl( *( (uint32_t *)iptr ) );
	iptr = PQgetvalue( res, 0, i_size );
	meta->size = ntohl( *( (uint32_t *)iptr ) );
	s = PQgetvalue( res, 0, i_isdir );
	meta->isdir = 0;
	if( s[0] == '\001' ) meta->isdir = 1;

	PQclear( res );
	
	return id;
}

int psql_create_file( PGconn *conn, const int parent_id, const char *path, const char *new_file, mode_t mode )
{
	int param1 = htonl( parent_id );
	const char *values[3] = { (char *)&param1, new_file, path };
	int lengths[3] = { sizeof( parent_id), strlen( new_file ), strlen( path ) };
	int binary[3] = { 1, 0, 0 };
	PGresult *res;
	
	res = PQexecParams( conn, "INSERT INTO dir( parent_id, name, path, isdir ) VALUES ($1::int4, $2::varchar, $3::varchar, false )",
		3, NULL, values, lengths, binary, 1 );

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

int psql_create_dir( PGconn *conn, const int parent_id, const char *path, const char *new_dir, mode_t mode )
{
	int param1 = htonl( parent_id );
	const char *values[3] = { (char *)&param1, new_dir, path };
	int lengths[3] = { sizeof( parent_id), strlen( new_dir ), strlen( path ) };
	int binary[3] = { 1, 0, 0 };
	PGresult *res;
	
	res = PQexecParams( conn, "INSERT INTO dir( parent_id, name, path, isdir ) VALUES ($1::int4, $2::varchar, $3::varchar, true )",
		3, NULL, values, lengths, binary, 1 );

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

int psql_write_meta( PGconn *conn, const int id, const char *path, PgMeta meta )
{
	int param1 = htonl( id );
	int param2 = htonl( meta.size );
	const char *values[2] = { (char *)&param1, (char *)&param2 };
	int lengths[2] = { sizeof( param1 ), sizeof( param2 ) };
	int binary[2] = { 1, 1 };
	PGresult *res;
	
	res = PQexecParams( conn, "UPDATE dir SET size=$2::int4 WHERE id=$1::int4",
		2, NULL, values, lengths, binary, 1 );

	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_write_meta for file '%s': %s", path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}

	PQclear( res );
	
	return 0;
}
