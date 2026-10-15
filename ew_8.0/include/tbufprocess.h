#ifndef TBUFPROCESS_H
#define TBUFPROCESS_H

#include "mseed.h"

typedef struct
{
  uint8_t encoding;        // data encoding
  int reclen;              // maximum record length
  uint32_t flags;          // flags to control packing
  flag verbose;            // verbosity
  flag dynamicdatasamples; // zero if datasamples was not dynamically allocated
  MS3Record *msr;          // the MS3Record or NULL if none
  MS3TraceList *mstl;      // the MS3TraceList or NULL if none
  msg_handler *mh;         // the MSEED handler
} tbufprocess_input;

// get the format
#define tbufprocess_format(ti) \
  ((ti->flags & MSF_PACKVER2) ? MSG_MSEED2 : MSG_MSEED3)

void tbufprocess_msr3_free (tbufprocess_input *ti);

void tbufprocess_mstl3_free (tbufprocess_input *ti);

void tbufprocess_free (tbufprocess_input *ti);

int tbufprocess_init (tbufprocess_input *ti);

// Process a TBUF message and call the specified callback handlers
// ti - tbufprocess input
// msg - the message data or NULL to flush
// returns 0 if successful
int tbufprocess (tbufprocess_input *ti, char *msg);

#endif
