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

/* version of PgFuse */

#define PGFUSE_VERSION		"0.0.1"

/* standard block size, rather a simulation currently */

#define STANDARD_BLOCK_SIZE	512

/* maximal number of open files, may vary as there may be hashtable
 * collitions. Must be a prime */

#define MAX_NOF_OPEN_FILES	257

/* maximum size of a file, rather arbitrary, 2^31 is a current implementation
 * limit, before fixing this, the storing and efficiency has to be rethought
 * anyway.. */

#define MAX_FILE_SIZE		10485760

/* maximum length of a filename , rather arbitrary choice */

#define MAX_FILENAME_LENGTH	4096

#endif
