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

#include "pool.h"

#include <string.h>		/* for strlen, memcpy, strcmp */
#include <errno.h>		/* for ENOENT and friends */
#include <stdlib.h>		/* for malloc */
#include <syslog.h>		/* for syslog */

#define AVAILABLE -1
#define ERROR -2

int psql_pool_init( PgConnPool *pool, const char *conninfo, const size_t max_connections )
{
	size_t i;
	int res;
	
	pool->conns = (PGconn **)malloc( sizeof( PGconn * ) * max_connections );
	if( pool->conns == NULL ) {
		return -ENOMEM;
	}
	
	pool->avail = (pthread_t *)malloc( sizeof( pthread_t ) * max_connections );
	if( pool->avail == NULL ) {
		free( pool->conns );
		return -ENOMEM;
	}
	
	pool->size = max_connections;

	res = pthread_mutex_init( &pool->lock, NULL );
	if( res < 0 ) {
		free( pool->avail );
		free( pool->conns );
		return res;
	}
	
	res = pthread_cond_init( &pool->cond, NULL );
	if( res < 0 ) {
		(void)pthread_mutex_destroy( &pool->lock );
		free( pool->avail );
		free( pool->conns );
		return res;
	}

	for( i = 0; i < max_connections; i++ ) {
		pool->conns[i] = PQconnectdb( conninfo );
		if( PQstatus( pool->conns[i] ) == CONNECTION_OK ) {
			pool->avail[i] = AVAILABLE;
		} else {
			syslog( LOG_ERR, "Connection to database failed: %s",
				PQerrorMessage( pool->conns[i]) );
			PQfinish( pool->conns[i] );
			pool->avail[i] = ERROR;
		}
	}
		
	return 0;
}

int psql_pool_destroy( PgConnPool *pool )
{
	size_t i;
	int res1;
	int res2;
	
	for( i = 0; i < pool->size; i++ ) {
		if( pool->avail[i] == AVAILABLE ) {
			PQfinish( pool->conns[i] );
		} else if( pool->avail[i] > 0 ) {
			syslog( LOG_ERR, "Destroying pool connection to thread '%u' which is still in use",
				(unsigned int)pool->avail[i] );
			PQfinish( pool->conns[i] );
		}
	}
	
	free( pool->conns );
	free( pool->avail );
	
	res1 = pthread_cond_destroy( &pool->cond );
	res2 = pthread_mutex_destroy( &pool->lock );
	
	return ( res1 < 0 ) ? res1 : res2;
}

PGconn *psql_pool_acquire( PgConnPool *pool )
{
	int res;
	size_t i;

	for( ;; ) {
		res = pthread_mutex_lock( &pool->lock );
		if( res < 0 ) {
			syslog( LOG_ERR, "Locking mutex failed for thread '%u': %d",
				(unsigned int)pthread_self( ), res );
			return NULL;
		}
		
		/* find a free connection, remember pid */
		for( i = 0; i < pool->size; i++ ) {
			if( pool->avail[i] == AVAILABLE ) {
				if( PQstatus( pool->conns[i] ) == CONNECTION_OK ) {
					pool->avail[i] = pthread_self( );
					(void)pthread_mutex_unlock( &pool->lock );
					return pool->conns[i];
				} else {
					pool->avail[i] = ERROR;
				}
			}
		}
		
		/* wait on conditional till a free connection is signalled */
		res = pthread_cond_wait( &pool->cond, &pool->lock );
		if( res < 0 ) {
			syslog( LOG_ERR, "Error waiting for free condition in thread '%u': %d",
				(unsigned int)pthread_self( ), res );
			(void)pthread_mutex_unlock( &pool->lock );
			return NULL;
		}
		
		(void)pthread_mutex_unlock( &pool->lock );
	}
	
	
	return NULL;
}

int psql_pool_release( PgConnPool *pool, PGconn *conn )
{
	int res;
	int i;

	res = pthread_mutex_lock( &pool->lock );
	if( res < 0 ) return res;
	
	for( i = pool->size-1; i >= 0; i-- ) {
		if( pool->conns[i] == conn ) {
			break;
		}
	}
	
	if( i < 0 ) {
		return -EINVAL;
	}

	pool->avail[i] = AVAILABLE;
	
	(void)pthread_mutex_unlock( &pool->lock );
	(void)pthread_cond_signal( &pool->cond );
	
	return 0;	
}
