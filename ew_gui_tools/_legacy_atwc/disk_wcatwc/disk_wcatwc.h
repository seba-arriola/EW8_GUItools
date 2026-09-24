/******************************************************************
 *                         File disk_wcatwc.h                     *
 *                                                                *
 *  Include file for disk writer used at the West Coast/Alaska    *
 *  Tsunami Warning Center.  Made into Earthworm module 01/2001.  *
 ******************************************************************/
#include <earlybirdlib.h>

#define    TOO_OLD   24*60*60 /* If data is this many seconds behind present,
                                 ignore it */

typedef struct {
   char StaFile[64];              /* Name of file with SCN info */
   char StaDataFile[64];          /* Further data on stations */
   char ResponseFile[64];         /* Broadband stn response file */
   long InKey;                    /* Key to ring where waveforms live */
   int  HeartbeatInt;             /* Heartbeat interval in seconds */
   int  Debug;                    /* If 1, print debug messages */
   unsigned char MyModId;         /* Module id of this program */
   int  FileLength;               /* File size in minutes long */
   int  CircDeleteHours;          /* # hours before data delete (0->no delete)*/
   char DiskWritePath[64];        /* Directory to write disk files */
   char FileSuffix[16];           /* Starting letter of file suffix */
   SHM_INFO InRegion;             /* Info structure for input region */
} GPARM;

typedef struct {
   unsigned char MyInstId;        /* Local installation */
   unsigned char GetThisInstId;   /* Get messages from this inst id */
   unsigned char GetThisModId;    /* Get messages from this module */
   unsigned char TypeHeartBeat;   /* Heartbeat message id */
   unsigned char TypeError;       /* Error message id */
   unsigned char TypeWaveform;    /* Waveform buffer for data input */
} EWH;

/* Function prototypes
   *******************/
thr_ret CircDeleteThread( void * );
void ConvertTM2ST( struct tm *, SYSTEMTIME * );
int  GetEwh( EWH * );
double DateToModJulianSec( SYSTEMTIME );
void   NewDateFromModSec( SYSTEMTIME *, double );

int  GetConfig( char *, GPARM * );                     /* config.c */
void LogConfig( GPARM * );

int  InitDiskFile( char *, STATION[], double, int, char *, int, double,
                   CHNLHEADER [], int );               /* diskw.c */
int  WriteDiskData( STATION *, long *, GPARM *, TRACE_HEADER *, int, 
                    STATION [], CHNLHEADER [] );

