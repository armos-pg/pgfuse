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

#ifndef POOL_H
#define POOL_H

#include <sys/types.h>		/* size_t */

#include <libpq-fe.h>		/* for Postgresql database access */

#include <pthread.h>		/* for mutex and conditionals */

typedef struct PgConnPool {
	PGconn **conns;		/* array of connections */
	size_t size;		/* max number of connections */
	pid_t *avail;		/* slots of allocated/available connections per thread */
	pthread_mutex_t lock;	/* monitor lock */
	pthread_cond_t cond;	/* condition signalling a free connection */
} PgConnPool;

int psql_pool_init( PgConnPool *pool, const char *conninfo, const size_t max_connections );

int psql_pool_destroy( PgConnPool *pool );

PGconn *psql_pool_acquire( PgConnPool *pool, pid_t pid );

int psql_pool_release( PgConnPool *pool, pid_t pid );

#endif
