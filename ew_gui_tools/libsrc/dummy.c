/******************************************************************
  * dummy.c                             * * *
  * Contains dummy file read/write functions.                      *
  * *
  * December, 2007: Split out Carrick's changes using ifdefs       *
  * so that PTWC can also use code in Solaris      *
  * *
  * By:   Whitmore - May, 2001                                   *
  ******************************************************************/
#ifdef _WINNT
 #define _CRTDBG_MAP_ALLOC
#endif

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>               
#include <string.h>
#include <earthworm.h>
#include "earlybirdlib.h"
#ifdef _WINNT
 #include <crtdbg.h>
#endif


#define BUFSIZE 512 // JMC -- Named Pipe


#ifdef _WINNT
LPTSTR lpszQuakeEWPipe = TEXT("\\\\.\\pipe\\earthvu.dummy"); 
int    totPipeInst = 5;
void logError( char *loc, int line )
{
   LPVOID  lpBuf;	

   FormatMessage( FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                  NULL, GetLastError (), 
                  MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), (LPTSTR) &lpBuf,
                  0, NULL );
   logit( "et", "~%s=%d~ Error Code = 0x%X, %s\n", loc, line, GetLastError(),
          lpBuf);
   LocalFree( lpBuf );
}
#endif

/* This function ensures that only one process has access to a particular file*/

#ifdef _WINNT
HANDLE hMutex = NULL;
void closeFile( FILE *str )
{
   fflush( str );
   fclose( str );
/* Allow queued activities access to the file */
   ReleaseMutex( hMutex );
}
#else
void closeFile( FILE *str )
{
   fflush( str );
   fclose( str );
}
#endif

#ifdef _WINNT
FILE *openFile( char *fName, char *attributes )
{
   double  dw=0;
   FILE   *fp=NULL;
  
   hMutex = CreateMutex( NULL, FALSE, "dummyX" );
   if ( hMutex == NULL )
   {
      if ( GetLastError() != ERROR_ALREADY_EXISTS )
      {
         logError( __FILE__, __LINE__ );
         logit( "t", "fName = [%s]\n", fName );
         return NULL;
      }
      hMutex = OpenMutex( MUTEX_ALL_ACCESS, FALSE, "dummyX" );	 
   }

/* If another process has the file open, then we will queue here
   -- wait 2 seconds */
   WaitForSingleObject( hMutex, 2000 );
   if ( dw != WAIT_TIMEOUT )     
      fp = fopen( fName, attributes );     
   else
   {
      logError( __FILE__, __LINE__ ); 
      ReleaseMutex( hMutex );      
      return ( NULL );
   } 
   return ( fp );
}
#else
FILE *openFile( char *fName, char *attributes )
{
   FILE   *fp;
  
   fp = fopen( fName, attributes );     
   return ( fp );
}
#endif

      /******************************************************************
       * ReadDummyData()                         *
       * *
       * This function reads hypocenter parameters data from the        *
       * specified dummy file.                                          *
       * *
       * December, 2007: Combined with old program ReadDummyFile.       *
       * *
       * October, 2003: Read in lat lon as +/- geographic coord.        *
       * *
       * February, 2002: Added iNumMwp to read/write                    *
       * *
       * October, 2002: Added num magnitudes for each type              *
       * *
       * Arguments:                                                    *
       * pHypo            Computed hypocentral parameters             *
       * pszDumFile       Parameter file (dummy file) to update       *
       * *
       * Returns: 1->ok read, 0->no read                               *
       * *
       ******************************************************************/	   
int ReadDummyData( HYPO *pHypo, char *pszDumFile )
{
   char    cLat, cLon;
   double  dAzm;          /* Azimuthal coverage in degrees */
   double  dDum;    
   FILE    *hFile;        /* File handle */
   int     iDepth;        /* Quake depth from dummy file */
   int     iDum;
   int     iMonth, iYear; /* Origin dates */ 
   int     iOTimeRnd;     /* O-time rounded to nearest minute */
   static  LATLON  ll;    /* Epicentral geographic location */
   struct tm tm;          /* Origin time in structure */
    
/* Initialize the hypocenter structure */
   InitHypo( pHypo );        
    
/* Read Dummy File */
   if ( (hFile = openFile( pszDumFile, "r" )) != NULL )
      /* CORRECCION 64 BITS: %d en lugar de %ld para todos los int */
      fscanf( hFile, "%d %lf %lf %d %lf %s %d %d %d %d %d %d "
                     "%d %d %lf %d %lf %d %lf %lf %d %lf %d %lf %d %d "
                     "%lf %lf %lf %d %d",
       &pHypo->iBullNo, &pHypo->llEpiGG.dLat, &pHypo->llEpiGG.dLon, &iDepth,
       &pHypo->dPreferredMag, pHypo->szPMagType, &tm.tm_mday, &iMonth, &iYear,
       &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &iDum, &pHypo->iNumPMags,
       &pHypo->dMSAvg, &pHypo->iNumMS, &pHypo->dMwpAvg, &pHypo->iNumMwp,
       &dDum, &pHypo->dMbAvg, &pHypo->iNumMb, &pHypo->dMlAvg, &pHypo->iNumMl,
       &pHypo->dMwAvg, &pHypo->iNumMw, &pHypo->iNumPs, &pHypo->dNearestDist,
       &pHypo->dAvgRes, &dAzm, &pHypo->iQuakeID, &pHypo->iUpdateMap );
            
   if ( hFile == NULL )
   {
      logit( "et" , "Dummy file [%s] not opened %s @ %d\n", pszDumFile, __FILE__ , __LINE__);
      return 0;
   }
   closeFile( hFile );

   if ( fabs( pHypo->llEpiGG.dLat ) > 90. )
   {
      logit( "t", "Bad lat read from dummy file = %lf\n", pHypo->llEpiGG.dLat );
      /* CORRECCION 64 BITS: %d en lugar de %ld */
      logit( "", "%d %lf %d %lf %s %d %d %d %d %d %d "
                 "%d %d %lf %d %lf %d %lf %lf %d %lf %d %lf %d %d "
                 "%lf %lf %lf %d %d",
        pHypo->iBullNo, pHypo->llEpiGG.dLon, iDepth, pHypo->dPreferredMag,
        pHypo->szPMagType, tm.tm_mday, iMonth, iYear,
        tm.tm_hour, tm.tm_min, tm.tm_sec, iDum, pHypo->iNumPMags,
        pHypo->dMSAvg, pHypo->iNumMS, pHypo->dMwpAvg, pHypo->iNumMwp,
        dDum, pHypo->dMbAvg, pHypo->iNumMb, pHypo->dMlAvg, pHypo->iNumMl,
        pHypo->dMwAvg, pHypo->iNumMw, pHypo->iNumPs, pHypo->dNearestDist,
        pHypo->dAvgRes, dAzm, iDum, pHypo->iUpdateMap );
      pHypo->llEpiGG.dLat = 45.;
   }
   dDum = pHypo->llEpiGG.dLon;
   if ( dDum > 180. && dDum < 360. ) dDum -= 360.;
   if ( fabs( dDum ) > 180. )
   {
      logit( "t", "Bad lon read from dummy file = %lf\n", pHypo->llEpiGG.dLon );
      /* CORRECCION 64 BITS: %d en lugar de %ld */
      logit( "", "%d %lf %d %lf %s %d %d %d %d %d %d "
                 "%d %d %lf %d %lf %d %lf %lf %d %lf %d %lf %d %d "
                 "%lf %lf %lf %d %d",
        pHypo->iBullNo, pHypo->llEpiGG.dLat, iDepth, pHypo->dPreferredMag,
        pHypo->szPMagType, tm.tm_mday, iMonth, iYear,
        tm.tm_hour, tm.tm_min, tm.tm_sec, iDum, pHypo->iNumPMags,
        pHypo->dMSAvg, pHypo->iNumMS, pHypo->dMwpAvg, pHypo->iNumMwp,
        dDum, pHypo->dMbAvg, pHypo->iNumMb, pHypo->dMlAvg, pHypo->iNumMl,
        pHypo->dMwAvg, pHypo->iNumMw, pHypo->iNumPs, pHypo->dNearestDist,
        pHypo->dAvgRes, dAzm, iDum, pHypo->iUpdateMap );
      pHypo->llEpiGG.dLon = 45.;
   }
   
/* Put lat/lon in geocentric form */
   pHypo->dLat = pHypo->llEpiGG.dLat;
   pHypo->dLon = pHypo->llEpiGG.dLon;
   GeoCent ((LATLON *) pHypo);
   GetLatLonTrig( (LATLON *) pHypo );
   
/* Convert location to character coords */
   ConvertLoc( &pHypo->llEpiGG, &ll, &cLat, &cLon );
   sprintf( pHypo->szLat, "%5.1lf%c", ll.dLat, cLat );
   sprintf( pHypo->szLon, "%6.1lf%c", ll.dLon, cLon ); 

/* Convert origin time to 1/1/70 seconds */   
   tm.tm_mon = iMonth - 1;
   tm.tm_year = iYear - 1900;
   tm.tm_isdst = 0;
   pHypo->dOriginTime = (double) mktime( &tm );

/* Fill SYSTEMTIME structure and get origin time in Mod. Julian seconds */
   pHypo->stOTime.wYear = iYear;
   pHypo->stOTime.wMonth = iMonth;
   pHypo->stOTime.wDay = tm.tm_mday;
   pHypo->stOTime.wHour = tm.tm_hour;
   pHypo->stOTime.wMinute = tm.tm_min;
   pHypo->stOTime.wSecond = tm.tm_sec;
   pHypo->stOTime.wMilliseconds = 0;
   
/* Get O-time rounded to minute for messages */
   NewDateFromModSecRounded( &pHypo->stOTimeRnd,
                              pHypo->dOriginTime );
   iOTimeRnd = pHypo->stOTimeRnd.wHour*100 + pHypo->stOTimeRnd.wMinute;
   
   /* CORRECCION LINUX: Reemplazamos _itoa (exclusivo de Windows) por sprintf (Universal) */
#ifdef _WIN32
   _itoa( iOTimeRnd, pHypo->szOTimeRnd, 10 ); 
#else
   sprintf(pHypo->szOTimeRnd, "%d", iOTimeRnd);
#endif
   
   PadZeroes( 4, pHypo->szOTimeRnd );      /* Add leading zeros */
   
/* Convert a few other items */   
   pHypo->iAzm = (int) dAzm;
   pHypo->dDepth = (double) iDepth;
   if (pHypo->dDepth > 750.) pHypo->dDepth = 750.;
   if (pHypo->dDepth < 0.)   pHypo->dDepth = 0.;
   return 1;
}

      /******************************************************************
       * ReadPTimeFile()                            *
       * *
       * This function reads P-time and magnitude information           *
       * created by loc_wcatwc after a location is made.                *
       * *
       * Arguments:                                                    *
       * piNumP           Number of data lines read                   *
       * pPBuf            P-pick buffer                               *
       * pszPTimeFile     P-time file created in loc_wcatwc           *
       * iPMax            Maximum number of P-times to read           *
       * *
       * Returns: 1->ok read, 0->no read                               *
       * *
       ******************************************************************/	   
int ReadPTimeFile( int *piNumP, PPICK *pPBuf, char *pszPTimeFile,
                   int iPMax )
{
   FILE    *hFile;          /* File handle */
   int     iCnt;    

   iCnt = 0;
   *piNumP = 0;
Open:
   if ( (hFile = fopen( pszPTimeFile, "r" )) != NULL )
   {
      while ( !feof( hFile ) )    /* Read till end of file */
      {
         /* CORRECCION 64 BITS: %d en lugar de %ld para iUseMe */
         if (fscanf( hFile, "%s %s %s %lf %s %lf %lf %lf %lf %lf %lf %lf %lf %lf "
                        "%lf %lf %lf %d %ld %c %lf %lf %lf %lf\n",
          pPBuf[*piNumP].szStation, pPBuf[*piNumP].szChannel,
          pPBuf[*piNumP].szNetID, &pPBuf[*piNumP].dPTime, 
          pPBuf[*piNumP].szPhase, &pPBuf[*piNumP].dMbAmpGM,
          &pPBuf[*piNumP].dMbPer, &pPBuf[*piNumP].dMbTime,
          &pPBuf[*piNumP].dMlAmpGM, &pPBuf[*piNumP].dMlPer,
          &pPBuf[*piNumP].dMlTime, &pPBuf[*piNumP].dMSAmpGM,
          &pPBuf[*piNumP].dMSPer, &pPBuf[*piNumP].dMSTime,
          &pPBuf[*piNumP].dMwpIntDisp, &pPBuf[*piNumP].dMwpTime,
          &pPBuf[*piNumP].dRes, &pPBuf[*piNumP].iUseMe,
          &pPBuf[*piNumP].lPickIndex, &pPBuf[*piNumP].cFirstMotion,
          &pPBuf[*piNumP].dMbMag, &pPBuf[*piNumP].dMlMag,
          &pPBuf[*piNumP].dMSMag, &pPBuf[*piNumP].dMwpMag ) != 24) {
              break;
          }

/* See if this is a P or just a Phase */
         if ( pPBuf[*piNumP].dPTime > 0. )
/* Use  same logic as in ANALYZE  */
            if ( ((pPBuf[*piNumP].szPhase[1] == 'P' || 
                   pPBuf[*piNumP].szPhase[1] == 'p') &&
                   strlen( pPBuf[*piNumP].szPhase ) <= 3) ||
                 (pPBuf[*piNumP].szPhase[1] == '(' &&
                 (pPBuf[*piNumP].szPhase[2] == 'P' || 
                  pPBuf[*piNumP].szPhase[2] == 'p')) ||
                 (strlen( pPBuf[*piNumP].szPhase ) == 1 &&
                  pPBuf[*piNumP].szPhase[0] == 'P') )
            {
               ;             /* It is a 1st arrival */
            }
            else
            {                /* It is a phase */
               pPBuf[*piNumP].dPhaseTime = pPBuf[*piNumP].dPTime;
               pPBuf[*piNumP].dPTime = 0.;
            }
         *piNumP += 1;
         if ( *piNumP >= iPMax ) break;
      }
      fclose( hFile );
   }
   else                     /* Couldn't open file */
   {
      if ( iCnt == 5 )      /* Quit trying */
      {
         logit ("t", "RTPFile (%s) could not be opened; 5X\n", pszPTimeFile );
         return ( 0 );
      }
      iCnt++;            /* Try again in 0.1 seconds */
      sleep_ew( 100 );
      goto Open;
   }
   return ( 1 );
}

      /******************************************************************
       * WriteDummyData()                         *
       * *
       * This function writes hypocenter parameters data to the         *
       * specified dummy file.  It also updates the file used by the    *
       * GIS (pszMapFile).                                              *
       * *
       * December, 2007: Combined with WriteDummyFile in \seismic.      *
       * October, 2003: Write lat lon as +/- geographic coord.          *
       * February, 2002: Added iNumMwp to read/write                    *
       * October, 2002: Added num magnitudes for each type              *
       * *
       * Arguments:                                                    *
       * pHypo            Computed hypocentral parameters             *
       * pszDumFile       Parameter file (dummy file) to update       *
       * iUpdate          iUpdate=1 => force a new map creation in EV *
       * iGetBull         Read in Bulletin number and quake ID first  *
       * *
       * Returns: 1->ok write, 0->no write                             *
       * *
       ******************************************************************/
	   
int WriteDummyData( HYPO *pHypo, char *pszDumFile, int iUpdate, int iGetBull )
{
   double  dAvgRes;     /* Average residual */
   double  dDum;
   FILE    *hFile;      /* File handle */
   static  int     iBullNo;     /* Bulletin number from dummy file */
   static  int     iQuakeID;    /* Quake ID from dummy file */
   int     iDum;
   static  LATLON ll;   /* Epicentral geographic location */
   long    lTime;       /* 1/1/70 time */
   char    szDum[8];
   struct  tm *tm;      /* Origin time in structure */
      
/* Get epicenter lat/lon in geographic coordinates */
   GeoGraphic( &ll, (LATLON *) pHypo );
   if ( ll.dLon > 180. ) ll.dLon -= 360.;
   
/* Read in bulletin number and quake ID from existing dummy file */   
   if ( iGetBull == 1 )          /* Only read in if told to */
   {
      hFile = openFile( pszDumFile, "r" );     // jmc
      if ( hFile != NULL )           /* Read bulletin number */
         /* CORRECCION 64 BITS: %d en lugar de %ld */
         fscanf( hFile, "%d %lf %lf %d %lf %s %d %d %d %d %d %d "
                        "%d %d %lf %d %lf %d %lf %lf %d %lf %d %lf %d %d "
                        "%lf %lf %lf %d %d",
          &iBullNo, &dDum, &dDum, &iDum, &dDum, szDum, &iDum, &iDum, &iDum,
          &iDum, &iDum, &iDum, &iDum, &iDum, &dDum, &iDum, &dDum, &iDum,
          &dDum, &dDum, &iDum, &dDum, &iDum, &dDum, &iDum, &iDum, &dDum,
          &dDum, &dDum, &iQuakeID, &iDum );
      else  
      {
         logit( "t" , "Dummy file not opened %s @ %d\n", __FILE__, __LINE__ );
         return 0;
      }
      closeFile( hFile );
   }
   else
   {
      iBullNo = pHypo->iBullNo;
      iQuakeID = pHypo->iQuakeID;
   }

/* Set max average residual */
   dAvgRes = pHypo->dAvgRes;
   if ( dAvgRes > 99.99 ) dAvgRes = 99.99;

/* Get time rounded to nearest second */
   lTime = (long) (pHypo->dOriginTime+0.5);
   tm = TWCgmtime( lTime );
   
/* Update Dummy File */
   /* CORRECCION 64 BITS: %d en lugar de %ld */
   logit( "t", "%d %7.3lf %8.3lf %d %3.1lf %s %d %d %d %d %d %d %d %d "
       "%3.1lf %d %3.1lf %d %3.1lf %3.1lf %d %3.1lf %d %3.1lf %d %d "
       "%7.3lf %5.2lf %5.1lf %d %d\n",
       iBullNo, ll.dLat, ll.dLon, (int) (pHypo->dDepth + 0.5),
       pHypo->dPreferredMag, pHypo->szPMagType, tm->tm_mday, tm->tm_mon+1,
       tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec, 0,
       pHypo->iNumPMags, pHypo->dMSAvg, pHypo->iNumMS, pHypo->dMwpAvg,
       pHypo->iNumMwp, 0., pHypo->dMbAvg, pHypo->iNumMb, pHypo->dMlAvg,
       pHypo->iNumMl, pHypo->dMwAvg, pHypo->iNumMw, pHypo->iNumPs,
       pHypo->dNearestDist, dAvgRes, (double) pHypo->iAzm, iQuakeID, iUpdate );
       
   if ( (hFile = openFile( pszDumFile, "w" )) != NULL )
   {
#ifdef _WINNT
      TCHAR  chReadBuf[BUFSIZE]; 
      DWORD  cbRead; 
      LPTSTR lpszPipename = lpszQuakeEWPipe; 
      HANDLE hPipe=NULL; 
#endif
     
      fprintf( hFile, "%d %7.3lf %8.3lf %d %3.1lf %s %d %d %d %d %d %d %d %d "
       "%3.1lf %d %3.1lf %d %3.1lf %3.1lf %d %3.1lf %d %3.1lf %d %d "
       "%7.3lf %5.2lf %5.1lf %d %d\n",
       iBullNo, ll.dLat, ll.dLon, (int) (pHypo->dDepth + 0.5),
       pHypo->dPreferredMag, pHypo->szPMagType, tm->tm_mday, tm->tm_mon+1,
       tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec, 0,
       pHypo->iNumPMags, pHypo->dMSAvg, pHypo->iNumMS, pHypo->dMwpAvg,
       pHypo->iNumMwp, 0., pHypo->dMbAvg, pHypo->iNumMb, pHypo->dMlAvg,
       pHypo->iNumMl, pHypo->dMwAvg, pHypo->iNumMw, pHypo->iNumPs,
       pHypo->dNearestDist, dAvgRes, (double) pHypo->iAzm, iQuakeID, iUpdate );


#ifdef _WINNT
      if ( iUpdate == 1 )
      {
         int tInst;
         for ( tInst=0; tInst<totPipeInst; tInst++ )
         {
            hPipe=CreateFile( lpszPipename, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, 0, NULL );      
            strcpy( chReadBuf, "update" );
            WriteFile( hPipe, chReadBuf, strlen( chReadBuf ), &cbRead, NULL );
            FlushFileBuffers( hPipe ); 
            CloseHandle( hPipe ); 
         }
      }	
#endif
   }
   else                         /* Try again in .1 sec if busy */
   {
      logit( "et" , "Dummy file not opened %s @ %d\n", __FILE__, __LINE__ );
      return ( 0 );
   }
   closeFile( hFile );
   return 1;
}

      /******************************************************************
       * WriteLPDataFile()                      *
       * *
       * This function writes the long period data in to a file in a    *
       * format suitable for locate.c.                                  *
       * *
       * Arguments:                                                    *
       * pszLPFile        LP data file to write to                    *
       * iNumSta          Number of Stations in this structure array  *
       * P                P Buffer array of structures to output      *
       * *
       * Return:            int      1->normal; 0-> problem            *
       * *
       ******************************************************************/
int WriteLPDataFile( char *pszLPFile, int iNumSta, PPICK P[] )
{
   FILE   *hFile;             /* File handle */
   int     i;                 /* Counter */
        
   hFile = fopen (pszLPFile, "w");
   if ( hFile == NULL )       /* Couldn't find directory, most likely */
   {
      logit( "t", "LPFile not written in WriteLPDataFile - %s\n", pszLPFile );
      return 0;
   }
   for ( i=0; i<iNumSta; i++ )  
      if ( P[i].iUseMe > 0 )
         if ( P[i].dMSAmpGM > 0. )
            fprintf( hFile, "%s %s %s %lf %s %lf %lf %lf %lf %lf %lf %E %lf\n",
             P[i].szStation, P[i].szChannel, P[i].szNetID, P[i].dPTime,
             P[i].szPhase, P[i].dMbAmpGM, P[i].dMbPer, P[i].dMlAmpGM,
             P[i].dMlPer, P[i].dMSAmpGM, P[i].dMSPer, P[i].dMwpIntDisp,
             P[i].dMwpTime );
   fclose( hFile );
   return 1;
}

      /******************************************************************
       * WritePTimeFile()                      *
       * *
       * This function writes the P and magnitude data to a file.       *
       * *
       * Arguments:                                                    *
       * iPNum            Number of Ps in this structure              *
       * P                One of the P Buffer structures              *
       * pszFile          Output data file name                       *
       * *
       ******************************************************************/
	   
void WritePTimeFile( int iNumP, PPICK P[], char *pszFile )
{
   FILE    *hFile;                   /* File handle */
   int     i;

/* Open pick file */        
   if ( (hFile = fopen( pszFile, "w" )) == NULL )
   {
      logit ("t", "Pick file, %s, not opened for write (1).\n", pszFile);
      return;
   }
   
/* Dump the picks and magnitude data to disk */   
   for ( i=0; i<iNumP; i++ )  
      if ( P[i].iUseMe > 0 )
         /* CORRECCION 64 BITS: %d para iUseMe */
         fprintf( hFile, "%s %s %s %lf %s %lf %lf %lf %lf %lf %lf %lf %lf %lf "
                         "%E %lf %lf %d %ld %c %lf %lf %lf %lf\n",
          P[i].szStation, P[i].szChannel, P[i].szNetID, P[i].dPTime,
          P[i].szPhase,   
          P[i].dMbAmpGM, P[i].dMbPer, P[i].dMbTime, 
          P[i].dMlAmpGM, P[i].dMlPer, P[i].dMlTime, 
          P[i].dMSAmpGM, P[i].dMSPer, P[i].dMSTime, 
          P[i].dMwpIntDisp, P[i].dMwpTime, P[i].dRes, P[i].iUseMe,
          P[i].lPickIndex, P[i].cFirstMotion, P[i].dMbMag, P[i].dMlMag,
          P[i].dMSMag, P[i].dMwpMag );

   fclose( hFile );                  
}
