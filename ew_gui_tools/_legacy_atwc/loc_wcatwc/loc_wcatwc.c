/*****************************************************************
       * loc_wcatwc.c                         * * * * This program locates earthquakes given P-picks placed by     *
       * pick_wcatwc in the InRing.  Some simple logic is used to     *
       * discriminate P-picks of different quakes.  The P-time        *
       * between stations is compared to the distance between the two.*
       * If the time is larger than can be possible given the P travel*
       * time table values, the latest pick is moved into the next    *
       * open P buffer.  As more P-picks come in, they are put in a   *
       * buffer based on their P-time.  If MaxTimeBetweenPicks expires*
       * between P-picks, the next P's are put into a new buffer.     *
       * After a buffer has enough P-picks to locate a quake (MinPs), *
       * the solution is computed.  If a good solution is made, P's   *
       * from other buffers are compared to this solution and added   *
       * back into the buffer if they fit (only P's that were thrown  *
       * out due to the MaxTimeBetweenPicks critieria, not those      *
       * eliminated due to excessively large P-time differences).     *
       * *
       * Quake locations are computed using Geiger's method given an  *
       * initial location computed by a technique developed at the    *
       * West Coast/Alaska Tsunami Warning Center.  An initial guess  *
       * is first assigned to the location of the first P-time in the *
       * buffer.  If a solution can not be computed from this initial *
       * location, a routine is called to compute the initial location*
       * from azimuth and distance determined from a quadrapartite of *
       * stations.  If a location can still not be determined, a bad  *
       * P-pick discriminator is called.  This simply throws out      *
       * stations one-at-a-time (up to three stations at once) and    *
       * re-computes the location.  Good solutions are verified by    *
       * total residual.  The quake locator was first introduced in   *
       * tsunami warning centers by Sokolowski in the 1970's.  I      *
       * I think it was originally developed at NEIC before that.     *
       * *
       * The IASPEI91 travel times are used as the basis for quake    *
       * locations in this program.  A time/distance/depth table has  *
       * been created from software provided by the National          *
       * Earthquake Information Center.  Locations with this set of P *
       * times have been been compared to those made with the         *
       * Jefferey's-Bullen set of times and were found to be superior *
       * in regards to depth discrimination and epicentral location   *
       * with poor azimuthal control.  The P-table is arranged on 10km*
       * depth increments and 0.5 degree distance increments.         *
       * *
       * After a good location has been computed, magnitude is output *
       * based on the amplitude/periods reported by the P-picker.  Mb,*
       * Ml, MS, and Mwp magnitudes are computed depending on         *
       * epicentral distance.                                         *
       * *
       * The locations/magnitudes are sent to the OutRing.  Alarms    *
       * based on location and magnitude can also be issued to the    *
       * AlarmRing if desired.  The page_alarm module will send these *
       * to a pager.                                                  *
       * *
       * June, 2007: Paul Huang's new associator logic was added.     *
       * This provides a better way to sort p-picks into  *
       * buffers.                                         *
       * March, 2006: Nyland's FindDepth function added so that       *
       * average depth in region is used as fixed depth  *
       * and depth can't float beyond max + 50km         *
       * Sept., 2004: Respond to special PPick from hypo_display      *
       * which causes an immediate relocate here.        *
       * Sept., 2004: Completely remove picks from all buffers if they*
       * appear to be Bad picks.                         *
       * July, 2004: Update Mwps and screen data every x seconds      *
       * automatically after a new location is made.      *
       * *
       * 2001: Paul Whitmore, NOAA-WCATWC - paul.whitmore@noaa.gov    *
       * *
       ****************************************************************/
	   
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <earthworm.h>
#include <transport.h>
#include "loc_wcatwc.h"

/* Global Variables (those needed in threads)
   ******************************************/
CITY   city[NUM_CITIES];       /* Array of reference city locations */
CITY   cityEC[NUM_CITIES_EC];  /* Array of eastern reference city locations */
EQDEPTHDATA EqDepth[EQSIZE];   /* Array of depth structures */
GPARM  Gparm;                  /* Configuration file parameters */
HYPO   Hypo[MAX_PBUFFS];       /* Hypocenter information for each P buffer */
int    iActiveBuffer;          /* Last buffer in which hypocenter computed */
int    iLastBuffCnt[MAX_PBUFFS];/* Previous number of Ps/buffer */
int    iPBufCnt[MAX_PBUFFS];   /* Buffer P-pick Counter */
int    iNumPBufRem[MAX_PBUFFS];/* # of stns (permanently) removed from buffer */
int    Nsta;                   /* Number of stations in data file */
PPICK  *PBuf[MAX_PBUFFS];      /* Pointer to P-pick buffers */
EWH    Ewh;                    /* Parameters from earthworm.h */
mutex_t mutsem1;               /* Semaphore to protect PBuf and iPBufCnt
                                  adjustments */
STATION *StaArray;             /* Station array (from .sta file or disk file)
                                  (lat/lon in geocentric in this program only)*/
char   szPStnArray[MAX_STATIONS][MAX_NUM_NEAR_STN][TRACE_STA_LEN];/* Near stn lookup table */
char   szStnRem[MAX_PBUFFS][MAX_STN_REM][TRACE_STA_LEN];/* Removed stns */

      /***********************************************************
       * The main program starts here.              *
       * *
       * Argument:                                              *
       * argv[1] = Name of configuration file                *
       ***********************************************************/

int main( int argc, char **argv )
{
   int           i, j;            /* Loop counters */
   int           iRC;
   char          PIn[MAX_PICKTWC_SIZE];/* Pointer to P-pick from ring */
   int           lineLen;         /* Length of heartbeat message */
   char          line[40];        /* Heartbeat message */
   long          MsgLen;          /* Size of retrieved message */
   MSG_LOGO      getlogo;         /* Logo of requested picks */
   MSG_LOGO      logo;            /* Logo of retrieved msg */
   MSG_LOGO      hrtlogo;         /* Logo of outgoing heartbeats */
   time_t        then;            /* Previous heartbeat time */
   long          InBufl;          /* Maximum buffer size in bytes */
   char          *configfile;     /* Pointer to name of config file */
   pid_t         myPid;           /* Process id of this process */
   static unsigned tidLocate;     /* Quake Location Thread */
   
/* Check command line arguments
   ****************************/
   if ( argc != 2 )
   {
      fprintf( stderr, "Usage: loc_wcatwc <configfile>\n" );
      return -1;
   }
   configfile = argv[1];

/* Get parameters from the configuration files
   *******************************************/
   if ( GetConfig( configfile, &Gparm ) == -1 )
   {
      fprintf( stderr, "loc_wcatwc: GetConfig() failed. Exiting.\n" );
      return -1;
   }

/* Look up info in the earthworm.h tables
   **************************************/
   if ( GetEwh( &Ewh ) < 0 )
   {
      fprintf( stderr, "loc_wcatwc: GetEwh() failed. Exiting.\n" );
      return -1;
   }

/* Specify logos of incoming P-picks and outgoing heartbeats
   *********************************************************/
   getlogo.instid = Ewh.GetThisInstId;
   getlogo.mod    = Ewh.GetThisModId;
   getlogo.type   = Ewh.TypePickTWC;

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
      logit( "e", "loc_wcatwc: Can't get my pid. Exiting.\n" );
      return -1;
   }

/* Log the configuration parameters
   ********************************/
   LogConfig( &Gparm );

/* Load reference city coordinates used in littoral locations
  ***********************************************************/
   if ( LoadCities( city, 1, Gparm.CityFileWUC, Gparm.CityFileWLC ) < 0)
   {
      logit( "t", "LoadCities failed, exiting\n" );
      return -1;
   }

/* Load eastern reference city coordinates used in littoral locations
  *******************************************************************/
   if ( LoadCitiesEC( cityEC, 1, Gparm.CityFileEUC, Gparm.CityFileELC ) < 0)
   {
      logit( "t", "LoadCitiesEC failed, exiting\n" );
      return -1;
   }
	
/* Load Richter B-values for use in Mb magnitudes 
   **********************************************/
   if ( LoadBVals( Gparm.szBValFile ) < 0 )
   {
      logit( "t", "LoadBVals failed, exiting\n" );
      return -1;
   }

/* Load Avg and max depth data
   ***************************/
   if ( LoadEQData(Gparm.szDepthDataFile, EQSIZE, EqDepth ) == -1 )
   {
      logit( "t", "LoadEQData failed\n" );
      return -1;
   }

/* Allocate the P-pick buffers
   ***************************/
   for ( i=0; i<Gparm.NumPBuffs; i++ )
   {
      InBufl = sizeof( PPICK ) * MAX_STATIONS; 
      PBuf[i] = (PPICK *) malloc( (size_t) InBufl );
      if ( PBuf[i] == NULL )
      {
         logit( "et", "loc_wcatwc: Cannot allocate waveform buffer %d\n", i );
         return -1;
      }
   }

/* Read the station list and return the number of stations found.
   Allocate the station list array.
   **************************************************************/
   if ( ReadStationList( &StaArray, &Nsta, Gparm.StaFile, Gparm.StaDataFile,
                         Gparm.ResponseFile, MAX_STATIONS, 0 ) == -1 )
   {
      logit( "", "loc_wcatwc: ReadStationList() failed. Exiting.\n" );
      for ( i=0; i<Gparm.NumPBuffs; i++ ) free( PBuf[i] );
      return -1;
   }
   if ( Nsta == 0 )
   {
      logit( "et", "loc_wcatwc: Empty station list. Exiting." );
      for ( i=0; i<Gparm.NumPBuffs; i++ ) free( PBuf[i] );
      free( StaArray );
      return -1;
   }
   logit( "t", "loc_wcatwc: Displaying %d stations.\n", Nsta );

/* Initialize P buffers
   ********************/   
   for ( i=0; i<Gparm.NumPBuffs; i++ )
   {
      for ( j=0; j<MAX_STATIONS; j++ ) InitP( &PBuf[i][j] );
      iPBufCnt[i] = 0;
      iNumPBufRem[i] = 0;
      InitHypo( &Hypo[i] );
      Hypo[i].iQuakeID = i+1;
      Hypo[i].iVersion = 1;
      Hypo[i].iAlarmIssued = 0;
   }
   iActiveBuffer = 0;

/* Create a table which lists the nearest NumNearStn stations which
   are sending P-picks to the locator.
   ****************************************************************/
   CreateNearbyStationLookupTable( StaArray, szPStnArray, Nsta,
                                   Gparm.iNumNearStn );
   
/* Create a mutex for protecting adjustments of PBuf and iPBufCnt
   **************************************************************/
   CreateSpecificMutex( &mutsem1 );

/* Attach to existing transport rings
   **********************************/
   if ( Gparm.OutKey != Gparm.InKey )
   {
      tport_attach( &Gparm.InRegion,  Gparm.InKey );
      tport_attach( &Gparm.OutRegion, Gparm.OutKey );
      tport_attach( &Gparm.AlarmRegion, Gparm.AlarmKey );
   }
   else
   {
      tport_attach( &Gparm.InRegion, Gparm.InKey );
      Gparm.OutRegion = Gparm.InRegion;
   }

/* Flush the input ring
   ********************/
   while ( tport_getmsg( &Gparm.InRegion, &getlogo, 1, &logo, &MsgLen,
                         PIn, MAX_PICKTWC_SIZE) != GET_NONE );

/* Send 1st heartbeat to the transport ring
   ****************************************/
   time( &then );
   sprintf( line, "%ld %d\n", (long) then, myPid );
   lineLen = strlen( line );
   if ( tport_putmsg( &Gparm.OutRegion, &hrtlogo, lineLen, line ) != PUT_OK )
   {
      logit( "et", "loc_wcatwc: Error sending 1st heartbeat. Exiting." );
      if ( Gparm.OutKey != Gparm.InKey )
      {
         tport_detach( &Gparm.InRegion );
         tport_detach( &Gparm.OutRegion );
         tport_detach( &Gparm.AlarmRegion );
      }
      else
         tport_detach( &Gparm.InRegion );
      for ( i=0; i<Gparm.NumPBuffs; i++ ) free( PBuf[i] );
      free( StaArray );
      return 0;
   }

/* Start the quake location thread
   *******************************/
   if ( StartThread( LocateThread, 8388608, &tidLocate ) == -1 )
   {
      for ( i=0; i<Gparm.NumPBuffs; i++ ) free( PBuf[i] );
      free( StaArray );
      if ( Gparm.OutKey != Gparm.InKey )
      {
         tport_detach( &Gparm.InRegion );
         tport_detach( &Gparm.OutRegion );
         tport_detach( &Gparm.AlarmRegion );
      }
      else
         tport_detach( &Gparm.InRegion );
      logit( "et", "Error starting Locate thread; exiting!\n" );
      return -1;
   }

/* Loop to read picker messages and invoke the locator
   ***************************************************/
   while ( tport_getflag( &Gparm.InRegion ) != TERMINATE )
   {
      int      rc;                /* Return code from tport_getmsg() */
      time_t   now;               /* Current time */
      PPICK    PStruct;           /* P-pick data structure */

      time( &now );
      
/* If we are in the ATPlayer version of loc_, see if we should re-init
   *******************************************************************/
      if ( (now - then) > 3600 )                             /* Big gap */
      {        
         sleep_ew( 5000 );  /* Let ATPlayer fill up the files */
         logit( "t", "Large gap noted in locator\n" );
         if ( strlen( Gparm.ATPLineupFileBB ) > 2 )          /* Then we are */
         {
            logit( "", "reset StaArray Nsta = %d\n", Nsta );	 
	    free( StaArray );                                  
	    StaArray = (STATION *) calloc( MAX_STATIONS, sizeof(STATION) );
            Nsta = ReadLineupFile( Gparm.ATPLineupFileBB, StaArray );
            logit( "", "New StaArray Nsta = %d\n", Nsta );	 
            if ( Nsta < 1 )
            {
               logit( "", "Bad Lineup File read-%s\n", Gparm.ATPLineupFileBB );
               continue;
            }	   
/* Create a table which lists the nearest NumNearStn stations which
   are sending P-picks to the locator based stations in data file.
   ****************************************************************/
            CreateNearbyStationLookupTable( StaArray, szPStnArray, Nsta,
                                            Gparm.iNumNearStn );
         } 	
      }

/* Send a heartbeat to the transport ring
   **************************************/
      if ( (now - then) >= Gparm.HeartbeatInt )
      {
         then = now;
         sprintf( line, "%ld %d\n", (long) now, myPid );
         lineLen = strlen( line );
         if ( tport_putmsg( &Gparm.OutRegion, &hrtlogo, lineLen, line ) !=
              PUT_OK )
         {
            logit( "et", "loc_wcatwc: Error sending heartbeat." );
            break;
         }
      }

/* Get a p-pick from transport region
   **********************************/
      rc = tport_getmsg( &Gparm.InRegion, &getlogo, 1, &logo, &MsgLen,
                         PIn, MAX_PICKTWC_SIZE);

      if ( rc == GET_NONE )
      {
         sleep_ew( 200 );
         continue;
      }

      if ( rc == GET_NOTRACK )
         logit( "et", "loc_wcatwc: Tracking error.\n");

      if ( rc == GET_MISS_LAPPED )
         logit( "et", "loc_wcatwc: Got lapped on the ring.\n");

      if ( rc == GET_MISS_SEQGAP )
         logit( "et", "loc_wcatwc: Gap in sequence numbers.\n");

      if ( rc == GET_MISS )
         logit( "et", "loc_wcatwc: Missed messages.\n");

      if ( rc == GET_TOOBIG )
      {
         logit( "et", "loc_wcatwc: Retrieved message too big (%d) for msg.\n",
                 MsgLen );
         continue;
      }
	  
/* Put P-pick into structure (NOTE: No byte-swapping is performed, so 
   there will be trouble getting picks from an opposite order machine)
   ******************************************************************/
      if ( PPickStruct( PIn, &PStruct, Ewh.TypePickTWC ) < 0 ) continue;
	  
/* See if this "PPick" is actually a trigger to force a location in a 
   specified buffer.  If so, re-locate that buffer.
   ******************************************************************/
      if ( PStruct.iHypoID > 0 && 
           !strcmp( PStruct.szStation, "LOC" ) &&
           !strcmp( PStruct.szChannel, "ATE" ) )    /* Locate now */
      {
         logit( "et", "Force location sent from hypo_display.\n" );
         RequestSpecificMutex( &mutsem1 );   /* Semaphore protect buffer writes */
         for ( i=0; i<Gparm.NumPBuffs; i++ )
            if ( Hypo[i].iQuakeID == PStruct.iHypoID )
            {
               logit( "", "Relocate ID %ld\n", PStruct.iHypoID );
               
               /* --- INICIO DEPURACION FORZADA --- */
               logit("e", "\nDEBUG F1: [Force Relocate] Forzando LocateQuake para QID %ld...\n", PStruct.iHypoID);
               /* --- FIN DEPURACION FORZADA --- */
               
               iRC = LocateQuake( PBuf[i], &iPBufCnt[i], &Gparm, &Hypo[i], i,
                                  &Ewh, city, 1, Hypo, iPBufCnt, cityEC,
                                  EqDepth, MAX_STATIONS );
						
               /* --- INICIO DEPURACION FORZADA --- */
               logit("e", "DEBUG F2: LocateQuake forzado retorno %d. iGoodSoln = %d\n", iRC, Hypo[i].iGoodSoln);
               /* --- FIN DEPURACION FORZADA --- */

               if ( iRC == -1 )
                  logit( "et", "Problem in LocateQuake-2\n" );
/* Set active buffer to the one which has had the latest good location (or the
   buffer which has the same quake with more picks) */			   
               if ( iRC >= 0 && Hypo[i].iGoodSoln >= 2 ) iActiveBuffer = iRC;
               break;
            }
         ReleaseSpecificMutex( &mutsem1 );   /* Let someone else have sem */
         continue;
      }

/* Add other known data about station to PPick structure
   *****************************************************/
      if ( PPickMatch( &PStruct, StaArray, Nsta, 2 ) < 0 )
      {
         int      lineLen;
         time_t   errTime;
         char     errmsg[80];
         MSG_LOGO logo;
		 
         /* --- LOG DEPURACION: VER POR QUE RECHAZA EL PICK --- */
         logit("e", "DEBUG RECHAZO: El pick de la estacion %s %s %s fue DESCARTADO porque los canales/nombres no coinciden exactamente con el archivo .sta\n", PStruct.szStation, PStruct.szChannel, PStruct.szNetID);
         /* --------------------------------------------------- */

         time( &errTime );
         sprintf( errmsg, "%ld 1 %s %s %s not found in StaDataFile\n",
                  (long) errTime,
                  PStruct.szStation, PStruct.szNetID, PStruct.szChannel );
         lineLen = strlen( errmsg );
         logo.type   = Ewh.TypeError;
         logo.mod    = Gparm.MyModId;
         logo.instid = Ewh.MyInstId;
         tport_putmsg( &Gparm.InRegion, &logo, lineLen, errmsg );
         continue;
      }
	  
/* Load P-pick into proper buffer    
   ******************************/
      RequestSpecificMutex( &mutsem1 );   /* Semaphore protect buffer writes */
      
      /* --- LOG DEPURACION: SEGUIMIENTO DE BUFFERS --- */
      logit("e", "\nDEBUG ASOCIADOR: Pick aceptado de %s. Pasando a LoadUpPBuff...\n", PStruct.szStation);
      
      LoadUpPBuff( &PStruct, PBuf, iPBufCnt, Hypo, &iActiveBuffer, &Gparm,
                   &Ewh, city, iLastBuffCnt, iNumPBufRem, cityEC, EqDepth,
                    szPStnArray, Nsta, Gparm.iNumNearStn, MAX_STATIONS );
                    
      /* Verifiquemos en qué buffers terminaron los picks */
      for(int b=0; b < 10; b++) {
          if (iPBufCnt[b] > 0) {
              logit("e", "  -> Status Buffer [%d]: Tiene ahora %d picks (Se necesitan %d).\n", b, iPBufCnt[b], Gparm.MinPs);
          }
      }
      /* ---------------------------------------------- */
      
      ReleaseSpecificMutex( &mutsem1 );   /* Let someone else have sem */
   }

/* Detach from the ring buffers
   ****************************/
   if ( Gparm.OutKey != Gparm.InKey )
   {
      tport_detach( &Gparm.InRegion );
      tport_detach( &Gparm.OutRegion );
      tport_detach( &Gparm.AlarmRegion );
   }
   else
      tport_detach( &Gparm.InRegion );
   for ( i=0; i<Gparm.NumPBuffs; i++ ) free( PBuf[i] );
   free( StaArray );
   logit( "t", "Termination requested. Exiting.\n" );
   return 0;
}

  /***************************************************************
   * CreateNearbyStationLookupTable()                     *
   * *
   * This function creates the nearby station lookup table used  *
   * in the associator.                                          *
   * *
   * Arguments:                                                 *
   * Sta              Pointer to station data array          *
   * pszPStnArray     Array with nearest station lookup table*
   * iNsta            Number of stations in Sta array        *
   * iNumNearStn      Number of near stations to compare     *
   * *
   * Returns -1 if an error is encountered; 1 otherwise         *
   ***************************************************************/

int CreateNearbyStationLookupTable( STATION *Sta,
     char pszPStnArray[][MAX_NUM_NEAR_STN][TRACE_STA_LEN], int iNSta,
     int iNumNearStn  )
{
   AZIDELT azidelt;
   int     i, j, jj;
   LATLON  ll;
   STATION StaTemp[MAX_STATIONS];
   
   for ( i=0; i<iNSta; i++ )
   {
      strcpy( StaTemp[i].szStation, Sta[i].szStation );
      strcpy( StaTemp[i].szChannel, Sta[i].szChannel );
      strcpy( StaTemp[i].szNetID, Sta[i].szNetID );
      ll.dLat = Sta[i].dLat;       /* Sta array in geographic */
      ll.dLon = Sta[i].dLon;
      GeoCent( &ll );              /* Convert to geocentric */
      GetLatLonTrig( &ll );
      StaTemp[i].dLat = ll.dLat;
      StaTemp[i].dLon = ll.dLon;
      StaTemp[i].dCoslat = ll.dCoslat;
      StaTemp[i].dSinlat = ll.dSinlat;
      StaTemp[i].dCoslon = ll.dCoslon;
      StaTemp[i].dSinlon = ll.dSinlon;
      StaTemp[i].dDelta = Sta[i].dDelta;
   }
   for ( i=0; i<iNSta; i++ )
   {
      for ( j=0; j<iNSta; j++ )
      {                            /* Only use vertical in near stn table */
         if ( !strcmp( StaTemp[j].szChannel, "SHZ" ) || 
              !strcmp( StaTemp[j].szChannel, "HHZ" ) || 
              !strcmp( StaTemp[j].szChannel, "EHZ" ) || 
              !strcmp( StaTemp[j].szChannel, "SZ" ) || 
              !strncmp( StaTemp[j].szChannel, "BHZ", 3 ) )
            azidelt = GetDistanceAz( (LATLON *) &StaTemp[i], (LATLON *) &StaTemp[j] );
         else
            azidelt.dDelta = 179.;	   
         Sta[j].dDelta = azidelt.dDelta;
         strcpy( Sta[j].szStation, StaTemp[j].szStation );
         strcpy( Sta[j].szChannel, StaTemp[j].szChannel );
         strcpy( Sta[j].szNetID, StaTemp[j].szNetID );
         Sta[j].dLat = StaTemp[j].dLat;
         Sta[j].dLon = StaTemp[j].dLon;
         Sta[j].dCoslat = StaTemp[j].dCoslat;
         Sta[j].dSinlat = StaTemp[j].dSinlat;
         Sta[j].dCoslon = StaTemp[j].dCoslon;
         Sta[j].dSinlon = StaTemp[j].dSinlon;
      }	    	  
/* Sort by distance (nearest first) */	  
      qsort( (void *) Sta, iNSta, sizeof( STATION ), SortAllByDistance );
      j = 0;
      jj = 0;
      while ( jj < iNumNearStn ) 
      {
         if ( (jj == 0) ||
              (jj > 0 && strcmp( Sta[j].szStation, pszPStnArray[i][jj-1] )) )
         {
            strcpy( pszPStnArray[i][jj], Sta[j].szStation );
            jj++;
         }
         j++;
      }
   }
   for ( i=0; i<iNSta; i++ )
   {
      logit( "", "%s - ", StaTemp[i].szStation );
      for ( j=0; j<iNumNearStn; j++ )
         logit( "", "%s ", pszPStnArray[i][j] );
      logit( "", "\n" );
   }
   return ( 1 );
}

      /*******************************************************
       * GetEwh()                       *
       * *
       * Get parameters from the earthworm.d file.      *
       *******************************************************/

int GetEwh( EWH *Ewh )
{
   if ( GetLocalInst( &Ewh->MyInstId ) != 0 )
   {
      fprintf( stderr, "loc_wcatwc: Error getting MyInstId.\n" );
      return -1;
   }

   if ( GetInst( "INST_WILDCARD", &Ewh->GetThisInstId ) != 0 )
   {
      fprintf( stderr, "loc_wcatwc: Error getting GetThisInstId.\n" );
      return -2;
   }                                              
   if ( GetModId( "MOD_WILDCARD", &Ewh->GetThisModId ) != 0 )
   {
      fprintf( stderr, "loc_wcatwc: Error getting GetThisModId.\n" );
      return -3;
   }
   if ( GetType( "TYPE_HEARTBEAT", &Ewh->TypeHeartBeat ) != 0 )
   {
      fprintf( stderr, "loc_wcatwc: Error getting TypeHeartbeat.\n" );
      return -4;
   }
   if ( GetType( "TYPE_ERROR", &Ewh->TypeError ) != 0 )
   {
      fprintf( stderr, "loc_wcatwc: Error getting TypeError.\n" );
      return -5;
   }
   if ( GetType( "TYPE_ALARM", &Ewh->TypeAlarm ) != 0 )
   {
      fprintf( stderr, "loc_wcatwc: Error getting TypeAlarm.\n" );
      return -6;
   }
   if ( GetType( "TYPE_PICKTWC", &Ewh->TypePickTWC ) != 0 )
   {
      fprintf( stderr, "loc_wcatwc: Error getting TypePickTWC.\n" );
      return -7;
   }
   if ( GetType( "TYPE_H71SUM2K", &Ewh->TypeH71Sum2K ) != 0 )
   {
      fprintf( stderr, "loc_wcatwc: Error getting TYPE_H71SUM2K.\n" );
      return -8;
   }
   if ( GetType( "TYPE_HYPOTWC", &Ewh->TypeHypoTWC ) != 0 )
   {
      fprintf( stderr, "loc_wcatwc: Error getting TypeHypoTWC.\n" );
      return -9;
   }
   return 0;
}

      /*********************************************************
       * LocateThread.()                     *
       * *
       * This thread checks each P buffer to see if enough    *
       * picks have been entered to make a location.  Each    *
       * new pick added to a buffer triggers another location.*
       * Location/magnitude are checked to alarm criteria.    *
       * After location, the thread checks whether any Ps from*
       * other buffers should be added to this buffer.        *
       * At the end of each loop, the thread checks time of   *
       * Ps in each buffer to see if that buffer should be    *
       * zeroed.                                              *
       * *
       * September, 2002: Changed when new locations are made;*
       * new locations now made whenever # Ps in buffer      *
       * change and there are more than MinP picks.          *
       * *
       *********************************************************/
	   
thr_ret LocateThread( void *dummy )
{
   double  dMin;                             /* Oldest P-time in buffers */
   int     i, j;
   int     iFinal;                           /* 1->location made X min after P*/
   int     iLoc;                             /* 1->full loc; 0->mag only */
   int     iMin;                             /* Index of oldest P buffer */
   int     iRC;                              /* Return from Locate */ 
   static  long    lLastTime[MAX_PBUFFS];    /* 1/1/70 time of last location */
   long    lTime;                            /* Present 1/1/70 time */
   
   //FILE *outputstream;
   //char outFile[] = "billk_loc.txt";

   for ( i=0; i<Gparm.NumPBuffs; i++ ) iLastBuffCnt[i] = 0;

/* Loop every second to check P buffer status */
   for (;;)
   {
   
/* Check each buffer to see if there are enough Ps to locate and/or there is a
   new or changed P */   
      time( &lTime );
      for ( i=0; i<Gparm.NumPBuffs; i++ )
      {
         iFinal = 0;
         if ( Hypo[i].iGoodSoln >= 2 && Hypo[i].iFinalMade == 0 &&
             (lTime-lLastTime[i]) > (long) (Gparm.MaxTimeBetweenPicks*60.) &&
             (lTime-Hypo[i].dOriginTime < 20*60) )
              iFinal = 1;
         if ( (iPBufCnt[i] >= Gparm.MinPs && iPBufCnt[i] != iLastBuffCnt[i]) ||
               iFinal == 1 )
         {		 	   
/* Semaphore protect buffer writes since this thread and others adjust
   buffers */			   
            RequestSpecificMutex( &mutsem1 );
            iLastBuffCnt[i] = iPBufCnt[i];
			
/* Try to locate a quake with this buffer's picks (if PTime > 0. and
   there are not more than MAX_STATIONS picks in a good soln) */			
            iLoc = 0;			
            if ( PBuf[i][iPBufCnt[i]-1].dPTime > 0. &&
                 Hypo[i].iVersion < MAX_VERSIONS &&
                 Hypo[i].iNumPs < MAX_STATIONS ) iLoc = 1;
                 
            /* --- INICIO DEPURACION --- */
            logit("e", "\nDEBUG 1: [Buffer %d] Tenemos %d picks (MinPs=%d). Intentando LocateQuake()...\n", 
                  i, iPBufCnt[i], Gparm.MinPs);
            /* --- FIN DEPURACION --- */

            iRC = LocateQuake( PBuf[i], &iPBufCnt[i], &Gparm, &Hypo[i], i, &Ewh,
                               city, iLoc, Hypo, iPBufCnt, cityEC, EqDepth,
                               MAX_STATIONS );			
            
            /* --- INICIO DEPURACION --- */
            logit("e", "DEBUG 2: [Buffer %d] LocateQuake retorno %d. iGoodSoln = %d\n", i, iRC, Hypo[i].iGoodSoln);
            if (iRC >= 0 && Hypo[i].iGoodSoln >= 2) {
                logit("e", "DEBUG 3: Localizacion BUENA! Lat: %.2f, Lon: %.2f. Revisa si escribio en disco.\n", Hypo[i].dLat, Hypo[i].dLon);
            } else if (iRC >= 0 && Hypo[i].iGoodSoln < 2) {
                logit("e", "DEBUG X: Algoritmo fallo en converger o el residual es muy alto (solucion descartada).\n");
            }
            /* --- FIN DEPURACION --- */

            if ( iRC == -1 )
               logit( "et", "Problem in LocateQuake\n" );
            else
            {   
/* Check other P buffers to see if any of their Ps should go with this quake */
               if ( iFinal == 0 )
                  CheckPBuffTimes( PBuf, iPBufCnt, Hypo, i, &Gparm,
                                   iLastBuffCnt, iNumPBufRem, szPStnArray,
                                   Nsta, Gparm.iNumNearStn, MAX_STATIONS );
            }

/* Set active buffer to the one which has had the latest good location (or the
   buffer which has the same quake with more picks) */			   
            if ( iRC >= 0 && Hypo[i].iGoodSoln >= 2 ) iActiveBuffer = iRC;			   
			
            ReleaseSpecificMutex( &mutsem1 );  /* Let someone else have sem */
			
/* Is this location the final? */
            if ( iFinal == 1 ) Hypo[i].iFinalMade = 1;			   
            lLastTime[i] = lTime;              /* Save for future comparisons */
         }
      }
		 
/* Reset oldest P Buffer to zero when all are filled */
      for ( i=0; i<Gparm.NumPBuffs; i++ )
         if ( iPBufCnt[i] == 0 ) goto Sleeper;  /* Then, at least 1 is empty */
		 
/* If we get here, all P buffers must have some Ps in them, so one buffer must
   be cleared out. Find the one with the oldest P's and re-init that one. */
      iMin = 0;
      dMin = 1.E20;
      for ( i=0; i<Gparm.NumPBuffs; i++ )
/* Save this if MS could still be updating */
         if ( Hypo[i].dMSAvg == 0. ||
            ((double) lTime-Hypo[i].dOriginTime) > 7200. )
            for ( j=0; j<iPBufCnt[i]; j++ )
               if ( PBuf[i][j].dPTime < dMin && PBuf[i][j].dPTime > 0.0 )
               {
                  dMin = PBuf[i][j].dPTime;
                  iMin = i;
               }
      RequestSpecificMutex( &mutsem1 );   /* Semaphore protect buffer writes */
      iPBufCnt[iMin] = 0;
      iNumPBufRem[iMin] = 0;
      iLastBuffCnt[iMin] = 0;
      for ( i=0; i<MAX_STATIONS; i++ ) InitP( &PBuf[iMin][i] );
      InitHypo( &Hypo[iMin] );
      Hypo[iMin].iQuakeID += Gparm.NumPBuffs;
      if ( Hypo[iMin].iQuakeID >= 10000 ) Hypo[iMin].iQuakeID -= 10000;
      Hypo[iMin].iVersion = 1;
      Hypo[iMin].iAlarmIssued = 0;
      ReleaseSpecificMutex( &mutsem1 );   /* Let someone else have sem */
			
/* Wait a bit before looping again */	 
Sleeper:
      sleep_ew( 3000 );   
   }
}

      /******************************************************************
       * SortAllByDistance()                      *
       * *
       * This function sorts the STATION array by distance.  It includes*
       * all stations in the sort.  This is a sort routine for qsort.   *
       * *
       * Arguments:                                                    *
       * pP1, pP2         STATION structures                          *
       * *
       ******************************************************************/
	   
int SortAllByDistance( const void *pP1, const void *pP2 )
{
   STATION   *pP1T, *pP2T;

   pP1T = (STATION *) pP1;
   pP2T = (STATION *) pP2;
   if ( pP1T->dDelta > pP2T->dDelta ) return 1;
   else if ( pP1T->dDelta < pP2T->dDelta ) return -1;
   else return 0;
}
