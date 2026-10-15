/*
 * latency_scnl.h
 *
 * Header file for Earthworm packets for latency information.
 */

#ifndef LATENCY_SCNL_H
#define LATENCY_SCNL_H

#include "strfuncs.h"
#include "trace_buf.h"
#include <stdio.h>
#include <string.h>
#define SEED_STA_LEN 5  // station without terminating NULL
#define SEED_NET_LEN 2  // network without terminating NULL
#define SEED_CHAN_LEN 3 // channel without terminating NULL
#define SEED_LOC_LEN 2  // location without terminating NULL

#define SEED_STA_STR_LEN (SEED_STA_LEN + 1)   // station with terminating NULL
#define SEED_NET_STR_LEN (SEED_NET_LEN + 1)   // network with terminating NULL
#define SEED_CHAN_STR_LEN (SEED_CHAN_LEN + 1) // channel with terminating NULL
#define SEED_LOC_STR_LEN (SEED_LOC_LEN + 1)   // location with terminating NULL

// latency output type for write_latency and write_latency_bin
typedef enum LATENCY_OUTPUT_TYPE {
  LATENCY_OUTPUT_TYPE_LATENCY_SCNL_BIN,
  LATENCY_OUTPUT_LATENCY_SCNL_TEXT,
  LATENCY_OUTPUT_TEXT_EPOCH, // Text with epoch for date
  LATENCY_OUTPUT_TEXT_UTC,   // Text with human readable date UTC
  LATENCY_OUTPUT_TEXT_LOCAL, // Text with human readable date local time zone
} LATENCY_OUTPUT_TYPE;

// maximum number of digits in a long integer (NULL-terminated)
#define LONG_LEN 21
// maximum number of digits in an integer (NULL-terminated)
#define INT_LEN 12

// latency SCNL information (with terminating NULL characters)
typedef struct _LATENCY_SCNL_INFO {
  double starttime;             /* time of first sample in epoch seconds
                                   (seconds since midnight 1/1/1970) */
  double endtime;               /* Time of last sample in epoch seconds */
  char sta[SEED_STA_STR_LEN];   /* Site name (NULL-terminated) */
  char net[SEED_NET_STR_LEN];   /* Network name (NULL-terminated) */
  char chan[SEED_CHAN_STR_LEN]; /* Component/channel code (NULL-terminated)*/
  char loc[SEED_LOC_STR_LEN];   /* Location code (NULL-terminated) */
  int latency;                  /* Latency in milliseconds */
} LATENCY_SCNL_INFO;

// latency SCNL binary (without terminating NULL characters)
typedef struct _LATENCY_SCNL_BIN {
  double starttime;         /* time of first sample in epoch seconds
                               (seconds since midnight 1/1/1970) */
  double endtime;           /* Time of last sample in epoch seconds */
  char sta[SEED_STA_LEN];   /* Site name (NULL-terminated) */
  char net[SEED_NET_LEN];   /* Network name (NULL-terminated) */
  char chan[SEED_CHAN_LEN]; /* Component/channel code (NULL-terminated)*/
  char loc[SEED_LOC_LEN];   /* Location code (NULL-terminated) */
  int latency;              /* Latency in milliseconds */
} LATENCY_SCNL_BIN;

// latency SCNL as text
typedef struct _LATENCY_SCNL_TEXT {
  char starttime[LONG_LEN];     /* time of first sample in epoch milliseconds
                                   (milliseconds since midnight 1/1/1970) */
  char endtime[LONG_LEN];       /* Time of last sample in epoch milliseconds */
  char sta[SEED_STA_STR_LEN];   /* Site name (NULL-terminated) */
  char net[SEED_NET_STR_LEN];   /* Network name (NULL-terminated) */
  char chan[SEED_CHAN_STR_LEN]; /* Component/channel code (NULL-terminated)*/
  char loc[SEED_LOC_STR_LEN];   /* Location code (NULL-terminated) */
  char latency[INT_LEN];        /* Latency in milliseconds */
} LATENCY_SCNL_TEXT;

/** Latency String size  */
#define LATENCY_STR_SIZE (sizeof(LATENCY_SCNL_TEXT))

/**
 * Convert LATENCY_SCNL_BIN to LATENCY_SCNL_INFO
 * @param latency_scnl_bin the LATENCY_SCNL_BIN
 * @param latency_scnl_info the LATENCY_SCNL_INFO
 */
void latencyscnlbin2info(LATENCY_SCNL_BIN *latency_scnl_bin,
                         LATENCY_SCNL_INFO *latency_scnl_info);

/**
 * Convert LATENCY_SCNL_BIN to LATENCY_SCNL_TEXT
 * @param latency_scnl_bin the LATENCY_SCNL_BIN
 * @param latency_scnl_text the LATENCY_SCNL_TEXT
 */
void latencyscnlbin2text(LATENCY_SCNL_BIN *latency_scnl_bin,
                         LATENCY_SCNL_TEXT *latency_scnl_text);

/**
 * Convert LATENCY_SCNL_INFO to LATENCY_SCNL_BIN
 * @param latency_scnl_info the LATENCY_SCNL_INFO
 * @param latency_scnl_bin the LATENCY_SCNL_BIN
 */
void latencyscnlinfo2bin(LATENCY_SCNL_INFO *latency_scnl_info,
                         LATENCY_SCNL_BIN *latency_scnl_bin);

/**
 * Convert LATENCY_SCNL_INFO to LATENCY_SCNL_BIN
 * @param latency_scnl_info the LATENCY_SCNL_INFO
 * @param latency_scnl_text the LATENCY_SCNL_TEXT
 */
void latencyscnlinfo2text(LATENCY_SCNL_INFO *latency_scnl_info,
                          LATENCY_SCNL_TEXT *latency_scnl_text);

/**
 * Convert LATENCY_SCNL_TEXT to LATENCY_SCNL
 * @param latency_scnl_text the LATENCY_SCNL_TEXT
 * @param latency_scnl_info  the LATENCY_SCNL_INFO
 */
void latencyscnltext2info(LATENCY_SCNL_TEXT *latency_scnl_text,
                          LATENCY_SCNL_INFO *latency_scnl_info);
/**
 * Parse the line of latency text.
 * @param latency_scnl_text the LATENCY_SCNL_TEXT
 * @param line the line
 * @return the number of  input items successfully matched and assigned
 * (normally 7) or -1 if EOF
 */
int parselatencytext(LATENCY_SCNL_TEXT *latency_scnl_text, char *line);

/**
 * Read the latency binary packet.
 * @param latency_scnl_info the LATENCY_SCNL_INFO
 * @param fp the file pointer
 * @return 0 if success, -1 if EOF, positive number if error.
 */
int read_latency_bin(LATENCY_SCNL_INFO *latency_scnl_info, FILE *fp);

/**
 * Read the latency text packet.
 * @param latency_scnl_info the LATENCY_SCNL_INFO
 * @param fp the file pointer
 * @return 0 if success, -1 if EOF, positive number if error.
 */
int read_latency_text(LATENCY_SCNL_INFO *latency_scnl_info, FILE *fp);

/**
 * Convert TBUF2 to LATENCY_SCNL_BIN
 * @param trh the TRACE2_HEADER
 * @param latency the latency
 * @param latency_scnl_bin the LATENCY_SCNL_BIN
 */
void tbuf2latencybin(TRACE2_HEADER *trh, int latency,
                     LATENCY_SCNL_BIN *latency_scnl_bin);

/**
 * Convert the TBUF2 to text.
 * @param trh the TRACE2_HEADER
 * @param latency the latency
 * @param str the string which should large enough to hold LATENCY_STR_SIZE
 * characters
 * @param fixed non-zero if fixed size
 */
void tbuf2latencystr(TRACE2_HEADER *trh, int latency, char *str, int fixed);

/**
 * Write the latency information as text.
 * @param latency_scnl_info the LATENCY_SCNL_INFO
 * @param fp the file pointer, can be stdout
 * @param outputType the output type
 */
void write_latency(LATENCY_SCNL_INFO *latency_scnl_info, FILE *fp,
                   LATENCY_OUTPUT_TYPE outputType);

/**
 * Write the latency information as text.
 * @param latency_scnl_bin the LATENCY_SCNL_BIN
 * @param fp the file pointer, can be stdout
 * @param outputType the output type
 */
void write_latency_bin(LATENCY_SCNL_BIN *latency_scnl_bin, FILE *fp,
                       LATENCY_OUTPUT_TYPE outputType);

#endif
