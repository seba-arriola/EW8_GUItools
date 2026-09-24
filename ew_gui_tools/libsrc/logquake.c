/************************************************************************ 
 * * LOGQUAKE.C                                                           *
  * *
  * These functions write quake locations and magnitudes to a log file.  *
  * Detailed data is logged with all P-picks, magnitudes, etc.           *
  * *
  * Made into earthworm module 2/2001.                                   *
  * *
  ************************************************************************/
  
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <earthworm.h>
#include <transport.h>
#include "earlybirdlib.h"

#ifdef WCATWC
   #include "my_global.h"  /* JMC 4.12.2008 for PH */
   #include "mysql.h"
#endif


      /******************************************************************
       * LogToMySQL()                          *
       * *
       * This function writes event information to a MySQL data base.   *
       * *
       * Arguments:                                                    *
       * pHypo          Hypocenter parameters                         *
       * pszIndexFile   FE Region index file                          *
       * pszLatFile     FE Region latitude file                       *
       * pszNameFileLC  FE Region geographic names - lower case       *
       * pszServer      MySQL data base server ip address             *
       * pszUser        MySQL data base user name                     *
       * pszPwd         MySQL data base password                      *
       * pszDB          MySQL data base name                          *
       * iMessageType   Message type from message2                    *
       * lMessageIssueTime Product issue time in 1/1/70 seconds       *
       * iMessageFlag   1->Entry relates to product, 0->Archival      *
       * pszComments    Comments to add to archival                   *
       * pszArchFile    Archival file name                            *
       * *
       ******************************************************************/      
void LogToMySQL( HYPO *pHypo, char *pszIndexFile,
                 char *pszLatFile, char *pszNameFileLC,
                 char *pszServer, char *pszUser, char *pszPwd, char *pszDB,
                 int iMessageType, long lMessageIssueTime, int iMessageFlag,
                 char *pszComments, char *pszArchFile )
{
#ifdef WCATWC   // JMC 4.12.2008 for PH
   MYSQL  *conn;
   int     iFERegion;           /* Flinn-Engdahl Region number */
   int     iRegion;             /* Procedural region number */
   LATLON  ll;                  /* Geographic epicenter coordinates */
   char    *psz;                /* Epicentral region */
   char    szInsertStmt[800];
   struct tm *tm;               /* C Time structure */

/* Convert epicenter coords from geocentric to geographic */
   GeoGraphic( &ll, (LATLON *) pHypo );         /* Quake lat/lon - ll.dLat,...*/
   if ( ll.dLon > 180.0 && ll.dLon < 360.0 ) ll.dLon -= 360.0;
   psz = namnumLC( ll.dLat, ll.dLon, &iFERegion, pszIndexFile, pszLatFile,
                   pszNameFileLC );
   psz[strlen( psz )-1] = '\0';                 /* Quake Region - "Kuril Is." */                      
   iRegion = GetRegion( ll.dLat, ll.dLon );     /* Quake Region - 1-20        */   
   tm = TWCgmtime( (long) (pHypo->dOriginTime+0.5) ); /* Origin Time - hr,... */

/* Connect to data base */
   conn = mysql_init( NULL ); 
   if ( !mysql_real_connect( conn, pszServer, pszUser, pszPwd, pszDB, 0,
                             NULL, 0 ) ) 
      logit( "t", "%s\n", mysql_error( conn ) );  
   else
   {
      sprintf( szInsertStmt, 
       "INSERT INTO WCATWCEvents (Latitude,Longitude,Depth,PreferredMag,MagType,"
       "Day,Month,Year,Hour,Minute,Second,NumPMags,MSAvg,NumMS,MwpAvg,NumMwp,MbAvg,NumMb,"
       "MlAvg,NumMl,MwAvg,NumMw,NumPs,NearestDist,AvgRes,Azm,QuakeID,OTime,"
       "MessageType,BullNo,MessageIssueTime,MessageFlag,"
       "ProcedureRegion,GeographicRegion,Comments,File) values ("
       "%7.3lf, %8.3lf, %ld, %3.1lf, '%s', %d, %d, %d, %d, %d, %d, "
       "%ld, %3.1lf, %ld, %3.1lf, %ld, %3.1lf, %ld, %3.1lf, %ld, %3.1lf, %ld, "
       "%ld, %7.3lf, %5.2lf, %5.1lf, %ld, %ld, %ld, %ld, %ld, %ld, %ld, "
       "'%s', '%s', '%s')",
       ll.dLat, ll.dLon, (long) (pHypo->dDepth + 0.5),
       pHypo->dPreferredMag, pHypo->szPMagType, tm->tm_mday, tm->tm_mon+1,
       tm->tm_year+1900, tm->tm_hour, tm->tm_min, tm->tm_sec, 
       (long)pHypo->iNumPMags, pHypo->dMSAvg, (long)pHypo->iNumMS, pHypo->dMwpAvg,
       (long)pHypo->iNumMwp, pHypo->dMbAvg, (long)pHypo->iNumMb, pHypo->dMlAvg,
       (long)pHypo->iNumMl, pHypo->dMwAvg, (long)pHypo->iNumMw, (long)pHypo->iNumPs,
       pHypo->dNearestDist, pHypo->dAvgRes, (double) pHypo->iAzm,
       (long)pHypo->iQuakeID, (long) pHypo->dOriginTime, (long)iMessageType, (long)pHypo->iBullNo,
       lMessageIssueTime, (long)iMessageFlag, (long)iRegion, psz, pszComments,
       pszArchFile);
       
/* Send insert statement */
       if ( mysql_query( conn, szInsertStmt ) != 0 ) 
          logit( "t", "%s\n", mysql_error( conn ) );

       mysql_close( conn );
   } 
#endif 
}
                  
      /******************************************************************
       * MagnitudeLog()                          *
       * *
       * This function provides magnitude logging.  Magnitudes must have*
       * already been calculated.                                       *
       * *
       * Arguments:                                                    *
       * iPNum          Number of P-data                              *
       * P              Array of P-data structures                    *
       * pHypo          Hypocenter parameters                         *
       * *
       ******************************************************************/
	   
void MagnitudeLog( int iPNum, PPICK P[], HYPO *pHypo )
{
   int     i;

/* If there is no solution, print nothing and return */
   if ( pHypo->dLat == 0. && pHypo->dLon == 0. ) return;

/* Is there any magnitude information to output? */
   if ( !pHypo->iNumMb && !pHypo->iNumMl && !pHypo->iNumMS && !pHypo->iNumMwp )
   {
      logit("", " No Magnitude Information for this event\n");
      logit("", "\n"
       "----------------------------------------------------------------------------\n");
   }
   else                  /* There are some magnitudes to print */
   {
      logit( "",
       "                              MAGNITUDE DATA\n"
       "              ----------------------------------------------\n" );
      logit( "", 
       "                STA  PER     AMP     DIST    Ml   Mb   MS  Mwp\n" );
      logit( "", "              ----  ---  ---------  ----  ---  ---  ---  ---\n" );
      for ( i=0; i<iPNum; i++ )
         if ( (P[i].dMbMag || P[i].dMlMag || P[i].dMSMag || P[i].dMwpMag) &&
	       P[i].iUseMe > 0 )
         {                      /* This station has a magnitude */
            logit( "", "              %4s", P[i].szStation );/* Station Name */
            if ( P[i].dMSMag )                              /* Period */
               logit( "", "%5.1f", P[i].dMSPer );
            else if (P[i].dMlMag )
               logit( "", "%5.1f", P[i].dMlPer );
            else if (P[i].dMbMag )
               logit( "", "%5.1f", P[i].dMbPer );
            else if (P[i].dMwpMag )
	       logit( "", "     " );
            if ( P[i].dMSMag )                              /* Amplitude */
               logit( "", "%9.1f", P[i].dMSAmpGM );
            else if ( P[i].dMlMag )
               logit( "", "%9.1f", P[i].dMlAmpGM );
            else if ( P[i].dMbMag )
               logit( "", "%9.1f", P[i].dMbAmpGM );
            else if ( P[i].dMwpMag )
               logit( "", "         " );
            logit( "", "%8.1f", P[i].dDelta );              /* Distance */
            if ( P[i].dMlMag )                              /* Magnitude */
            {
               logit( "", "%5.1f", P[i].dMlMag );
               if ( P[i].iMlClip ) logit( "", "C      " );
               else                logit( "", "       " );
            }
            else if ( P[i].dMbMag )
            {
               logit( "", "     %5.1f", P[i].dMbMag );
               if ( P[i].iMbClip ) logit( "", "C " );
               else                logit( "", "  " );
            }
            else
               logit( "", "             " );
            if ( P[i].dMSMag )
            {
               logit( "", "%3.1f", P[i].dMSMag );
               if ( P[i].iMSClip ) logit( "", "C " );
               else                logit( "", "  " );
            }
            else
               logit( "", "     " );
            if ( P[i].dMwpMag )
               logit( "", "%3.1f  ", P[i].dMwpMag );
            else
               logit( "", "     " );
            logit( "", "\n" );
         }
	
/* Output the modified averages (not including those 0.6 away from norm) */
      if ( pHypo->iNumMS || pHypo->iNumMl || pHypo->iNumMb || pHypo->iNumMw ||
           pHypo->iNumMwp )
      {
         logit( "",
                 "              ---------------------------------------------------\n"
                 "                                MODIFIED AVERAGES\n" );
         if ( pHypo->iNumMS )
            logit( "", "                         MS =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMSAvg, pHypo->iNumMS );
         if ( pHypo->iNumMwp >= 3 )
            logit( "", "                         Mwp=%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMwpAvg, pHypo->iNumMwp );
         else if ( pHypo->iNumMwp >= 1 )
            logit( "", "                         Mwp=%4.1f?; %d STATION AVERAGE\n",
                   pHypo->dMwpAvg, pHypo->iNumMwp );
         if ( pHypo->iNumMb )
            logit( "", "                         Mb =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMbAvg, pHypo->iNumMb );
         if ( pHypo->iNumMl )
            logit( "", "                         Ml =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMlAvg, pHypo->iNumMl );
         if ( pHypo->iNumMw )
            logit( "", "                         Mw =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMwAvg, pHypo->iNumMw );
      }
		
/* End of output */
      logit( "", 
             "------------------------------------------------------------------------------\n\n" );
   }
}

      /******************************************************************
       * QuakeLog()                            *
       * *
       * This function writes the hypocenter parameters to a log file.  *
       * *
       * Arguments:                                                    *
       * iPNum          Number of P-data                              *
       * P              Array of P-data structures                    *
       * pHypo          Hypocenter parameters                         *
       * pcity          Array of reference cities                     *
       * pcityEC        Array of eastern reference cities             *
       * pszNameFile    FE Region geographic names                    *
       * pszNameFileLC  FE Region geographic names - lower case       *
       * pszIndexFile   FE Region index file                          *
       * pszLatFile     FE Region latitude file                       *
       * iType          1=write to log file, 2=write to other file    *
       * pszOutputFile  Other file to write to (or NULL)              *
       * *
       ******************************************************************/
       
void QuakeLog( int iPNum, PPICK P[], HYPO *pHypo, CITY *pcity, CITY *pcityEC,
               char *pszNameFile, char *pszNameFileLC, char *pszIndexFile,
               char *pszLatFile, int iType, char *pszOutputFile )
{
   char    cEW, cNS;            /* N, S, E, W character for lat/lon */
   CITYDIS CityDis;             /* Distance, direction from nearest cities */
   char    cTemp;               /* X if necessary for iUseMe */
   FILE   *hFile=NULL;          /* Output file - if used */
   int     i;
   int     iDepth;              /* Epicentral depth in km */
   int     iFERegion;           /* Flinn-Engdahl Region number */
   int     iNumUsed;            /* Number of stations used in location */
   int     iRegion;             /* Procedural region number */
   int     iTenths;             /* Tenths of seconds in P-time */
   LATLON  ll;                  /* Geographic epicenter coordinates */
   long    lTime,logTime;       /* 1/1/70 time in seconds */
   char    *psz;                /* Epicentral region */
   char    szHour[3], szMin[3], szSec[3];  /* P-time in string form */
   char    szTenth[3], szTenths[3];        /* Character string of tenths */
   struct tm *tm;               /* C Time structure */

/* If there is no solution, print nothing and return */
   if ( pHypo->dLat == 0. && pHypo->dLon == 0. ) return;   

/* Open output file */
   if ( iType == 2 ) 
      if ( (hFile = fopen( pszOutputFile, "a" )) == NULL )/* Couldn't open it */
      {
         logit( "t", "Sheet file could not be opened in QuakeLog\n" );
         return;
      }

/* Convert epicenter coords from geocentric to geographic */
   GeoGraphic( &ll, (LATLON *) pHypo );
   
/* Put lat/lon in form for output */
   cEW = 'E';
   cNS = 'N';
   if ( ll.dLon > 180.0 )
   {
      while ( ll.dLon > 180.0 ) ll.dLon -= 360.0;
      ll.dLon = fabs( ll.dLon );
      cEW = 'W';
   }
   if ( ll.dLat < 0.0 )
   {
      ll.dLat = fabs( ll.dLat );
      cNS = 'S';
   }
   
/* Convert depth to integer */
   iDepth = (int) (pHypo->dDepth + 0.5);

/* Get current date/time and write on top of output header */
   time( &lTime );
   logTime = lTime;                           // JMC 12.4.2008
   if ( iType == 1 )
   {
      /* CORRECCION 64 BITS: %d en lugar de %ld */
      logit( "", "\n%d          Quake %5d-%d - Solution at: %s", pHypo->iGoodSoln,
             pHypo->iQuakeID, pHypo->iVersion, asctime( TWCgmtime( lTime ) ) );
      logit( "", 
 "------------------------------------------------------------------------------\n"
 "         STA  P-TIME       RES   DIS   AZM    Ml    Mb    Mw    MS   Mwp\n"
 "         ---  ------       ---   ---   ---    ---   ---   ---   ---   ---\n" );
   }
   else 
   {
      /* CORRECCION 64 BITS: %d en lugar de %ld */
      fprintf( hFile, "\n%d          Quake %5d-%d - Solution at: %s",
               pHypo->iGoodSoln, pHypo->iQuakeID, pHypo->iVersion,
               asctime( TWCgmtime( lTime ) ) );
      fprintf( hFile, 
 "------------------------------------------------------------------------------\n"
 "         STA  P-TIME       RES   DIS   AZM    Ml    Mb    Mw    MS   Mwp\n"
 "         ---  ------       ---   ---   ---    ---   ---   ---   ---   ---\n" );
   }

/* Print out P-times, deltas, etc. */
   iNumUsed = 0;
   for ( i=0; i<iPNum; i++ )   /* Only print those with P-times */
      if ( P[i].dPTime > 0. && P[i].iUseMe > 0 ) iNumUsed++;
   for ( i=0; i<iPNum; i++ )
   {
      lTime = (long) (P[i].dPTime);
      tm = TWCgmtime( lTime );
      itoaX( tm->tm_hour, szHour );
      itoaX( tm->tm_min, szMin );
      itoaX( tm->tm_sec, szSec );
      PadZeroes( 2, szHour );
      PadZeroes( 2, szMin );
      PadZeroes( 2, szSec );
      iTenths = (int) (((P[i].dPTime-floor( P[i].dPTime ))*10.)+0.5);
      itoaX( iTenths, szTenth );
      strncpy( &szTenths[0], szTenth, 1 );
      strcpy( &szTenths[1], "\0" );
      cTemp = ' ';
      if ( P[i].iUseMe <= 0 ) cTemp = 'X';
      if ( iType == 1 )
         logit( "", "        %4s %2s%2s%2s.%1s%c %7.1f%6.1f%7.1f",
                P[i].szStation, szHour, szMin, szSec, szTenths, cTemp,
                P[i].dRes, P[i].dDelta, P[i].dAz );
      else
         fprintf( hFile, "        %4s %2s%2s%2s.%1s%c %7.1f%6.1f%7.1f",
                  P[i].szStation, szHour, szMin, szSec, szTenths, cTemp,
                  P[i].dRes, P[i].dDelta, P[i].dAz );
      if ( P[i].dMlMag )
      {
         if ( iType == 1 )
         {
            logit( "", "%5.1f", P[i].dMlMag );
            if ( P[i].iMlClip ) logit( "", "C         " );
            else                logit( "", "         " );
         }
         else
         {
            fprintf( hFile, "%5.1f", P[i].dMlMag );
            if ( P[i].iMlClip ) fprintf( hFile, "C         " );
            else                fprintf( hFile, "         " );
         }
      }
      else if ( P[i].dMbMag )
      {
         if ( iType == 1 )
         {
            logit( "", "      %5.1f", P[i].dMbMag );
            if ( P[i].iMbClip ) logit( "", "C  " );
            else                logit( "", "   " );
         }
         else
         {
            fprintf( hFile, "      %5.1f", P[i].dMbMag );
            if ( P[i].iMbClip ) fprintf( hFile, "C  " );
            else                fprintf( hFile, "   " );
         }
      }
      else
      {
         if ( iType == 1 )
         {   logit( "", "              " ); }
         else 
         {   fprintf( hFile, "              " ); }
      }
      if ( P[i].dMwMag > 0. )                 
      {
         if ( iType == 1 )
         {
            logit( "", "%3.1f", P[i].dMwMag );
            if ( P[i].iMwClip == 1 ) logit( "", "C  " );
            else                     logit( "", "   " );
         }
         else
         {
            fprintf( hFile, "%3.1f", P[i].dMwMag );
            if ( P[i].iMwClip == 1 ) fprintf( hFile, "C  " );
            else                     fprintf( hFile, "   " );
         }
      }
      else
      {
         if ( iType == 1 )
            logit( "", "      "  );
         else
            fprintf( hFile, "      " );
      }
      if ( P[i].dMSMag )
      {
         if ( iType == 1 )
         {
            logit( "", "%3.1f", P[i].dMSMag );
            if ( P[i].iMSClip ) logit( "", "C  " );
            else                logit( "", "   " );
         }
         else
         {
            fprintf( hFile, "%3.1f", P[i].dMSMag );
            if ( P[i].iMSClip ) fprintf( hFile, "C  " );
            else                fprintf( hFile, "   " );
         }
      }
      else
      {
         if ( iType == 1 )
            logit( "", "      " );
         else
            fprintf( hFile, "      " );
      }
      if ( iType == 1 )
      {
         if ( P[i].dMwpMag ) logit( "", "%3.1f  ", P[i].dMwpMag );
         else                logit( "", "      " );
         logit( "", "\n");
      }
      else
      {
         if ( P[i].dMwpMag ) fprintf( hFile, "%3.1f  ", P[i].dMwpMag );
         else                fprintf( hFile, "      " );
         fprintf( hFile, "\n");
      }
   }
   
/* Next print hypocenter parameters summary */
   if ( iType == 1 )
   {
      logit( "", "------------------------------------------------------------------------------\n" );
      lTime = (long) (pHypo->dOriginTime+0.5);
      logit( "", "LOC...=%5.1f%c%8.1f%c   DEP=%3d",
             ll.dLat, cNS, ll.dLon, cEW, iDepth );
      logit( "", "               H(Z)  =%s", asctime( TWCgmtime( lTime ) ) );
      logit( "", "                                            " );
   }
   else
   {
      fprintf( hFile, "------------------------------------------------------------------------------\n" );
      lTime = (long) (pHypo->dOriginTime+0.5);
      fprintf( hFile, "LOC...=%5.1f%c%8.1f%c   DEP=%3d",
             ll.dLat, cNS, ll.dLon, cEW, iDepth );
      fprintf( hFile, "               H(Z)  =%s", asctime( TWCgmtime( lTime ) ) );
      fprintf( hFile, "                                            " );
   }
	
/* Convert the O-time to local time and output */
   if ( IsDayLightSavings2( pHypo->dOriginTime, -9 ) ) /* In AK */
   {                                                   /* Daylight time */                
      lTime = (long) (pHypo->dOriginTime-28800.+0.5);
      tm = TWCgmtime( lTime );                         /* 8 hours UTC diff. */
      if ( iType == 1 )
         logit( "", "    H(AKDT)=%s", asctime( tm ) );
      else
         fprintf( hFile, "    H(AKDT)=%s", asctime( tm ) );
   }
   else	                                               /* Standard time */
   {
      lTime = (long) (pHypo->dOriginTime-32400.+0.5);
      tm = TWCgmtime( lTime );                         /* 9 hours UTC diff. */
      if ( iType == 1 )
         logit( "", "    H(AKST)=%s", asctime( tm ) );
      else
         fprintf( hFile, "    H(AKST)=%s", asctime( tm ) );
   }
   
   if ( pHypo->iGoodSoln > 0 )
   {
/* Find the nearest major and minor city to the epicenter */
      GeoGraphic( &ll, (LATLON *) pHypo );
      if ( ll.dLon < 0 ) ll.dLon += 360.;
      iRegion = GetRegion( ll.dLat, ll.dLon );
      if ( iRegion >= 10 )                  /* WC&ATWC Eastern AOR */
         NearestCitiesEC( (LATLON *) pHypo, pcityEC, &CityDis ); 
      else
         NearestCities( (LATLON *) pHypo, pcity, &CityDis );
	  
      if ( CityDis.iDis[1] < 320 || CityDis.iDis[0] < 320 )
/* Then, print nearest cities and azimuthal coverage */
      {
         if ( iType == 1 )
         {
            logit( "", "LOC...= %3d miles %2s of %s",
                   CityDis.iDis[0], CityDis.pszDir[0], CityDis.pszLoc[0] );
            for ( i=strlen( CityDis.pszLoc[0] ); i<35; i++ ) logit( "", " " );
            logit( "", "       AZM =    %3d\n", pHypo->iAzm );
         }
         else
         {
            fprintf( hFile, "LOC...= %3d miles %2s of %s",
                   CityDis.iDis[0], CityDis.pszDir[0], CityDis.pszLoc[0] );
            for ( i=strlen( CityDis.pszLoc[0] ); i<35; i++ )
               fprintf( hFile, " " );
            fprintf( hFile, "       AZM =    %3d\n", pHypo->iAzm );
         }
      }
      else             /* Otherwise, use General Area of... */
      {
         GeoGraphic( &ll, (LATLON *) pHypo );
         if ( ll.dLon > 180.0 && ll.dLon < 360.0 ) ll.dLon -= 360.0;
         psz = namnum( ll.dLat, ll.dLon, &iFERegion, pszIndexFile, pszLatFile,
                       pszNameFile );
         psz[strlen (psz)-1] = '\0';                      
         if ( iType == 1 )
         {
            logit( "", "LOC...= General Area of %s", psz );
            for ( i = strlen (psz); i<35; i++ ) logit( "", " ");
            logit( "", "       AZM =    %3d\n", pHypo->iAzm );
            logit( "", "                                            " );
            logit( "", "                       RES = %6.1f\n", pHypo->dAvgRes );
         }
         else
         {
            fprintf( hFile, "LOC...= General Area of %s", psz );
            for ( i = strlen (psz); i<35; i++ ) fprintf( hFile, " ");
            fprintf( hFile, "       AZM =    %3d\n", pHypo->iAzm );
            fprintf( hFile, "                                            " );
            fprintf( hFile, "                       RES = %6.1f\n",
                     pHypo->dAvgRes );
         }
      }
      
/* List distance from major city, also */
      if ( CityDis.iDis[1] < 320 || CityDis.iDis[0] < 320 )  /* if near */
         if ( strncmp( CityDis.pszLoc[1], CityDis.pszLoc[0], 27 ) )
         {
            if ( iType == 1 )
            {
               logit( "", "LOC...= %3d miles %2s of %s",
                        CityDis.iDis[1], CityDis.pszDir[1], CityDis.pszLoc[1] );
               for ( i=strlen( CityDis.pszLoc[1] ); i<35; i++ )logit( "", " " );
               logit( "", "       RES = %6.1f\n", pHypo->dAvgRes);
            }
            else
            {
               fprintf( hFile, "LOC...= %3d miles %2s of %s",
                        CityDis.iDis[1], CityDis.pszDir[1], CityDis.pszLoc[1] );
               for ( i=strlen( CityDis.pszLoc[1] ); i<35; i++ )
                  fprintf( hFile, " " );
               fprintf( hFile, "       RES = %6.1f\n", pHypo->dAvgRes);
            }
         }
   }
   
/* Print out final line */
   if ( iType == 1 )
      logit( "",
"------------------------------------------------------------------------------"
"\n" );
   else
      fprintf( hFile,
"------------------------------------------------------------------------------"
"\n" );
	
/* Output the modified averages (not including those 0.6 away from norm) */
   if ( pHypo->iNumMS || pHypo->iNumMl || pHypo->iNumMb || pHypo->iNumMw || 
        pHypo->iNumMwp )
   {
      if ( iType == 1 )
      {
         logit( "",
                 "                                MODIFIED AVERAGES\n" );
         if ( pHypo->iNumMS )
            logit( "", "                         MS =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMSAvg, pHypo->iNumMS );
         if ( pHypo->iNumMwp >= 3 )
            logit( "", "                         Mwp=%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMwpAvg, pHypo->iNumMwp );
         else if ( pHypo->iNumMwp >= 1 )
            logit( "", "                         Mwp=%4.1f?; %d STATION AVERAGE\n",
                   pHypo->dMwpAvg, pHypo->iNumMwp );	  
         if ( pHypo->iNumMb )
            logit( "", "                         Mb =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMbAvg, pHypo->iNumMb );
         if ( pHypo->iNumMl )
            logit( "", "                         Ml =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMlAvg, pHypo->iNumMl );
         if ( pHypo->iNumMw )
            logit( "", "                         Mw =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMwAvg, pHypo->iNumMw );
      }
      else
      {
         fprintf( hFile,
                 "                                MODIFIED AVERAGES\n" );
         if ( pHypo->iNumMS )
            fprintf( hFile, "                         MS =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMSAvg, pHypo->iNumMS );
         if ( pHypo->iNumMwp >= 3 )
            fprintf( hFile, "                         Mwp=%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMwpAvg, pHypo->iNumMwp );
         else if ( pHypo->iNumMwp >= 1 )
            fprintf( hFile, "                         Mwp=%4.1f?; %d STATION AVERAGE\n",
                   pHypo->dMwpAvg, pHypo->iNumMwp );	  
         if ( pHypo->iNumMb )
            fprintf( hFile, "                         Mb =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMbAvg, pHypo->iNumMb );
         if ( pHypo->iNumMl )
            fprintf( hFile, "                         Ml =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMlAvg, pHypo->iNumMl );
         if ( pHypo->iNumMw )
            fprintf( hFile, "                         Mw =%4.1f;  %d STATION AVERAGE\n",
                   pHypo->dMwAvg, pHypo->iNumMw );
      }
   }
		
/* End of output */
   if ( iType == 1 )
      logit( "", 
"------------------------------------------------------------------------------"
"\n\n" );
   else
      fprintf( hFile, 
"------------------------------------------------------------------------------"
"\n\n" );

   if ( iType == 2 ) fclose( hFile );
}


      /******************************************************************
       * QuakeLog2()                          *
       * *
       * This function writes the hypocenter parameters to a log file.  *
       * This file is appended and is an ongoing log of automatic       *
       * locations.                                                     *
       * *
       * Arguments:                                                    *
       * pszFile        File to add quake data                        *
       * pHypo          Hypocenter parameters                         *
       * *
       ******************************************************************/
       
void QuakeLog2( char *pszFile, HYPO *pHypo )
{
   FILE    *hFile;              /* File handle */
   int     iDepth;              /* Epicentral depth in km */
   LATLON  ll;                  /* Geographic epicenter coordinates */
   long    lTime;               /* 1/1/70 time in seconds */

/* If there is no solution, print nothing and return */
   if ( pHypo->dLat == 0. && pHypo->dLon == 0. ) return;   

/* Convert epicenter coords from geocentric to geographic */
   GeoGraphic( &ll, (LATLON *) pHypo );
   if (ll.dLon > 180.) ll.dLon -= 360.;
   
/* Convert depth to integer */
   iDepth = (int) (pHypo->dDepth + 0.5);
   
/* Open log file for append */   
   hFile = fopen( pszFile, "a" );
   if ( hFile == NULL )
   {
      logit( "t", "%s log file not opened\n", pszFile );
      return;
   }
   
/* Get current date/time and write first */
   time( &lTime );
   fprintf( hFile, "Solution at: %s", asctime( TWCgmtime( lTime ) ) );
   
/* Print hypocenter parameters summary */
   lTime = (long) (pHypo->dOriginTime+0.5);
   /* CORRECCION 64 BITS: %d en lugar de %ld */
   fprintf( hFile, "%d %d %.24s %lf %lf %d %d %lf %d\n", pHypo->iQuakeID,
          pHypo->iVersion, asctime( TWCgmtime( lTime ) ), ll.dLat, ll.dLon,
          iDepth, pHypo->iNumPs, pHypo->dAvgRes, pHypo->iAzm );
   fprintf( hFile, "      Mw=%lf-%d, Mwp=%lf-%d, MS=%lf-%d, Mb=%lf-%d, Ml=%lf-%d\n",
          pHypo->dMwAvg, pHypo->iNumMw, pHypo->dMwpAvg, pHypo->iNumMwp,
		  pHypo->dMSAvg, pHypo->iNumMS, pHypo->dMbAvg, pHypo->iNumMb,
		  pHypo->dMlAvg, pHypo->iNumMl );
   fclose( hFile );
}
