
 /************************************************************************
  * ReadStationData.c                                                    *
  *                                                                      *
  * These functions read in meta-data and controld data for the seismic  *
  * stations used in Earlybird.  The data comes from several files,      *
  * mainly *.sta, station.dat, calibs, and the screen.ini file.          *
  *                                                                      *
  * Moved into libsrc folder in generalized in 5/2008.                   *
  *                                                                      *
  ************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <earthworm.h>
#include <transport.h>
#include <trace_buf.h>
#include "earlybirdlib.h"

    /*************************************************************************
     *                             IsComment()                               *
     *                                                                       *
     *  Accepts: String containing one line from a mtinver station list      *
     *  Returns: 1 if it's a comment line                                    *
     *           0 if it's not a comment line                                *
     *************************************************************************/

int IsComment( char string[] )
{
   int i;

   for ( i=0; i<(int)strlen( string ); i++ )
   {
      char test = string[i];

      if ( test!=' ' && test!='\t' && test!='\n' )
      {
         if ( test == '#'  )
            return 1;          /* It's a comment line */
         else
            return 0;          /* It's not a comment line */
      }
   }
   return 1;                   /* It contains only whitespace */
}

  /***************************************************************
   *                       LoadResponseData()                    *
   *                                                             *
   *  Read response information in poles/zeroes form.            *
   *                                                             *
   *  Returns -1 if an error is encountered, 0 if no match found.*
   ***************************************************************/

int LoadResponseData( STATION *Sta, char *pszRespFile )
{
   double  dTemp;
   double  dAmp0;             /* Station stage 0 sensitivity */ 
   FILE    *hFile;
   int     i, iTemp, j, iLine;
   int     iNDecoded;         /* Number of fields successfully read in sscanf */
   int     iNPole, iNZero;    /* Number of poles and number of zeroes */
   int     iUse;              /* Calibs flag indicating whether or not to use */
   char    szChannel[TRACE_CHAN_LEN], szStation[TRACE_STA_LEN],
           szNetID[TRACE_NET_LEN];
   char    szLine[128];
   fcomplex zPoles[MAX_ZP];   /* poles of response function */
   fcomplex zZeros[MAX_ZP];   /* zeros of response function */

/* First, intialize information in the structure
   *********************************************/
   Sta->dAmp0 = 0.;
   Sta->iNZero = 0;
   Sta->iNPole = 0;
   for ( i=0; i<MAX_ZP; i++ )
   {
      Sta->zPoles[i] = Complex( 0., 0. );
      Sta->zZeros[i] = Complex( 0., 0. );   
   }
         
/* Open the response file
   **********************/
   if ( (hFile = fopen( pszRespFile, "r")) == NULL )
   {
      logit( "et", "Error opening response file <%s>.\n", pszRespFile );
      return -1;
   }

/* Read response data from the response file 
   *****************************************/
   iLine = 0;
   while ( !feof( hFile ) )
   {
      if ( fgets( szLine, 126, hFile ) == NULL ) break;
      iNDecoded = sscanf( szLine, "%*s %s %s %s %*s %*lf %*lf %*lf %*s %*s %d",
                          szStation, szChannel, szNetID, &iUse );	  
      if ( iNDecoded < 4 )
      {
         logit( "et", "Error decoding response file - 1.\n" );
         logit( "e", "ndecoded: %d\n", iNDecoded );
         logit( "e", "Offending line:\n" );
         logit( "e", "%s\n", szLine );
         logit( "e", "Line # = %ld\n", iLine );
         fclose( hFile );
         return -1;
      }
      if ( fgets( szLine, 126, hFile ) == NULL ) break;
      sscanf( szLine, "%lf %lf %d", &dTemp, &dTemp, &iTemp );
      if ( fgets( szLine, 126, hFile ) == NULL ) break;
      iNDecoded = sscanf( szLine, "%*s %*d %*c %lf %d %d", &dAmp0, &iNPole,
                          &iNZero );			  
      iLine += 4;
      if ( iNDecoded < 3 )
      {
         logit( "et", "Error decoding response file - 2.\n" );
         logit( "e", "ndecoded: %d\n", iNDecoded );
         logit( "e", "Offending line:\n" );
         logit( "e", "%s\n", szLine );
         logit( "e", "Line # = %ld\n", iLine );
         fclose( hFile );
         return -1;
      }
      if ( iNPole > MAX_ZP || iNZero > MAX_ZP )
      {
         logit( "et", "Too many poles or zeroes - p=%ld, z=%ld\n", iNPole,
                                                                   iNZero );
         logit( "e", "Max poles/zeroes: %d\n", MAX_ZP );
         logit( "e", "Offending line:\n" );
         logit( "e", "%s\n", szLine );
         logit( "e", "Line # = %ld\n", iLine );
         fclose( hFile );
         return -1;
      }
      for ( j=0; j<iNPole; j++ )
      {
         if ( fgets( szLine, 126, hFile ) == NULL ) break;
         sscanf( szLine, "%e %e", &zPoles[j].r, &zPoles[j].i );
      }
      for ( j=0; j<iNZero; j++ )
      {
         if ( fgets( szLine, 126, hFile ) == NULL ) break;
         sscanf( szLine, "%e %e", &zZeros[j].r, &zZeros[j].i );
      }
      iLine += (iNPole+iNZero);
      dAmp0 *= 1.0e9;                 /* To match Benz's cal file */
      if ( !strcmp( szStation, Sta->szStation ) &&  /* Match resp file with */
           !strcmp( szChannel, Sta->szChannel ) &&  /*  station             */
           !strcmp( szNetID, Sta->szNetID ) && iUse == 1 )
      {
         Sta->dAmp0  = dAmp0;
         Sta->iNPole = iNPole;
         Sta->iNZero = iNZero;
         for ( j=0; j<iNPole; j++ )
         {
            Sta->zPoles[j].r = zPoles[j].r;
            Sta->zPoles[j].i = zPoles[j].i;
         }
         for ( j=0; j<iNZero; j++ )
         {
            Sta->zZeros[j].r = zZeros[j].r;
            Sta->zZeros[j].i = zZeros[j].i;
         }
         fclose( hFile );
         return 1;
      }
      if ( fgets( szLine, 126, hFile ) == NULL ) break;
   }	  
   fclose( hFile );
   return 0;
}

  /***************************************************************
   *                       LoadStationData()                     *
   *                                                             *
   *       Get data on station from information file             *
   *                                                             *
   *  Returns -1 if an error is encountered or no match is found.*
   ***************************************************************/

int LoadStationData( STATION *Sta, char *pszInfoFile )
{
   int     iNDecoded;                   /* Number of fields in line read */
   FILE    *hFile;
   char    szChannel[TRACE_CHAN_LEN], szStation[TRACE_STA_LEN],
           szNetID[TRACE_NET_LEN];
   char    szString[180];               /* Line from file with information */

/* Open the station data file
   **************************/
   if ( ( hFile = fopen( pszInfoFile, "r") ) == NULL )
   {
      logit( "et", "Error opening station data file <%s>.\n", pszInfoFile );
      return -1;
   }

/* Read station data from the station data file 
   ********************************************/
   while ( fgets( szString, 160, hFile ) != NULL )
   {
      iNDecoded = sscanf( szString, "%s %s %s", szStation, szChannel, szNetID );
      if ( iNDecoded < 3 )
      {
         logit( "et", "Error decoding station data file-1 %s.\n", pszInfoFile );
         logit( "e", "ndecoded: %d\n", iNDecoded );
         logit( "e", "Offending line:\n" );
         logit( "e", "%s\n", szString );
         fclose( hFile );
         return -1;
      }
	  
/* Compare SCNs
   ************/	  
      if ( !strcmp( szStation, Sta->szStation ) &&
           !strcmp( szChannel, Sta->szChannel ) &&
           !strcmp( szNetID, Sta->szNetID ) )
      {     /* We have a match */
         iNDecoded = sscanf( szString, "%s %s %s %lf %lf %lf %lf %lf %lf %lf %d %d %s",
                      Sta->szStation, Sta->szChannel, Sta->szNetID,
                      &Sta->dSens, &Sta->dGainCalibration, 
                      &Sta->dLat, &Sta->dLon, &Sta->dElevation,
                      &Sta->dClipLevel, &Sta->dTimeCorrection,					
                      &Sta->iStationType, &Sta->iAgency, Sta->szStationName );
         if ( iNDecoded < 13 )
         {
            logit( "et", "Error decoding station data file-2 %s.\n",
                   pszInfoFile );
            logit( "e", "ndecoded: %d\n", iNDecoded );
            logit( "e", "Offending line:\n" );
            logit( "e", "%s\n", szString );
            fclose( hFile );
            return -1;
         }
         fclose( hFile );
         return 0;
      }
   }
   fclose( hFile );
   return -1;
}

 /***********************************************************************
  *                             LogStaList()                            *
  *                                                                     *
  *                         Log the station list                        *
  ***********************************************************************/

void LogStaList( STATION *Sta, int iNSta )
{
   int i;

   logit( "", "\nStation List:\n" );
   for ( i=0; i<iNSta; i++ )
   {
      logit( "", "%4s",    Sta[i].szStation );
      logit( "", " %3s",   Sta[i].szChannel );
      logit( "", " %2s\n", Sta[i].szNetID );
   }
   logit( "", "\n" );
}

  /***************************************************************
   *                    ReadStationData()                        *
   *                                                             *
   * Read the entire array of stations in the data base and then *
   * add response info.                                          *
   *                                                             *
   * pszStaFile File with statioN data (station.dat)             *
   * pszStaResp File with station calibration info (calibs)      *
   * Sta     Pointer to array of STATION structures with data    *
   * iMaxStn Maximum number of stations to allow                 *
   *                                                             *
   *  Returns -1 if an error is encountered; 1 if ok             *
   ***************************************************************/
   
int ReadStationData( char *pszStaFile, char *pszStaResp, STATION sta[],
                     int iMaxStn )
{
   FILE    *hFile;
   int     i;
   int     iNDecoded;                   /* Number of fields in line read */
   int     iReturn;                     /* Return from response read call */
   char    szString[180];               /* Line from file with information */

/* Open the station data file
   **************************/
   if ( ( hFile = fopen( pszStaFile, "r") ) == NULL )
   {
      logit( "et", "Error opening station data file <%s>.\n", pszStaFile );
      return -1;
   }
   
/* Read station data from the station data file
   ********************************************/
   i = 0;
   while ( fgets( szString, 160, hFile ) != NULL )
   {
      iNDecoded = sscanf( szString, "%s %s %s %lf %lf %lf %lf %lf %lf %lf %d %d %s",
       sta[i].szStation, sta[i].szChannel, sta[i].szNetID, &sta[i].dSens,
       &sta[i].dGainCalibration, &sta[i].dLat, &sta[i].dLon, &sta[i].dElevation,
       &sta[i].dClipLevel, &sta[i].dTimeCorrection,					
       &sta[i].iStationType, &sta[i].iAgency, sta[i].szStationName );
      if ( iNDecoded < 13 )
      {
         logit( "et", "Error decoding station data file-%s.\n", pszStaFile );
         logit( "e", "ndecoded: %d\n", iNDecoded );
         logit( "e", "Offending line:\n" );
         logit( "e", "%s\n", szString );
         fclose( hFile );
         return -1;
      }
      sta[i].dTimeCorrection *= (-1.);
      
/* Read Station response file and match up with stn
   ************************************************/
      iReturn = LoadResponseData( &sta[i], pszStaResp );
      if ( iReturn == -1 )
      {
         logit( "e", "file: %s\n", pszStaResp );
         logit( "e", "scn = %s %s %s\n", sta[i].szStation, sta[i].szChannel,
                                         sta[i].szNetID );
         fclose( hFile );
         return -1;
      }
      else if ( iReturn == 0 )
         logit( "", "scn = %s %s %s - No RESPONSE info\n", sta[i].szStation,
                sta[i].szChannel, sta[i].szNetID );
      else if ( iReturn == 1 )
         logit( "", "scn = %s %s %s - RESPONSE info read\n", sta[i].szStation,
                sta[i].szChannel, sta[i].szNetID );
      InitVar( &sta[i] );
      i++;
   }
   fclose( hFile );
   return 1;
}
  
  /***************************************************************
   *                    ReadStationList()                        *
   *                                                             *
   * Read the list of stations used in this module and assign    *
   * station data to the list.                                   *
   *                                                             *
   * May, 2008: Combined all station reads and structures in EB. *
   *                                                             *
   * Sta     Pointer to array of STATION structures with data    *
   * iNSta   Number of stations to process                       *
   * pszStaList File with stations to process (...sta)           *
   * pszStaData File with station data (station.dat)             *
   * pszStaResp File with station calibration info (calibs)      *
   * iMaxStn Maximum number of stations to allow                 *
   * iPickerCall 1 if called from pick_wcatwc; 0 otherwise       *
   *                                                             *
   *  Returns -1 if an error is encountered; 1 if ok             *
   ***************************************************************/

int ReadStationList( STATION **Sta, int *iNSta, char *pszStaList,
                     char *pszStaData, char *pszStaResp, int iMaxStn,
                     int iPickerCall )
{
   double  dTemp;                 /* Values in file, not needed here */
   char    szString[256];         /* Line from .sta file */
   int     i;
   int     iReturn;               /* Return from response read call */
   int     iNDecoded;             /* Number of fields read in .sta file */
   int     iStaCnt;               /* Station counter */
   STATION *sta;
   STATION StaTemp;
   FILE    *hFile;

/* Open the station list file
   **************************/
   if ( ( hFile = fopen( pszStaList, "r") ) == NULL )
   {
      logit( "et", "Error opening station list file <%s>.\n", pszStaList );
      return -1;
   }

/* Count channels in the station file.
   Ignore comment lines and lines consisting of all whitespace.
   ************************************************************/
   iStaCnt = 0;
   while ( fgets( szString, 160, hFile ) != NULL )
   {
      if ( IsComment( szString ) ) continue;
      iNDecoded = sscanf( szString,
       "%s %s %s %d %d %d %d %lf %lf %lf %d %lf %lf\n",
       StaTemp.szStation, StaTemp.szChannel, StaTemp.szNetID,
       &StaTemp.iPickStatus, &StaTemp.iFiltStatus, &StaTemp.iSignalToNoise,
       &StaTemp.iAlarmStatus, &StaTemp.dAlarmAmp, &StaTemp.dAlarmDur,
       &StaTemp.dAlarmMinFreq, &StaTemp.iComputeMwp, &dTemp, &dTemp );
      if ( iNDecoded < 13 )
      {
         logit( "et", "Error decoding station file - 1.\n" );
         logit( "e", "iNDecoded: %d\n", iNDecoded );
         logit( "e", "Offending line:\n" );
         logit( "e", "%s\n", szString );
         return -1;
      }
      if ( iPickerCall == 1 ) 
         { if ( StaTemp.iPickStatus == 1 ) iStaCnt++; }
      else                                 iStaCnt++;
   }
   rewind( hFile );

/* Allocate the station array
   **************************/
   if ( iStaCnt > iMaxStn )   /* But first see if there are too many stns */
   {
      logit( "et", "Too many stations in Station file - %s\n", iStaCnt );
      return -1;
   }   
   sta = (STATION *) calloc( iStaCnt, sizeof( STATION ) );
   if ( sta == NULL )
   {
      logit( "et", "Cannot allocate the station array\n" );
      return -1;
   }

/* Read stations from the station list file into the station array.
   ****************************************************************/
   i = 0;
   while ( fgets( szString, 160, hFile ) != NULL )
   {
      if ( IsComment( szString ) ) continue;
      iNDecoded = sscanf( szString,"%s %s %s %d %d %d %d %lf %lf %lf %d %lf %lf\n",
       sta[i].szStation, sta[i].szChannel, sta[i].szNetID, &sta[i].iPickStatus,
       &sta[i].iFiltStatus, &sta[i].iSignalToNoise, &sta[i].iAlarmStatus, 
       &sta[i].dAlarmAmp, &sta[i].dAlarmDur, &sta[i].dAlarmMinFreq,
       &sta[i].iComputeMwp, &sta[i].dSampRate, &sta[i].dScaleFactor );
      if ( iNDecoded < 13 )
      {
         logit( "et", "Error decoding station file - 2.\n" );
         logit( "e", "ndecoded: %d\n", iNDecoded );
         logit( "e", "Offending line:\n" );
         logit( "e", "%s\n", szString );
         return -1;
      }
      if ( iPickerCall == 1 )
         if ( sta[i].iPickStatus == 0 ) continue;      
      
/* Read Station data file and match up with list
   *********************************************/      
      sta[i].dTimeCorrection *= (-1.);
      if ( LoadStationData( &sta[i], pszStaData ) == -1 )
      {
         logit( "et", "No match for scn in station info file.\n" );
         logit( "e", "file: %s\n", pszStaData );
         logit( "e", "scn = %s %s %s\n", sta[i].szStation, sta[i].szChannel,
                                         sta[i].szNetID );
         return -1;
      }
      
/* Read Station response file and match up with list
   *************************************************/
      if ( strlen( pszStaResp ) > 2 )
      {
         iReturn = LoadResponseData( &sta[i], pszStaResp );
         if ( iReturn == -1 )
         {
            logit( "e", "file: %s\n", pszStaResp );
            logit( "e", "scn = %s %s %s\n", sta[i].szStation, sta[i].szChannel,
                                            sta[i].szNetID );
            return -1;
         }
         else if ( iReturn == 0 )
            logit( "", "scn = %s %s %s - No RESPONSE info\n", sta[i].szStation,
                   sta[i].szChannel, sta[i].szNetID );
         else if ( iReturn == 1 )
            logit( "", "scn = %s %s %s - RESPONSE info read\n", sta[i].szStation,
                   sta[i].szChannel, sta[i].szNetID );
      }
      InitVar( &sta[i] );	  
      sta[i].dEndTime = 0.;
      sta[i].iFirst = 1;
      i++;
   }
   fclose( hFile );
   *Sta   = sta;
   *iNSta = iStaCnt;
   return 1;
}

