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

#include "hash.h"

#include <stdlib.h>		/* for malloc, free */
#include <string.h>		/* for memset */
#include <assert.h>		/* for assert */

hashtable_t *hashtable_init_mem( hashtable_t *h, void *mem, size_t size, hashfunc_t hash, comparefunc_t compare )
{
	h->mem = NULL;
	h->size = size;
	h->hash = hash;
	h->compare = compare;
	h->nodes = (struct hashnode_t *)mem;
	h->self_allocated = false;
	
	memset( (void *)h->nodes, 0, sizeof( struct hashnode_t ) * size );
	
	return h;
}

hashtable_t *hashtable_init( hashtable_t *h, size_t size, hashfunc_t hash, comparefunc_t compare )
{
	void *mem = malloc( size * sizeof( struct hashnode_t ) );
	if( mem == NULL ) {
		free( h );
		return NULL;
	}
	
	(void)hashtable_init_mem( h, mem, size, hash, compare );
	
	h->mem = mem;
	
	return h;
}

hashtable_t *hashtable_create( size_t size, hashfunc_t hash, comparefunc_t compare )
{
	hashtable_t *h;
	
	h = (hashtable_t *)malloc( sizeof( hashtable_t ) );
	if( h == NULL ) return NULL;
	
	hashtable_init( h, size, hash, compare );
	
	h->self_allocated = true;
	
	return h;
}

void hashtable_destroy( hashtable_t *h )
{
	if( h->mem != NULL ) {
		free( h->mem );
	}
	
	if( h->self_allocated ) {
		free( h );
	}
}

int hashtable_insert( hashtable_t *h, uintptr_t key, uintptr_t value )
{
	struct hashnode_t *prev;
	struct hashnode_t *node;
	size_t hash;
	
	hash = h->hash( key ) % h->size;
	
	node = &h->nodes[hash];
	
	/* free slot in nodes array, put it directly there, if a key */
	if( node->key == 0 ) {
		node->key = key;
		node->value = value;
		return 0;
	}
	
	/* not free, is it the same key? If yes, we replace the data only */
	if( h->compare( node->key, key ) != 0 ) {
		node->value = value;
		return 0;
	}

	/* find last element in overflow list, again handle
	 * matching keys */
	do {
		if( h->compare( node->key, key ) != 0 ) {
			node->value = value;
			return 0;
		}
		prev = node;
		node = node->next;
	} while( node != NULL );
	assert( node == NULL );
	assert( prev != NULL );
	
	/* allocate a new child element for the overflow list */
	node = (struct hashnode_t *)malloc( sizeof( struct hashnode_t ) );
	if( node == NULL ) {
		return -1;
	}
	
	node->value = value;
	node->key = key;
	
	prev->next = node;

	return 0;
}

int hashtable_remove( hashtable_t *h, uintptr_t key )
{
	return 0;
}

size_t hashtable_int_hash( uintptr_t key )
{
	return key;
}

int hashtable_int_compare( uintptr_t a, uintptr_t b )
{
	if( a < b ) return -1;
	else if( a > b ) return 1;
	else return 0;
}
