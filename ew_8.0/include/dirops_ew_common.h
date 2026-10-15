#ifndef DIROPS_EW_COMMON_H
#define DIROPS_EW_COMMON_H

#include <stdio.h>

/**
 * Open a file and create any parent directories if needed if not reading
 * @param pathname the path name for the file
 * @param mode the mode for opening the file (see fopen)
 * @return the FILE pointer or NULL if erro.
 */
FILE *OpenFile(const char *pathname, const char *mode);

#endif
