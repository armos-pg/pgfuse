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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

int main( void )
{
	char buf[4096];
	int fd;
	ssize_t res;
	
	memset( buf, 0, 4096 );
	
	fd = open( "./mnt/testfsync.data", O_WRONLY | O_CREAT,
	    S_IRUSR | S_IWUSR );
	if( fd < 0 ) {
		perror( "Unable to open testfile" );
		return 1;
	}
	
	res = write( fd, buf, 4096 );
	if( res != 4096 ) {
		perror( "Error writing" );
		(void)close( fd );
		return 1;
	}
	
	fdatasync( fd );
	fsync( fd );
	
	(void)close( fd );
	
	return 0;
}
