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

#include "config.h"		/* compiled in defaults */

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

/* block information for read/write/truncate */
typedef struct PgDataInfo {
	unsigned int from_block;
	off_t from_offset;
	size_t from_len;
	unsigned int to_block;
	size_t to_len;
} PgDataInfo;

static PgDataInfo compute_block_info( off_t offset, size_t len )
{
	PgDataInfo info;
	int nof_blocks;
	
	info.from_block = offset / STANDARD_BLOCK_SIZE;
	info.from_offset = offset % STANDARD_BLOCK_SIZE;
	
	nof_blocks = ( info.from_offset + len ) / STANDARD_BLOCK_SIZE;
	if( nof_blocks == 0 ) {
		info.from_len = len;
	} else {
		info.from_len = STANDARD_BLOCK_SIZE - info.from_offset;
	}
	
	info.to_block = info.from_block + nof_blocks;
	info.to_len = ( info.from_offset + len ) % STANDARD_BLOCK_SIZE;
	
	if( info.to_len == 0 ) {
		info.to_block--;
		info.to_len = STANDARD_BLOCK_SIZE;
	}
	
	return info;
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
	const char *values[11] = { (const char *)&param1, new_file, path, (const char *)&param2, (const char *)&param3, (const char *)&param4, (const char *)&param5, (const char *)&param6, (const char *)&param7, (const char *)&param8 };
	int lengths[11] = { sizeof( param1 ), strlen( new_file ), strlen( path ), sizeof( param2 ), sizeof( param3 ), sizeof( param4 ), sizeof( param5 ), sizeof( param6 ), sizeof( param7 ), sizeof( param8 ) };
	int binary[11] = { 1, 0, 0, 1, 1, 1, 1, 1, 1, 1 };
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

int psql_read_buf( PGconn *conn, const int id, const char *path, char *buf, const off_t offset, const size_t len, int verbose )
{
	PgDataInfo info;
	int param1;
	int param2;
	int param3; 
	const char *values[3] = { (const char *)&param1, (const char *)&param2, (const char *)&param3 };
	int lengths[3] = { sizeof( param1 ), sizeof( param2 ), sizeof( param3 ) };
	int binary[3] = { 1, 1, 1 };
	PGresult *res;
	char zero_block[STANDARD_BLOCK_SIZE];
	int block_no;
	char *iptr;
	char *data;
	size_t copied;
	int db_block_no;
	int idx;
	char *dst;
	PgMeta meta;
	int tmp;
	int size;
	
	tmp = psql_get_meta( conn, path, &meta );
	if( tmp < 0 ) {
		return tmp;
	}
	
	size = len;
	if( offset + size > meta.size ) {
		size = meta.size - offset;
	}
	
	info = compute_block_info( offset, size );
	
	param1 = htonl( id );
	param2 = htonl( info.from_block );
	param3 = htonl( info.to_block );

	res = PQexecParams( conn, "SELECT block_no, data FROM data WHERE dir_id=$1::int4 AND block_no>=$2::int4 AND block_no<=$3::int4 ORDER BY block_no ASC",
		3, NULL, values, lengths, binary, 1 );
	
	if( PQresultStatus( res ) != PGRES_TUPLES_OK ) {
		syslog( LOG_ERR, "Error in psql_read_buf for path '%s'", path );
		PQclear( res );
		return -EIO;
	}
	
	memset( zero_block, 0, STANDARD_BLOCK_SIZE );

	dst = buf;
	copied = 0;
	for( block_no = info.from_block, idx = 0; block_no <= info.to_block; block_no++ ) {
		
		/* handle sparse files */
		if( idx < PQntuples( res ) ) {
			iptr = PQgetvalue( res, idx, 0 );
			db_block_no = ntohl( *( (uint32_t *)iptr ) );
		
			if( block_no < db_block_no ) {
				data = zero_block;
			} else {
				data = PQgetvalue( res, idx, 1 );
				idx++;
			}
		} else {
			data = zero_block;
		}
				
		/* first block */
		if( block_no == info.from_block ) {
			
			memcpy( dst, data + info.from_offset, info.from_len - info.from_offset );
			
			dst += info.from_len;
			copied += info.from_len;
			
		/* last block */
		} else if( block_no == info.to_block ) {
			
			memcpy( dst, data, info.to_len );
			copied += info.to_len;
		
		/* intermediary blocks, are copied completly */
		} else {
			
			memcpy( dst, data, STANDARD_BLOCK_SIZE );
			dst += STANDARD_BLOCK_SIZE;
			copied += STANDARD_BLOCK_SIZE;
		}

		if( verbose ) {
			syslog( LOG_DEBUG, "File '%s', reading block '%d', copied: '%d', DB block: '%d'",
				path, block_no, copied, db_block_no );
		}
	}

	PQclear( res );
	
	if( copied != size ) {
		syslog( LOG_ERR, "File '%s', reading block '%d', copied '%d' bytes but expecting '%d'!",
			path, block_no, copied, size );
		return -EIO;
	}
	
	return copied;
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
		syslog( LOG_ERR, "Error in psql_delete_dir for path '%s': %s",
			path, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}
	
	PQclear( res );
	
	return 0;
}

static int psql_write_block( PGconn *conn, const int id, const char *path, const char *buf, const unsigned int block_no, const off_t offset, const size_t len, int verbose )
{
	int param1 = htonl( id );
	int param2 = htonl( block_no );
	const char *values[3] = { (const char *)&param1, (const char *)&param2, buf + offset };
	int lengths[3] = { sizeof( param1 ), sizeof( param2 ), len };
	int binary[3] = { 1, 1, 1 };
	PGresult *res;
	char sql[256];
	
	/* could actually be an assertion, as this can never happen */
	if( offset + len > STANDARD_BLOCK_SIZE ) {
		syslog( LOG_ERR, "Got a too big block write for file '%s', block '%d': %u + %u > %d!",
			path, block_no, (unsigned int)offset, (unsigned int)len, STANDARD_BLOCK_SIZE );
		return -EIO;
	}

update_again:

	/* write a complete block, old data in the database doesn't bother us */
	if( offset == 0 && len == STANDARD_BLOCK_SIZE ) {
		
		strcpy( sql, "UPDATE data set data = $3::bytea WHERE dir_id=$1::int4 AND block_no=$2::int4" );
		
	/* small in the middle write, keep data on both sides */
	} else if( offset > 0 && offset + len < STANDARD_BLOCK_SIZE ) {

		sprintf( sql, "UPDATE data set data = substring( data from %d for %d ) || $3::bytea || substring( data from %d for %d ) WHERE dir_id=$1::int4 AND block_no=$2::int4",
			0, (unsigned int)offset,
			(unsigned int)offset + len, STANDARD_BLOCK_SIZE - ( (unsigned int)offset + len ) );
	
	/* keep data on the right */
	} else if( offset == 0 && len < STANDARD_BLOCK_SIZE ) {

		sprintf( sql, "UPDATE data set data = $3::bytea || substring( data from %d for %d ) WHERE dir_id=$1::int4 AND block_no=$2::int4",
			len, STANDARD_BLOCK_SIZE - len );

	/* keep data on the left */
	} else if( offset > 0 && offset + len == STANDARD_BLOCK_SIZE ) {
		
		sprintf( sql, "UPDATE data set data = substring( data from %d for %d ) || $3::bytea WHERE dir_id=$1::int4 AND block_no=$2::int4",
			0, (unsigned int)offset );
					
	/* we should never get here */
	} else {
		syslog( LOG_ERR, "Unhandled write case for file '%s' in block '%d': offset: %u, len: %u, blocksize: %u",
			path, block_no, (unsigned int)offset, (unsigned int)len, STANDARD_BLOCK_SIZE );
		return -EIO;
	}		
	
	if( verbose ) {
		syslog( LOG_DEBUG, "%s, block: %d, offset: %d, len: %d => %s\n",
			path, block_no, (unsigned int)offset, len, sql );
	}
	
	res = PQexecParams( conn, sql, 3, NULL, values, lengths, binary, 1 );
	
	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_write_block(%d,%u,%u) for file '%s' (%s): %s",
			block_no, (unsigned int)offset, (unsigned int)len, path,
			sql, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}

	/* ok, one block updated */
	if( atoi( PQcmdTuples( res ) ) == 1 ) {
		PQclear( res );
		return len;
	}

	/* funny problems */
	if( atoi( PQcmdTuples( res ) ) != 0 ) {
		syslog( LOG_ERR, "Unable to update block '%d' of file '%s'! Data consistency problems!",
			block_no, path );
		PQclear( res );
		return -EIO;
	}
	
	PQclear( res );
	
	/* the block didn't exist, so create one */
	res = PQexecParams( conn, "INSERT INTO data( dir_id, block_no ) VALUES"
		" ( $1::int4, $2::int4 )",
		2, NULL, values, lengths, binary, 1 );

	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_write_block(%d,%u,%u) for file '%s' allocating new block '%d': %s",
			block_no, (unsigned int)offset, (unsigned int)len, path, block_no, PQerrorMessage( conn ) );
		PQclear( res );
		return -EIO;
	}
	
	if( atoi( PQcmdTuples( res ) ) != 1 ) {
		syslog( LOG_ERR, "Unable to add new block '%d' of file '%s'! Data consistency problems!",
			block_no, path );
		PQclear( res );
		return -EIO;
	}
	
	PQclear( res );
	
	goto update_again;
}

int psql_write_buf( PGconn *conn, const int id, const char *path, const char *buf, const off_t offset, const size_t len, int verbose )
{
	PgDataInfo info;
	int res;
	unsigned int block_no;
	
	if( len == 0 ) return 0;
	
	info = compute_block_info( offset, len );
	
	/* first (partial) block */
	res = psql_write_block( conn, id, path, buf, info.from_block, info.from_offset, info.from_len, verbose );
	if( res < 0 ) {
		return res;
	}
	if( res != info.from_len ) {
		syslog( LOG_ERR, "Partial write in file '%s' in first block '%d' (%u instead of %u octets)",
			path, info.from_block, res, info.from_len );
		return -EIO;
	}
	
	/* special case of one block */
	if( info.from_block == info.to_block ) {
		return res;
	}
	
	buf += info.from_len;
	
	/* all full blocks */
	for( block_no = info.from_block + 1; block_no < info.to_block; block_no++ ) {
		res = psql_write_block( conn, id, path, buf, block_no, 0, STANDARD_BLOCK_SIZE, verbose );
		if( res < 0 ) {
			return res;
		}
		if( res != STANDARD_BLOCK_SIZE ) {
			syslog( LOG_ERR, "Partial write in file '%s' in block '%d' (%u instead of %u octets)",
				path, block_no, res, STANDARD_BLOCK_SIZE );
			return -EIO;
		}
		buf += STANDARD_BLOCK_SIZE;
	}
	
	/* last partial block */
	res = psql_write_block( conn, id, path, buf, info.to_block, 0, info.to_len, verbose );
	if( res < 0 ) {
		return res;
	}
	if( res != info.to_len ) {
		syslog( LOG_ERR, "Partial write in file '%s' in last block '%d' (%u instead of %u octets)",
			path, block_no, res, info.to_len );
		return -EIO;
	}
	
	return len;
}

int psql_truncate( PGconn *conn, const int id, const char *path, const off_t offset )
{
	PgDataInfo info;
	int res;
	PgMeta meta;
	int param1;
	int param2;
	const char *values[2] = { (const char *)&param1, (const char *)&param2 };
	int lengths[2] = { sizeof( param1 ), sizeof( param2 ) };
	int binary[2] = { 1, 1 };
	PGresult *dbres;
	
	res = psql_get_meta( conn, path, &meta );
	if( res < 0 ) {
		return res;
	}
	
	info = compute_block_info( 0, offset );
	
	param1 = htonl( id );
	param2 = htonl( info.to_block );
	
	dbres = PQexecParams( conn, "DELETE FROM data WHERE dir_id=$1::int4 AND block_no>$2::int4",
		2, NULL, values, lengths, binary, 1 );
	
	if( PQresultStatus( dbres ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Error in psql_truncate for file '%s' to size '%u': %s",
			path, (unsigned int)offset, PQerrorMessage( conn ) );
		PQclear( dbres );
		return -EIO;
	}
	
	PQclear( dbres );
	
	meta.size = offset;
	
	res = psql_write_meta( conn, id, path, meta );
	if( res < 0 ) {
		return res;
	}
	
	return 0;
}

int psql_begin( PGconn *conn )
{
	PGresult *res;
	
	res = PQexec( conn, "BEGIN" );
	
	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Begin of transaction failed!!" );
		return -EIO;
	}
	
	PQclear( res );
	
	return 0;
}

int psql_commit( PGconn *conn )
{
	PGresult *res;
	
	res = PQexec( conn, "COMMIT" );
	
	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Commit of transaction failed!!" );
		return -EIO;
	}
	
	PQclear( res );
	
	return 0;
}

int psql_rollback( PGconn *conn )
{
	PGresult *res;
	
	res = PQexec( conn, "ROLLBACK" );
	
	if( PQresultStatus( res ) != PGRES_COMMAND_OK ) {
		syslog( LOG_ERR, "Rollback of transaction failed!!" );
		return -EIO;
	}
	
	PQclear( res );
	
	return 0;
}
