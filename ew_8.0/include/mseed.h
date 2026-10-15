#ifndef MSEED_H
#define MSEED_H

#include "libmseed.h"
#include "trace_buf.h"

#define MSG_TBUF 0
#define MSG_MSEED2 2
#define MSG_MSEED3 3

// network, station, location and channel
typedef struct
{
  char net[LM_SIDLEN];
  char sta[LM_SIDLEN];
  char loc[LM_SIDLEN];
  char chan[LM_SIDLEN];
} ms_nslc_t;

// length of the network, station, location and channel with null character
typedef struct
{
  int netlen;
  int stalen;
  int loclen;
  int chanlen;
} ms_nslc_len;

// message callback handler
// msg - the message data
// msglength - the message length
// version - the message version (MSG_TBUF, MSG_MSEED2, MSG_MSEED3)
// returns 0 if successful
typedef int (msg_handler) (char *msg, int msglength, char version);

#endif
