#ifndef __FDSN_CLIENT_H__
#define __FDSN_CLIENT_H__

#include "earthworm.h"
#include "socket_ew.h"
#include "trace_buf.h"

#define PROGRAM_SIZE 2048
#define TIME_STR_LEN 20
#ifndef PATH_MAX
#define PATH_MAX 512
#endif

#ifndef EMPTY_LOC
#define EMPTY_LOC "--"
#endif

#define FDSN_CLIENT_SERVICE_AVAILABILITY "availability"
#define FDSN_CLIENT_SERVICE_DATASELECT "dataselect"
#define FDSN_CLIENT_SERVICE_EVENT "event"
#define FDSN_CLIENT_SERVICE_STATION "station"

typedef struct _FDSN_CLIENT_INFO {
  char wgetpath[PATH_MAX];      // wget path
  char wsurl[PATH_MAX];         // web services URL (e.g.
                                // "https://service.iris.edu/fdsnws")
  char service[16];             // service (e.g. "dataselect")
  char program[PROGRAM_SIZE];   // program to execute
  char sta[TRACE2_STA_LEN];     // Site name (NULL-terminated)
  char net[TRACE2_NET_LEN];     // Network name (NULL-terminated)
  char chan[TRACE2_CHAN_LEN];   // Component/channel code (NULL-terminated)
  char loc[TRACE2_LOC_LEN];     // Location code (NULL-terminated)
  char starttime[TIME_STR_LEN]; // start time
  char endtime[TIME_STR_LEN];   // end time
  char format[256];             // format or empty for default
} FDSN_CLIENT_INFO;

/**
 * Clear the FDSN client information.
 * @param fci the FDSN client information
 */
void fdsn_client_clear(FDSN_CLIENT_INFO *fci);

/**
 * Get the program path for the FDSN client (fci->program). The fci->wgetpath,
 * fci->wsurl and fci->service values must be set.
 * @param fci the FDSN client information
 */
void fdsn_client_get(FDSN_CLIENT_INFO *fci);

/**
 * Set the end time.
 * @param fci the FDSN client information
 * @param t the eoch for the end time
 */
void fdsn_client_set_endtime(FDSN_CLIENT_INFO *fci, double t);

/**
 * Set the start time.
 * @param fci the FDSN client information
 * @param t the eoch for the start time
 */
void fdsn_client_set_starttime(FDSN_CLIENT_INFO *fci, double t);

#endif
