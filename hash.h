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

#ifndef HASH_H

#include <stdint.h>		/* for uintptr_t */
#include <stddef.h>		/* for size_t */
#include <stdbool.h>		/* for bool */

struct hashnode_t {
	uintptr_t key;
	uintptr_t value;
	struct hashnode_t *next;	/* overflow link */
};

typedef size_t (*hashfunc_t)( uintptr_t );
typedef int (*comparefunc_t)( uintptr_t a, uintptr_t b );

typedef struct hashtable_t {
	size_t size;			/* size of hash table */
	struct hashnode_t *nodes;	/* memory buffer, allocated outside */
	hashfunc_t hash;		/* hash function */
	comparefunc_t compare;		/* compare function */
	void *mem;			/* optional allocated memory */
	bool self_allocated;		/* self allocated or not? */
} hashtable_t;

hashtable_t *hashtable_init_mem( hashtable_t *h, void *mem, size_t size, hashfunc_t hash, comparefunc_t compare);
hashtable_t *hashtable_init( hashtable_t *h, size_t size, hashfunc_t hash, comparefunc_t compare );
hashtable_t *hashtable_create( size_t size, hashfunc_t hash, comparefunc_t compare );
void hashtable_destroy( hashtable_t *h );

size_t hashtable_int_hash( uintptr_t key );
int hashtable_int_compare( uintptr_t a, uintptr_t b );

#endif
