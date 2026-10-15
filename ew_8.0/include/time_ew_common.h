#ifndef TIME_EW_COMMON_H
#define TIME_EW_COMMON_H

#include "time_ew.h"

/**
 * Parses the time string to get a double epoch (seconds since 1970:01:01)
 * @param t the epoch
 * @param timestr the time string
 * @return the number of time fields parsed or 0 if none
 */
int parsetime_ew(double *t, const char *const timestr);

#endif
