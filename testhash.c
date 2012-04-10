#include "hash.h"

#include <stdbool.h>

static bool alloc1( ) 
{
	hashtable_t *h;
	
	h = hashtable_create( 100, hashtable_int_hash, hashtable_int_compare );
	if( h == NULL ) return false;
	
	hashtable_destroy( h );
	
	return true;
}

static bool alloc2( )
{
	hashtable_t h;
	
	hashtable_init( &h, 100, hashtable_int_hash, hashtable_int_compare );
	
	return true;
}
	
int main( void )
{
	if( !alloc1( ) ) return 1;
	if( !alloc2( ) ) return 1;
	
	return 0;
}
