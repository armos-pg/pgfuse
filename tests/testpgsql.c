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

#include <libpq-fe.h>		/* for Postgresql database access */

#include <stdio.h>		/* for fprintf */
#include <string.h>		/* for strcmp */
#include <stdbool.h>		/* for bool */
#include <stdint.h>		/* for uint64_t */
#include <sys/time.h>		/* for struct timespec, gettimeofday */

#include "endian.h"		/* for be64toh */

/* January 1, 2000, 00:00:00 UTC (in Unix epoch seconds) */
#define POSTGRES_EPOCH_DATE 946684800

static struct timespec get_now( void )
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

int main( int argc, char *argv[] )
{
	PGconn *conn;
	const char *value;
	bool integer_datetimes;
	PGresult *res;
	uint64_t tmp;
	uint64_t param1;
	const char *values[1] = { (const char *)&param1 };
	int lengths[1] = { sizeof( param1 ) };
	int binary[1] = { 1 };
	struct timespec now;
	
	if( argc != 2 ) {
		fprintf( stderr, "usage: testpgsql <Pg conn info>\n" );
		return 1;
	}
	
	conn = PQconnectdb( argv[1] );
	if( PQstatus( conn ) != CONNECTION_OK ) {
		fprintf( stderr, "Connection to database failed: %s",
			PQerrorMessage( conn ) );
		PQfinish( conn );
		return 1;
	}
	
	value = PQparameterStatus( conn, "integer_datetimes" );
	if( value == NULL ) {
		fprintf( stderr, "PQ param integer_datetimes empty?\n" );
		PQfinish( conn );
		return 1;
	}
	
	integer_datetimes = strcmp( value, "on" ) == 0 ? true : false;
	printf( "integer_datetimes: %s\n", integer_datetimes ? "true" : "false" );

	now = get_now( );
	tmp = ( (uint64_t)now.tv_sec - POSTGRES_EPOCH_DATE ) * 1000000;
	tmp += now.tv_nsec / 1000;
	param1 = htobe64( tmp );
		
	res = PQexecParams( conn, "SELECT now(),$1::timestamp",
		1, NULL, values, lengths, binary, 1 );
	
	if( PQresultStatus( res ) != PGRES_TUPLES_OK ) {
		fprintf( stderr, "select error: %s\n", PQerrorMessage( conn ) );
		PQclear( res );
		PQfinish( conn );
		return 1;
	}
	
	if( PQntuples( res ) == 0 ) {
		PQclear( res );
		PQfinish( conn );
		return 1;
	}
	
	if( PQntuples( res ) > 1 ) {
		fprintf( stderr, "Expecting exactly one tuple as result." );
		PQclear( res );
		PQfinish( conn );
		return 1;
	}
		
	/* Since PostgreSQL 8.4 int64 representation should be the default
	 * unless changed at compilation time
	 */
	if( integer_datetimes ) {
		char *data_db;
		char *data_select;
		struct timespec time_db;
		struct timespec time_select;
		uint64_t t_db;
		uint64_t t_select;
		
		data_db = PQgetvalue( res, 0, 0 );
		
		t_db = be64toh( *( (uint64_t *)data_db ) );
		
		time_db.tv_sec = POSTGRES_EPOCH_DATE + t_db / 1000000;
		time_db.tv_nsec = ( t_db % 1000000 ) * 1000;

		data_select = PQgetvalue( res, 0, 1 );
		
		t_select = be64toh( *( (uint64_t *)data_select ) );
		
		time_select.tv_sec = POSTGRES_EPOCH_DATE + t_select / 1000000;
		time_select.tv_nsec = ( t_select % 1000000 ) * 1000;

		now = get_now( );
				
		printf( "now passed as param: %lu.%lu, now from database: %lu.%lu, now computed: %lu.%lu\n",
			time_select.tv_sec, time_select.tv_nsec, time_db.tv_sec, time_db.tv_nsec, now.tv_sec, now.tv_nsec );
	} else {
		/* doubles have no standard network representation! */
		fprintf( stderr, "Not supporting dates as doubles!\n" );
		PQclear( res );
		PQfinish( conn );
		return 1;
	}

	value = PQparameterStatus( conn, "client_encoding" );
	if( value == NULL ) {
		fprintf( stderr, "PQ param client_encoding empty?\n" );
		PQfinish( conn );
		return 1;
	}
	
	printf( "integer_datetimes: %s\n", value );

	PQfinish( conn );
	
	return 0;
}
