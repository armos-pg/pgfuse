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

#ifndef CONFIG_H
#define CONFIG_H

/* standard block size, rather a simulation currently */
#define STANDARD_BLOCK_SIZE 512

/* maximal number of open files, limited currently due to a too simple
 * hash table implementation of open file handles */
#define MAX_NOF_OPEN_FILES 256

/* maximum size of a file, rather arbitrary, 2^31 is a current implementation
 * limit, before fixing this, the storing and efficiency has to be rethought
 * anyway.. */
#define MAX_FILE_SIZE 65535

#endif
