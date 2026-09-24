/******************************************************************
 *                          File loc_wcatwc.h                     *
 *                                                                *
 *  Include file for location module used at the West Coast/Alaska*
 *  Tsunami Warning Center.  Made into Earthworm module 2/2001.   *
 ******************************************************************/

#include <trace_buf.h>
#include <earlybirdlib.h>

/* Definitions
 *************/             
#define MAX_PBUFFS       100  /* Max number of P-pick buffers */
#define MAX_STN_REM      100  /* Max number of Stations to permanently remove */
#define BUFFER_TIMEOUT   150  /* Adjusted P-pick time limit */
#define PPICK_TIMEOUT   1800  /* Don't accept P-picks older than this (sec) */
#define MAX_SCAVENGE      20  /* Don't scavenge from a buffer if it has more 
                                 than this # of picks */
#define MAX_VERSIONS      98  /* Max number of times to allow quake to be
                                 located. (Needed due to an occasional
                                 infinite loop triggered by scavenging) */
#define MAX_NUM_NEAR_STN 50   /* Number of near-by stations to compare with
                                 incoming pick */


typedef struct {
   char           StaFile[64];    /* P-pick station file used in picker */
   char           StaDataFile[64];/* Station information file */
   char           ResponseFile[64]; /* Broadband stn response file */
   char           ATPLineupFileBB[64];/* Optional cmd when used with ATPlayer */
   long           InKey;          /* Key to ring where waveforms live */
   long           OutKey;         /* Key to ring where picks will live */
   long           AlarmKey;       /* Key to ring where alarms will live */
   int            HeartbeatInt;   /* Heartbeat interval in seconds */
   int            Debug;          /* If 1, print debug messages */
   unsigned char  MyModId;        /* Module id of this program */
   int            NumPBuffs;      /* Number of P-pick buffers */
   int            iNumNearStn;    /* Number of nearest stations for comparison
                                     with PBuffers */
   double     MaxTimeBetweenPicks;/* Time (min.) between Ps to start new buff */
   int            MinPs;          /* Minimum number of P-times/buff to locate */
   double         MinMagToSend;   /* For email output - min magnitude to send */
   double         SouthernLat;    /* Southern latitude of region to solve for */
   double         NorthernLat;    /* Northern latitude of region to solve for */
   double         WesternLon;     /* Western longitude of region to solve for */
   double         EasternLon;     /* Eastern longitude of region to solve for */
   char           szBValFile[128];/* File containing Richter B-values for mb */
   char           szOldQuakes[128];/* List of last MAX_QUAKES quakes */
   char           szAutoLoc[128]; /* ANALZYE Trigger file to update screen */
   char           szDummyFile[128];/* WC/ATWC EarlyBird dummy File */
   char           szMapFile[128]; /* WC/ATWC EarthVu map key file */
   char           szRTPFile[64];  /* File to send P/mag data to LOCATE */
   char           szPFilePath[64];/* Path for loc_wcatwc P data files */
   char           szQLogFile[64]; /* Log of all locations made */
   char           szMwFile[128];  /* Mws computed in mm */
   char           szDepthDataFile[128];/* File with depth data */
   char           CityFileWUC[128];/* West coast city file - upper case */
   char           CityFileWLC[128];/* West coast city file - lower case */
   char           CityFileEUC[128];/* East coast city file - upper case */
   char           CityFileELC[128];/* East coast city file - lower case */
   char           szNameFile[128];/* FE Region Name File */
   char           szNameFileLC[128];/* FE Region Name File - lower case */
   char           szIndexFile[128];/* FE Region Index File */
   char           szLatFile[128]; /* FE Region Lat File */
   SHM_INFO       InRegion;       /* Info structure for input region */
   SHM_INFO       OutRegion;      /* Info structure for output region */
   SHM_INFO       AlarmRegion;    /* Info structure for alarm output region */
} GPARM;

typedef struct {
   unsigned char MyInstId;        /* Local installation */
   unsigned char GetThisInstId;   /* Get messages from this inst id */
   unsigned char GetThisModId;    /* Get messages from this module */
   unsigned char TypeHeartBeat;   /* Heartbeat message id */
   unsigned char TypeError;       /* Error message id */
   unsigned char TypeAlarm;       /* Tsunami Ctr alarm message id */
   unsigned char TypePickTWC;     /* Tsunami Ctr P-picker message id */
   unsigned char TypeH71Sum2K;    /* Hypocenter message */
   unsigned char TypeHypoTWC;     /* Hypocenter message - TWC format*/
} EWH;

/* Function declarations for loc_wcatwc
   ************************************/
void    AddInMw( char *, HYPO * );
int     CreateNearbyStationLookupTable( STATION *,
                                        char[][MAX_NUM_NEAR_STN][TRACE_STA_LEN],
                                        int, int );
int     GetEwh( EWH * );
int     LocateQuake( PPICK *, int *, GPARM *, HYPO *, int, EWH *, CITY *, int,
                     HYPO [], int [], CITY *, EQDEPTHDATA[], int );
thr_ret LocateThread( void * );
int     SortAllByDistance( const void *, const void * );

int     GetConfig( char *, GPARM * );                    /* config.c */
void    LogConfig( GPARM * );

void    CheckPBuffTimes( PPICK **, int [], HYPO [], int, GPARM *, int [],
                         int [], char[][MAX_NUM_NEAR_STN][TRACE_STA_LEN], int,
                         int, int );
void    LoadPagerString( HYPO *, char *, CITY *, CITY *, GPARM * );/* locate.c*/
void    LoadUpPBuff( PPICK *, PPICK **, int [], HYPO [], int *, GPARM *,
                     EWH *, CITY *, int [], int [], CITY *, EQDEPTHDATA[],
                     char[][MAX_NUM_NEAR_STN][TRACE_STA_LEN], int, int, int );
void    MakeH71Msg( HYPO *, char * );
void    MakeTWCMsg( HYPO *, char * );
void    RemoveP( PPICK *, int *, int );
