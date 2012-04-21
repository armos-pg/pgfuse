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

#ifndef _PGFUSE_ENDIAN_H
#define _PGFUSE_ENDIAN_H

#if defined(__linux__)
#include <endian.h>
#ifndef htobe64
#include <byteswap.h>
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define htobe64( x ) bswap_64 ( x )
#define be64toh( x ) bswap_64( x )
#else
#define htobe64( x ) ( x )
#define be64toh( x ) ( x )
#endif
#endif
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#include <sys/endian.h>
#elif defined(__OpenBSD__)
#include <sys/types.h>
#define be64toh( x ) betoh64(x)
#else
#error unknown platform for htobe64 and be64toh, port first!
#endif

#endif
