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
#include <endian.h>		/* for be64toh (BSD-ism, but easy to port if necessary) */
#include <sys/time.h>		/* for struct timespec */

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
	uint64_t param1 = 0;
	const char *values[1] = { (const char *)&param1 };
	int lengths[1] = { sizeof( param1 ) };
	int binary[1] = { 1 };
	char *data;
	struct timespec time;
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
		uint64_t t;
		
		data = PQgetvalue( res, 0, 0 );
		
		t = be64toh( *( (uint64_t *)data ) );
		
		time.tv_sec = POSTGRES_EPOCH_DATE + t / 1000000;
		time.tv_nsec = ( t % 1000000 ) * 1000;
		
		now = get_now( );
		
		printf( "now from database, %llu now from database: %lu.%lu, now computed: %lu.%lu\n", t, time.tv_sec, time.tv_nsec, now.tv_sec, now.tv_nsec );
	} else {
		/* doubles have no standard network representation! */
		fprintf( stderr, "Not supporting dates as doubles!\n" );
		PQclear( res );
		PQfinish( conn );
		return 1;
	}
		
/*		
	iptr = PQgetvalue( res, 0, 0 );
	id = ntohl( *( (uint32_t *)iptr ) );
		
			if ( r->integer_datetimes)
		  {
		    int64_t x;

	iptr = PQgetvalue( res, 0, 0 );
	id = ntohl( *( (uint32_t *)iptr ) );

		    GET_VALUE (&vptr, x);

		    x /= 1000000;

		    val->f = (x + r->postgres_epoch * 24 * 3600 );
		  }
		else
		  {
		    double x;

		    GET_VALUE (&vptr, x);

		    val->f = (x + r->postgres_epoch * 24 * 3600 );
		  }
		  * 
		  *  r->postgres_epoch =
    calendar_gregorian_to_offset (2000, 1, 1, NULL, NULL);
    
	iptr = PQgetvalue( res, 0, 0 );
	id = ntohl( *( (uint32_t *)iptr ) );

	idx = PQfnumber( res, "size" );
	iptr = PQgetvalue( res, 0, idx );
	meta->size = ntohl( *( (uint32_t *)iptr ) );
	
	idx = PQfnumber( res, "mode" );
	iptr = PQgetvalue( res, 0, idx );
	meta->mode = ntohl( *( (uint32_t *)iptr ) );

	idx = PQfnumber( res, "uid" );
*/

	PQfinish( conn );
	
	return 0;
}
