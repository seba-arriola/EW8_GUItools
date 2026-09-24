/*****************************************************************
        * disk_wcatwc.c                        *
        * *
        * This module writes raw trace data to disk files.  All        *
        * data (from the stations listed in pick_wcatwc.sta) will be   *
        * written to disk.  The data is written in a format used       *
        * by the West Coast/Alaska Tsunami Warning Center.  It is      *
        * described in detail in the DiskHeaderWrite function.         *
        * Data from all stations are written to the same file.  The    *
        * file length (in minutes) is specifiable in the .d file.      *
        * File lengths must be an integer value of minutes.  The       *
        * first sample from the file is the first sample at or after   *
        * the starttime of the file.  This time is encoded in the file *
        * name (that is, the nominal start time of the file).          *
        * *
        * Data is saved at whatever sample rate it is taken off the    *
        * ring with.  So, any decimation must be performed prior to    *
        * placement on this module's input ring.  In practice, more    *
        * than one instance of this module may be needed.  For example,*
        * data saved at the full rate may be necessary for P-picking   *
        * and short period magnitudes.  But, this rate may cause disk  *
        * reads to be too slow for long period data processing where   *
        * over an hour of data may be needed at once.  So, the .d file *
        * allows input of the data file name's suffix.  This way long  *
        * period data is discernable by file name from short period    *
        * data.                                                        *
        * *
        * Data files are created and initialized with headers and      *
        * zeroes when the first packet of data arrives that should     *
        * be placed in the file.  Headers are updated as more data     *
        * arrives and the data is patched in the correct section of    *
        * file.                                                        *
        * *
        * NOTE: This module will only work under Windows.  The         *
        * functions necessary in CircDeleteThread are Windows    *
        * specific. The data files are used by the WC/ATWC       *
        * programs ANALYZE, hypo_display, and mtinver which are  *
        * also Windows programs.                                 *
        * *
        * 2001: Paul Whitmore, NOAA-WCATWC - paul.whitmore@noaa.gov    *
        * *
        ****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <earthworm.h>
#include <transport.h>
#include <trace_buf.h>
#include <swap.h>
#include "disk_wcatwc.h"

/* Global variables
   ****************/
GPARM   Gparm;           /* Configuration file parameters (needed in thread) */

      /***********************************************************
       * The main program starts here.              *
       * *
       * Argument:                                              *
       * argv[1] = Name of configuration file                *
       ***********************************************************/

int main( int argc, char **argv )
{
   CHNLHEADER    *chn;            /* CHNLHEADER Array */
   int           i;               /* Loop counter */
   STATION       *StaArray;       /* Station array */
   char          *WaveBuf;        /* Pointer to waveform buffer */
   TRACE_HEADER  *WaveHead;       /* Pointer to waveform header */
   int32_t       *WaveLong;       /* CORRECCION 64 BITS: Garantizar 4 bytes */
   short         *WaveShort;      /* Short pointer to waveform data */
   int           lineLen;         /* Length of heartbeat message */
   char          line[40];        /* Heartbeat message */
   long          MsgLen;          /* Size of retrieved message */
   MSG_LOGO      getlogo;         /* Logo of requested waveforms */
   MSG_LOGO      logo;            /* Logo of retrieved msg */
   MSG_LOGO      hrtlogo;         /* Logo of outgoing heartbeats */
   int           Nsta;            /* Number of stations in list */
   time_t        then;            /* Previous heartbeat time */
   long          InBufl;          /* Maximum message size in bytes */
   EWH           Ewh;             /* Parameters from earthworm.h */
   char          *configfile;     /* Pointer to name of config file */
   pid_t         myPid;           /* Process id of this process */
   static unsigned tidCDel;       /* Thread to delete old data files */

/* Check command line arguments
   ****************************/
   if ( argc != 2 )
   {
      fprintf( stderr, "Usage: disk_wcatwc <configfile>\n" );
      return -1;
   }
   configfile = argv[1];

/* Get parameters from the configuration files
   *******************************************/
   if ( GetConfig( configfile, &Gparm ) == -1 )
   {
      fprintf( stderr, "disk_wcatwc: GetConfig() failed. Exiting.\n" );
      return -1;
   }

/* Look up info in the earthworm.h tables
   **************************************/
   if ( GetEwh( &Ewh ) < 0 )
   {
      fprintf( stderr, "disk_wcatwc: GetEwh() failed. Exiting.\n" );
      return -1;
   }

/* Specify logos of incoming waveforms and outgoing heartbeats
   ***********************************************************/
   getlogo.instid = Ewh.GetThisInstId;       /* Default to wildcards   */
   getlogo.mod    = Ewh.GetThisModId;        /* and tracebuf messages */
   getlogo.type   = Ewh.TypeWaveform;

   hrtlogo.instid = Ewh.MyInstId;
   hrtlogo.mod    = Gparm.MyModId;
   hrtlogo.type   = Ewh.TypeHeartBeat;

/* Initialize name of log-file & open it
   *************************************/
   logit_init( configfile, Gparm.MyModId, 256, 1 );

/* Get our own pid for restart purposes
   ************************************/
   myPid = getpid();
   if ( myPid == -1 )
   {
      logit( "e", "disk_wcatwc: Can't get my pid. Exiting.\n" );
      return -1;
   }

/* Log the configuration parameters
   ********************************/
   LogConfig( &Gparm );

/* Allocate the waveform buffer
   ****************************/
   InBufl = MAX_TRACEBUF_SIZ*2;
   WaveBuf = (char *) malloc( (size_t) InBufl );
   if ( WaveBuf == NULL )
   {
      logit( "et", "disk_wcatwc: Cannot allocate waveform buffer\n" );
      return -1;
   }

/* Point to header and data portions of waveform message
   *****************************************************/
   WaveHead  = (TRACE_HEADER *) WaveBuf;
   /* CORRECCION 64 BITS: int32_t * extrae perfectamente los bloques de 4 bytes del mensaje */
   WaveLong  = (int32_t *) (WaveBuf + sizeof(TRACE_HEADER));
   WaveShort = (short *) (WaveBuf + sizeof(TRACE_HEADER));

/* Read the station list and return the number of stations found.
   Allocate the station list array.
   **************************************************************/
   if ( ReadStationList( &StaArray, &Nsta, Gparm.StaFile, Gparm.StaDataFile,
                         Gparm.ResponseFile, MAX_STATIONS, 0 ) == -1 )
   {
      logit( "", "disk_wcatwc: ReadStationList() failed. Exiting.\n" );
      free( WaveBuf );
      return -1;
   }
   if ( Nsta == 0 )
   {
      logit( "et", "disk_wcatwc: Empty station list. Exiting." );
      free( WaveBuf );
      free( StaArray );
      return -1;
   }
   logit( "t", "disk_wcatwc: Displaying %d stations.\n", Nsta );

/* Log the station list
   ********************/
   LogStaList( StaArray, Nsta );

/* Allocate the CHNLHEADER array
   *****************************/
   chn = (CHNLHEADER *) calloc( Nsta, sizeof(CHNLHEADER) );
   if ( chn == NULL )
   {
      logit( "et", "pick_wcatwc: Cannot allocate the chnlheader array\n" );
      free( StaArray );
      free( WaveBuf );
      return -1;
   }

/* Start the circular delete thread
   ********************************/
   if ( Gparm.CircDeleteHours > 0 )
      if ( StartThread(  CircDeleteThread, 8192, &tidCDel )
	       == -1 )
      {
         free( StaArray );
         free( chn );
         free( WaveBuf );
         logit( "et", "Error starting CircDelete thread; exiting!\n" );
         return -1;
      }

/* Attach to existing transport ring
   *********************************/
   tport_attach( &Gparm.InRegion, Gparm.InKey );
	  
/* Flush the input ring
   ********************/
   while ( tport_getmsg( &Gparm.InRegion, &getlogo, 1, &logo, &MsgLen,
                          WaveBuf, MAX_TRACEBUF_SIZ) != GET_NONE );

/* Send 1st heartbeat to the transport ring
   ****************************************/
   time( &then );
   sprintf( line, "%ld %d\n", (long) then, myPid );
   lineLen = strlen( line );
   if ( tport_putmsg( &Gparm.InRegion, &hrtlogo, lineLen, line ) !=
        PUT_OK )
   {
      logit( "et", "disk_wcatwc: Error sending 1st heartbeat. Exiting." );
      tport_detach( &Gparm.InRegion );
      free( WaveBuf );
      free( StaArray );
      free( chn );
      return 0;
   }
	  
/* Loop to read waveform messages and write to disk
   ************************************************/
   while ( tport_getflag( &Gparm.InRegion ) != TERMINATE )
   {
      char    type[3];
      STATION *Sta;             /* Pointer to the station being processed */
      int     rc;               /* Return code from tport_getmsg() */
      time_t  now;              /* Current time */

/* Send a heartbeat to the transport ring
   **************************************/
      time( &now );
      if ( (now - then) >= Gparm.HeartbeatInt )
      {
         then = now;
         sprintf( line, "%ld %d\n", (long) now, myPid );
         lineLen = strlen( line );
         if ( tport_putmsg( &Gparm.InRegion, &hrtlogo, lineLen, line ) !=
              PUT_OK )
         {
            logit( "et", "disk_wcatwc: Error sending heartbeat. Exiting." );
            break;
         }
      }

/* Get a waveform buffer from transport region
   *******************************************/
      rc = tport_getmsg( &Gparm.InRegion, &getlogo, 1, &logo, &MsgLen,
                         WaveBuf, MAX_TRACEBUF_SIZ);

      if ( rc == GET_NONE )
      {
         sleep_ew( 200 );
         continue;
      }

      if ( rc == GET_NOTRACK )
         logit( "et", "disk_wcatwc: Tracking error.\n");

      if ( rc == GET_MISS_LAPPED )
         logit( "et", "disk_wcatwc: Got lapped on the ring.\n");

      if ( rc == GET_MISS_SEQGAP )
         logit( "et", "disk_wcatwc: Gap in sequence numbers.\n");

      if ( rc == GET_MISS )
         logit( "et", "disk_wcatwc: Missed messages.\n");

      if ( rc == GET_TOOBIG )
      {
         logit( "et", "disk_wcatwc: Retrieved message too big (%d) for msg.\n",
                MsgLen );
         continue;
      }

/* If necessary, swap bytes in the message
   ***************************************/
      if ( WaveMsgMakeLocal( WaveHead ) < 0 )
      {
         logit( "et", "disk_wcatwc: Unknown waveform type.\n" );
         logit( "t", "disk_wcatwc: Unknown waveform type: %s 0x%x 0x%x 0x%x 0x%x\n",
          WaveHead->datatype, WaveHead->datatype[0], WaveHead->datatype[1],
          WaveHead->datatype[2],WaveHead->datatype[3]);
  
         continue;
      }
	  
/* If sample rate is 0, get out of here before it kills program
   ************************************************************/
      if ( WaveHead->samprate == 0. )
      {
         logit( "", "Sample rate=0., %s %s\n", WaveHead->sta, WaveHead->chan );
         continue;
      }

/* Look up SCN number in the station list
   **************************************/
      Sta = NULL;								  
      for ( i=0; i<Nsta; i++ )
         if ( !strcmp( WaveHead->sta,  StaArray[i].szStation ) &&
              !strcmp( WaveHead->chan, StaArray[i].szChannel ) &&
              !strcmp( WaveHead->net,  StaArray[i].szNetID ) )
         {
            Sta = (STATION *) &StaArray[i];
            break;
         }

      if ( Sta == NULL )      /* SCN not found */
         continue;
		
/* Check if the time stamp is reasonable.  If it is ahead of the present
   1/1/70 time, it is not reasonable. (+1. to account for int).  If it is
   more than TOO_OLD seconds behind present, ignore it.
   **********************************************************************/
      if ( WaveHead->endtime > (double) now+1. ||
           WaveHead->endtime < (double) (now-TOO_OLD) ) 
      {
         logit( "t", "%s %s Bad Time: endtime %lf; present %ld\n",
                Sta->szStation, Sta->szChannel, WaveHead->endtime, (long) now );
         continue;
      }

/* If the samples are shorts, make them longs (which we treat as 32-bit int)
   ******************************************/
      strcpy( type, WaveHead->datatype );
      if ( (strcmp(type,"i2")==0) || (strcmp(type,"s2")==0) )
         for ( i = WaveHead->nsamp - 1; i > -1; i-- )
            WaveLong[i] = (int32_t) WaveShort[i];

/* Dump the data to disk
   *********************/
      /* CORRECCION 64 BITS: Casteo a long* unicamente para respetar el prototipo original, pero la lectura/escritura ahora es de 32 bits puros en diskw.c */
      if ( WriteDiskData( Sta, (long*)WaveLong, &Gparm, WaveHead, Nsta, StaArray,
	                      chn ) < 0 )
         logit( "t", "WriteDiskData Failure: %s %s\n",
		              Sta->szStation, Sta->szChannel );
   }

/* Detach from the ring buffers
   ****************************/
   tport_detach( &Gparm.InRegion );
   free( WaveBuf );
   free( StaArray );
   free( chn );
   logit( "t", "Termination requested. Exiting.\n" );
   return 0;
}

      /*********************************************************
       * CircDeleteThread()                    *
       * *
       * This thread checks the age of seismic data files.    *
       * If the files are older than Gparm->CircDeleteHours   *
       * they are deleted.  The files are checked every 1     *
       * minute.                                              *
       * *
       * NOTE: If CircDeleteHours is 0, this thread does not  *
       * start.                                         *
       * *
       *********************************************************/
	   
thr_ret CircDeleteThread( void *dummy )
{
   /* En Linux, la limpieza de disco no la hace el codigo en C, 
      se le delega a un CronJob del sistema operativo. */
   while ( 1 ) {
       sleep_ew( 60000 );
   }
   return NULL;
}

      /*******************************************************
       * GetEwh()                       *
       * *
       * Get parameters from the earthworm.h file.      *
       *******************************************************/

int GetEwh( EWH *Ewh )
{
   if ( GetLocalInst( &Ewh->MyInstId ) != 0 )
   {
      fprintf( stderr, "disk_wcatwc: Error getting MyInstId.\n" );
      return -1;
   }
   if ( GetInst( "INST_WILDCARD", &Ewh->GetThisInstId ) != 0 )
   {
      fprintf( stderr, "disk_wcatwc: Error getting GetThisInstId.\n" );
      return -2;
   }
   if ( GetModId( "MOD_WILDCARD", &Ewh->GetThisModId ) != 0 )
   {
      fprintf( stderr, "disk_wcatwc: Error getting GetThisModId.\n" );
      return -3;
   }
   if ( GetType( "TYPE_HEARTBEAT", &Ewh->TypeHeartBeat ) != 0 )
   {
      fprintf( stderr, "disk_wcatwc: Error getting TypeHeartbeat.\n" );
      return -4;
   }
   if ( GetType( "TYPE_ERROR", &Ewh->TypeError ) != 0 )
   {
      fprintf( stderr, "disk_wcatwc: Error getting TypeError.\n" );
      return -5;
   }
   if ( GetType( "TYPE_TRACEBUF", &Ewh->TypeWaveform ) != 0 )
   {
      fprintf( stderr, "disk_wcatwc: Error getting TYPE_TRACEBUF.\n" );
      return -8;
   }
   return 0;
}

int nSCN = 0;
void *Send = 0;
