#ifndef __HEXDUMP_H__
#define __HEXDUMP_H__

#include <stdio.h>
#include <stdlib.h>

/// Print the hexadecimal dump of data in a buffer.
/// @param buffer the buffer of data
/// @param sz the number of bytes of data
/// @param perline the number of values per line
void hexdump(void *buffer, int sz, int perline);

#endif
