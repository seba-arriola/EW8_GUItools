#ifndef MSEEDPROCESS_H
#define MSEEDPROCESS_H

#include "mseed.h"

// Parse network, station, location and channel from an FDSN Source
// sid - source identifier
// nslc - the network, station, location and channel
// nslc_len - the length of the network, station, location and channel or NULL if none
// return 0 on success
int
ms_sid2nslc_t (const char *sid, ms_nslc_t *nslc, ms_nslc_len *nslc_len);

// Parse network, station, location and channel from an FDSN Source
// sid - source identifier
// trh2 - TRACEBUF2 header
// return 0 on success
int
ms_sid2tbuf (const char *sid, TRACE2_HEADER *trh2);

// Process a MSEED record and call the specified callback handlers
// msg - the message data
// msglength - the message length
// maxLatency - the maximum latency or 0 if none
// mh - the MSEED handler or NULL if none
// th - the TBUF handler or NULL if none
// returns 0 if successful
int mseedprocess (char *msg, int msglength, double maxLatency,
                  msg_handler mh, msg_handler th);

// Unpack NSLC from a MSEED record
// msg - the message data
// msglength - the message length
// nslc - the network, station, location and channel
// returns 0 if successful
int
ms_unpacknslc (char *msg, int msglength, ms_nslc_t *nslc);

#endif
