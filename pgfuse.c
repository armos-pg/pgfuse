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

#include <unistd.h>		/* for exit */
#include <stdlib.h>		/* for EXIT_FAILURE, EXIT_SUCCESS */
#include <stdio.h>		/* for fprintf */
#include <string.h>		/* for strdup */

#include "pgfuse_cmdline.h"	/* for command line and option parsing (gengetopt) */

static int parse_options_and_arguments( int argc, char *argv[], struct gengetopt_args_info *args_info ) {
	struct cmdline_parser_params params;

	cmdline_parser_params_init( &params );
	params.override = 1;
	params.initialize = 0;
	params.check_required = 1;

        cmdline_parser_init( args_info );

        if( cmdline_parser_ext( argc, argv, args_info, &params ) != 0 ) {
        	cmdline_parser_free( args_info );
        	return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}

static int test_config( const char *filename ) {
	return EXIT_SUCCESS;
}

static int read_config( const char *filename, struct gengetopt_args_info *args_info ) {
	char *config_filename = strdup( filename );
	struct cmdline_parser_params params;

	cmdline_parser_params_init( &params );
	params.override = 0;
	params.initialize = 0;
	params.check_required = 1;

	if( cmdline_parser_config_file( config_filename, args_info, &params ) != 0 ) {
		fprintf( stdout, "\n%s\n", gengetopt_args_info_usage );
		cmdline_parser_free( args_info );
		free( config_filename );
		return EXIT_FAILURE;
	}
	free( config_filename );
	
	return EXIT_SUCCESS;
}

int main( int argc, char *argv[] )
{
	struct gengetopt_args_info args_info;
	
	if( parse_options_and_arguments( argc, argv, &args_info ) == EXIT_FAILURE ) {
		exit( EXIT_FAILURE );
	}

	if( args_info.config_file_given ) {
		if( read_config(  args_info.config_file_arg, &args_info ) == EXIT_FAILURE ) {
			exit( EXIT_FAILURE );
		}
	}
	
	if( args_info.test_given ) {
		cmdline_parser_free( &args_info );
		exit( test_config( args_info.config_file_arg ) );
	}
	
	cmdline_parser_free( &args_info );
	
	exit( EXIT_SUCCESS );
}
