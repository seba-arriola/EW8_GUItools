/************************************************************************
  * DISKRW.C                                                             *
  ************************************************************************/
  
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <earthworm.h>
#include <transport.h>                
#include "earlybirdlib.h"

char *CreateFileName( double dTime, int iFileSize, char *pszRootDirectory,
                      char *pszSuffix )
{
   time_t    itime;         /* time (1/1/70) at start of file */   
   static char  szFile[128];/* Created file name to return (with directory) */
   char      szYear[4], szMon[4], szDay[4], szMonthUnit[2], szDayUnit[2],
             szHour[3], szMin[3];/* Minute, hour, etc. time units for file  */
   struct tm *tm;           /* time structure for the file name time */

   if ( iFileSize == 0 )
   {
      logit( "", "iFileSize = 0; INVALID !!!\n" );
      return NULL;
   }

   itime = (time_t) (floor( dTime+0.001 ) );
   tm = TWCgmtime( itime );
   
   if ( (tm->tm_min % iFileSize) != 0 )    
      tm = TWCgmtime( itime-((tm->tm_min%iFileSize)*60) );

   while ( tm->tm_year >= 100 ) tm->tm_year-=100;  
   itoaX( tm->tm_year, szYear ); 
   itoaX( tm->tm_mon+1, szMon );		
   itoaX( tm->tm_mday, szDay );		
   PadZeroes( 2, szYear );
   PadZeroes( 2, szMon );
   PadZeroes( 2, szDay );

   if      ( tm->tm_mon+1 == 10 ) strcpy( szMonthUnit, "A" ); 	
   else if ( tm->tm_mon+1 == 11 ) strcpy( szMonthUnit, "B" );
   else if ( tm->tm_mon+1 == 12 ) strcpy( szMonthUnit, "C" );
   else itoaX( tm->tm_mon+1, szMonthUnit );

   if      ( tm->tm_mday == 10 ) strcpy( szDayUnit, "A" );
   else if ( tm->tm_mday == 11 ) strcpy( szDayUnit, "B" );
   else if ( tm->tm_mday == 12 ) strcpy( szDayUnit, "C" );
   else if ( tm->tm_mday == 13 ) strcpy( szDayUnit, "D" );
   else if ( tm->tm_mday == 14 ) strcpy( szDayUnit, "E" );
   else if ( tm->tm_mday == 15 ) strcpy( szDayUnit, "F" );
   else if ( tm->tm_mday == 16 ) strcpy( szDayUnit, "G" );
   else if ( tm->tm_mday == 17 ) strcpy( szDayUnit, "H" );
   else if ( tm->tm_mday == 18 ) strcpy( szDayUnit, "I" );
   else if ( tm->tm_mday == 19 ) strcpy( szDayUnit, "J" );
   else if ( tm->tm_mday == 20 ) strcpy( szDayUnit, "K" );
   else if ( tm->tm_mday == 21 ) strcpy( szDayUnit, "L" );
   else if ( tm->tm_mday == 22 ) strcpy( szDayUnit, "M" );
   else if ( tm->tm_mday == 23 ) strcpy( szDayUnit, "N" );
   else if ( tm->tm_mday == 24 ) strcpy( szDayUnit, "O" );
   else if ( tm->tm_mday == 25 ) strcpy( szDayUnit, "P" );
   else if ( tm->tm_mday == 26 ) strcpy( szDayUnit, "Q" );
   else if ( tm->tm_mday == 27 ) strcpy( szDayUnit, "R" );
   else if ( tm->tm_mday == 28 ) strcpy( szDayUnit, "S" );
   else if ( tm->tm_mday == 29 ) strcpy( szDayUnit, "T" );
   else if ( tm->tm_mday == 30 ) strcpy( szDayUnit, "U" );
   else if ( tm->tm_mday == 31 ) strcpy( szDayUnit, "V" );
   else itoaX( tm->tm_mday, szDayUnit );

   itoaX( tm->tm_hour, szHour );
   itoaX( tm->tm_min, szMin );             
   PadZeroes( 2, szHour );
   PadZeroes( 2, szMin );

   strcpy( szFile, pszRootDirectory );   
   #ifdef _WIN32
      strcat( szFile, "\\D" );              
   #else
      strcat( szFile, "/D" );
   #endif
      strcat( szFile, szYear );
      strcat( szFile, szMon );
      strcat( szFile, szDay );
   #ifdef _WIN32
      strcat( szFile, "\\S" );              
   #else
      strcat( szFile, "/S" );
   #endif
   strcat( szFile, szMonthUnit );
   strcat( szFile, szDayUnit );
   strcat( szFile, szHour );
   strcat( szFile, szMin );
   strcat( szFile, pszSuffix );
   strcat( szFile, szYear );
   return szFile;
}

double GetTimeFromFileName (char *pszFile)
{
   int        iTemp;
   time_t     lTime;             /* Epochal time (1/1/70) */
   char       *psz;
   char       szYear[3], szMon[3], szDay[3], szHour[3], szMin[3];
   struct     tm *tm;            /* Time in structure format */

   psz = strrchr (pszFile, '\\');  
#ifndef _WIN32
   if (psz == NULL) psz = strrchr (pszFile, '/');
#endif

   iTemp = psz - pszFile;
   szYear[0] = pszFile[iTemp-6];   
   szYear[1] = pszFile[iTemp-5];
   szYear[2] = '\0';
   szMon[0] = pszFile[iTemp-4];
   szMon[1] = pszFile[iTemp-3];
   szMon[2] = '\0';
   szDay[0] = pszFile[iTemp-2];
   szDay[1] = pszFile[iTemp-1];
   szDay[2] = '\0';
   szHour[0] = pszFile[iTemp+4];   
   szHour[1] = pszFile[iTemp+5];
   szHour[2] = '\0';
   szMin[0] = pszFile[iTemp+6];
   szMin[1] = pszFile[iTemp+7];
   szMin[2] = '\0';
   
   lTime = 0;
   tm = TWCgmtime( lTime );
   tm->tm_isdst = 0;                         
   tm->tm_sec = 0;
   tm->tm_min = atoi( szMin );     
   tm->tm_hour = atoi( szHour );
   tm->tm_mday = atoi( szDay );
   tm->tm_mon = atoi( szMon ) - 1;
   tm->tm_year = atoi( szYear );
   if ( tm->tm_year < 90 ) tm->tm_year += 100;    
   else                    tm->tm_year += 0;
   lTime = mktime( tm );                          

   return ( (double) lTime );
}

int ReadDiskData( char *pszFile, STATION Sta[], int *piNumStas, 
                  int iScroll, int iLP )
{
   static  CHNLHEADER ch;     
   static  FILE *hFile;       
   static  DISKHEADER dh;     
   static  double dStartTime; 
   int     i, iTemp;
   static  int iBytePerSamp[MAX_STATIONS];   
   long    lIndexToStartWrite[MAX_STATIONS]; 
   LATLON  ll, llOut;
   long    lTemp;
   /* PARCHE 64 BITS: INT32_T EN LUGAR DE LONG PARA LEER 4 BYTES DIRECTO DEL DISCO */
   static  int32_t lBuff[CIRC_BUFFER_SIZE];     

   if ( (hFile = fopen( pszFile, "rb" )) == NULL ) 
   {
      logit( "t", "File %s not opened in ReadDiskData\n", pszFile );
      return 0;
   }

   if ( fread( &dh, sizeof( DISKHEADER ), 1, hFile ) < 1 )
   {
      fclose( hFile );
      logit( "t", "DISKHEADER read failed in file %s ReadDiskData\n", pszFile );
      return 0;
   }
   
   dStartTime = DateToModJulianSec( dh.stStartTime ) - 3506630400.;
   if ( dStartTime < 100. )             
   {
      fclose( hFile );
      logit( "t", "dStartTime incorrect (%lf), %s\n", dStartTime, pszFile );
      return 0;
   }
   if ( dh.iNumChans > MAX_STATIONS )    
   {
      fclose( hFile );
      logit( "t", "Too many data channels (%ld), %s\n", dh.iNumChans, pszFile );
      return 0;
   }
  
   for ( i=0; i<dh.iNumChans; i++ )
   {
      if ( (int) fread( &ch, dh.iChnHdrSize, 1, hFile ) < 1 ) 
      {
         fclose( hFile );
         logit( "t", "CHNLHEADER read failed, file %s ReadDiskData\n", pszFile);
         return 0;
      }
      Sta[i].lSampsInLastPacket = ch.lNumSamps;
      if ( iScroll == 0 )        
      {
         strcpy( Sta[i].szStation, ch.szStation );
         strcpy( Sta[i].szChannel, ch.szChannel );
         strcpy( Sta[i].szNetID, ch.szNetID );
         Sta[i].dSampRate = ch.dSampRate;
         ll.dLat = ch.dLat;   
         ll.dLon = ch.dLon;   
         if ( DateToModJulianSec( dh.stStartTime ) < 4488393600. )
         {
            GeoGraphic( &llOut, &ll );
            Sta[i].dLat = llOut.dLat;
            Sta[i].dLon = llOut.dLon;
            Sta[i].dElevation = ch.dElevation*EARTHRAD;
         }
         else
         {
            Sta[i].dLat = ll.dLat;
            Sta[i].dLon = ll.dLon;                
            Sta[i].dElevation = ch.dElevation;
         }
         Sta[i].dSens = ch.dGain;
         Sta[i].dGainCalibration = ch.dGainCalibration;
         Sta[i].dClipLevel = ch.dClipLevel;
         Sta[i].dTimeCorrection = ch.dTimeCorrection;
         Sta[i].iStationType = ch.iStationType;
         Sta[i].iSignalToNoise = ch.iSignalToNoise;
         Sta[i].iTrigger = ch.iTrigger;
         iBytePerSamp[i] = ch.iBytePerSamp;
         Sta[i].dScaleFactor = ch.dScaleFactor;  
         if ( Sta[i].dSampRate < 3. && Sta[i].dScaleFactor > 0.005 &&
              dStartTime+3506630400. < 4477766400. )
            Sta[i].dScaleFactor = 0.005;
         Sta[i].iDisplayStatus = 1;
      }

      if ( iScroll == 0 )  
      {
         CopyDate( &dh.stStartTime, &Sta[i].stStartTime );
         Sta[i].dStartTime = dStartTime;
         Sta[i].dEndTime = Sta[i].dStartTime + 
	  ((double) (Sta[i].lSampsInLastPacket-1)/Sta[i].dSampRate);
         Sta[i].lIndex = 0;              
         lIndexToStartWrite[i] = 0;
         Sta[i].iHasWrapped = 0;
      }
      else if ( dStartTime < Sta[i].dStartTime )
      {               
         Sta[i].lIndex -= (long) ((Sta[i].dStartTime - 
                                   dStartTime) * Sta[i].dSampRate + 0.0001);
         if ( Sta[i].lIndex < 0 ) 
         {
            Sta[i].iHasWrapped = 1;
            Sta[i].lIndex += CIRC_BUFFER_SIZE;
         }
         CopyDate( &dh.stStartTime, &Sta[i].stStartTime );
         Sta[i].dStartTime = dStartTime;
         lIndexToStartWrite[i] = Sta[i].lIndex;
         lTemp = (long) ((Sta[i].dEndTime-Sta[i].dStartTime)
                  * Sta[i].dSampRate + 0.001) + 1;
         if ( lTemp > CIRC_BUFFER_SIZE ) Sta[i].dEndTime -= 
             (double) (lTemp-CIRC_BUFFER_SIZE) / Sta[i].dSampRate;
      }
      else if ( dStartTime + ((double) (Sta[i].lSampsInLastPacket-1) / 
                Sta[i].dSampRate) > Sta[i].dEndTime )
      {         
         Sta[i].dEndTime = dStartTime + ((double)
          (Sta[i].lSampsInLastPacket-1) / Sta[i].dSampRate);
         lTemp = (long) ((Sta[i].dEndTime-Sta[i].dStartTime)
                  * Sta[i].dSampRate + 0.001) + 1;
         if ( lTemp > CIRC_BUFFER_SIZE )  
         {                                
            Sta[i].dStartTime +=     
             (lTemp-CIRC_BUFFER_SIZE) / Sta[i].dSampRate;
            NewDateFromModSec( &Sta[i].stStartTime, 
                               Sta[i].dStartTime + 3506630400. );
            Sta[i].lIndex += (lTemp-CIRC_BUFFER_SIZE);
            if ( Sta[i].lIndex >= CIRC_BUFFER_SIZE ) 
                 Sta[i].lIndex -= CIRC_BUFFER_SIZE;
            Sta[i].iHasWrapped = 1;
         }
         lIndexToStartWrite[i] = (long) ((dStartTime-Sta[i].dStartTime) * Sta[i].dSampRate + 0.001) + Sta[i].lIndex;
         if ( lIndexToStartWrite[i] >= CIRC_BUFFER_SIZE ) 
              lIndexToStartWrite[i] -= CIRC_BUFFER_SIZE;
      }
      else             
      {
         lIndexToStartWrite[i] = (long) ((dStartTime - 
          Sta[i].dStartTime) * Sta[i].dSampRate + 0.0001) +
          Sta[i].lIndex;
         if ( lIndexToStartWrite[i] >= CIRC_BUFFER_SIZE ) 
              lIndexToStartWrite[i] -= CIRC_BUFFER_SIZE;
      }
      Sta[i].lSampIndexR = Sta[i].lIndex + (long)((Sta[i].dEndTime-
       Sta[i].dStartTime)*Sta[i].dSampRate + 0.001) + 1;
      if ( Sta[i].lSampIndexR > CIRC_BUFFER_SIZE ) 
         Sta[i].lSampIndexR -= CIRC_BUFFER_SIZE;
      Sta[i].lSampIndexF = Sta[i].lSampIndexR;
      if ( i == MAX_STATIONS-1 ) break;  
   }
      
   for ( i=0; i<dh.iNumChans; i++ )
   {
      if ( Sta[i].lSampsInLastPacket > CIRC_BUFFER_SIZE ) 
      {
         logit( "t", "i=%ld, # bytes/chn in file > CIRC_BUFFER_SIZE, bytes=%ld "
                     "CIRC_BUFFER_SIZE=%ld\n", i, 
                     Sta[i].lSampsInLastPacket, CIRC_BUFFER_SIZE );
         fclose( hFile );
         return 0;
      }
      
      if ( Sta[i].lSampsInLastPacket+lIndexToStartWrite[i] >
           CIRC_BUFFER_SIZE )       
      {
         lTemp = (Sta[i].lSampsInLastPacket+lIndexToStartWrite[i]) - 
                  CIRC_BUFFER_SIZE;
         int read1 = Sta[i].lSampsInLastPacket - lTemp;
         if ( (int) fread( lBuff, iBytePerSamp[i], read1, hFile ) < read1 )
         {
            logit( "t", "start at %ld, byte=%ld, samps=%ld\n", 
            lIndexToStartWrite[i], iBytePerSamp[i],
            Sta[i].lSampsInLastPacket );
            logit( "t", "File %s fread3a fail\n", pszFile );
            fclose( hFile );
            return 0;
         }
         /* PARCHE DE 64 BITS: EXPANSION SEGURA */
         for(int m=0; m < read1; m++) 
            Sta[i].plRawCircBuff[lIndexToStartWrite[i] + m] = (long)lBuff[m];

         if ( (int) fread( lBuff, iBytePerSamp[i], lTemp, hFile ) < lTemp )
         {
            logit( "t", "File %s fread3b fail\n", pszFile );
            fclose( hFile );
    	    return 0;
         }
         for(int m=0; m < lTemp; m++) 
            Sta[i].plRawCircBuff[m] = (long)lBuff[m];
      }
      else                                      
      {
         if ( (iTemp = (int) fread( lBuff, (size_t) iBytePerSamp[i], 
               (size_t) Sta[i].lSampsInLastPacket, hFile )) < Sta[i].lSampsInLastPacket )
         {
            logit( "t", "File %s fread3 fail\n", pszFile );
	    fclose( hFile );
    	    return 0;
         }
         for(int m=0; m < Sta[i].lSampsInLastPacket; m++) 
            Sta[i].plRawCircBuff[lIndexToStartWrite[i] + m] = (long)lBuff[m];
      }
      if ( i == MAX_STATIONS-1 ) break;  
      memcpy( Sta[i].plFiltCircBuff, Sta[i].plRawCircBuff, 
              CIRC_BUFFER_SIZE*sizeof( long ) );
   }
   *piNumStas = dh.iNumChans;
   if ( *piNumStas > MAX_STATIONS ) *piNumStas = MAX_STATIONS;
   fclose( hFile );
   return 1;
}

int ReadDiskDataForHypo( int iFileSize, int iTotalTime, char *szPath,
                         char *szSuffix, double dPreEventTime, int iNumStas,
                         PPICK PBuf[], STATION StaArray[] )
{
   CHNLHEADER ch[MAX_STATIONS];        /* CHNLHEADER info (see diskrw.h) */
   static  FILE    *hFile;             /* Handle to seismic data file */
   DISKHEADER dh;                      /* DISKHEADER info (see diskrw.h) */
   static  double  dFileTime;          /* 1/1/70 time at file start */
   double  dInt;                       /* Time interval between samps */
   static  double  dMin, dMax;         /* Min/Max of expected Ps */
   int     i, j, k;
   static  int     iAllRead;           /* 1 -> all needed data files read */
   int     iInt;                       /* # samps since start of second*/
   static  int     iMaxToRead;         /* Number of files for each station */
   static  int     iMin;               /* Index of Min of expected Ps */
   static  int     iRead[MAX_STATIONS];
   time_t  iTime;                      
   
   /* PARCHE DE 64 BITS: EL BUFFER TIENE QUE SER DEL TAMANO DEL TIPO QUE SE ESCRIBIO (32BITS) */
   int32_t lTemp32[MAX_TEMP];             

   static  char    *pszFile;           /* Data file names */
   struct  tm *tm;                     /* time struct for the file name time */

   iAllRead = 0;
   for ( i=0; i<iNumStas; i++ )
   {
      iRead[i] = 0;
      StaArray[i].lRawCircCtr = 0;
      StaArray[i].lSampIndexF = 0;
      StaArray[i].dEndTime = 0.;
   }
   iMaxToRead = iTotalTime / iFileSize;
   
   dMin = 10000000000.;
   dMax = 0.;
   for ( i=0; i<iNumStas; i++ )
   {
      if ( PBuf[i].dExpectedPTime > dMax )
         dMax = PBuf[i].dExpectedPTime;
      if ( PBuf[i].dExpectedPTime < dMin )
      {
         dMin = PBuf[i].dExpectedPTime;
         iMin = i;
      }
   }   
   dMin -= (dPreEventTime+10.);

   iTime = (time_t) (floor( dMin ) );
   tm = TWCgmtime( iTime );
   dInt = 1. / StaArray[iMin].dSampRate;
   iInt = (int) ((dMin - floor( dMin )) / dInt + 0.00001);
   dFileTime = dMin - ((double) (tm->tm_min % iFileSize)*60.) -
               (double) tm->tm_sec - ((double) iInt * dInt);

   while ( iAllRead == 0 )
   {
      pszFile = CreateFileName( dFileTime, iFileSize, szPath, szSuffix );

      if ( (hFile = fopen( pszFile, "rb" )) == NULL ) 
      {
         logit( "t", "File %s not opened in ReadDiskData\n", pszFile );
         goto IncFileTime;
      }

      if ( fread( &dh, sizeof (DISKHEADER), 1, hFile ) < 1 )       
      {
         fclose( hFile );
         logit( "t", "DISKHEADER read failed in file %s\n", pszFile );
         return( -1 );
      }
    
      for ( i=0; i<dh.iNumChans; i++ )
      {
         if ( (int) fread( &ch[i], dh.iChnHdrSize, 1, hFile ) < 1 ) 
         {
            fclose( hFile );
            logit( "t", "CHNLHEADER read failed, file %s\n", pszFile );
            return( -1 );
         }
         if ( i == MAX_STATIONS )
         {
            fclose( hFile );
            logit( "t", "Too many stations in file %s, %ld\n", pszFile,
                   dh.iNumChans );
            return( -1 );
         }
      }
      
      for ( i=0; i<dh.iNumChans; i++ )
      {
         if ( ch[i].lNumSamps > MAX_TEMP ) 
         {
            logit( "t", "%s lNumSamps (%ld) > MAX_TEMP\n", ch[i].szStation,
                                                           ch[i].lNumSamps );
            fclose( hFile );
    	    return( -1 );
         }
         /* USANDO EL BÚFER DE 32 BITS */
         if ( (int) fread( lTemp32, ch[i].iBytePerSamp, ch[i].lNumSamps, hFile ) < ch[i].lNumSamps )
         {
            logit( "t", "File %s fread3 fail, i=%ld\n", pszFile, i );
            fclose( hFile );
    	    return( -1 );
         }

         for ( j=0; j<iNumStas; j++ )
            if ( !strcmp( ch[i].szStation, StaArray[j].szStation ) &&	 
                 !strcmp( ch[i].szChannel, StaArray[j].szChannel ) &&	 
                 !strcmp( ch[i].szNetID, StaArray[j].szNetID ) )
            {
               if ( iRead[j] == iMaxToRead ) break;    	 
               if ( PBuf[j].dExpectedPTime-dPreEventTime <
                    dFileTime + (double)iFileSize*60.) 
               {	 
                  StaArray[j].lSampsInLastPacket = ch[i].lNumSamps;
                  StaArray[j].dSampRate = ch[i].dSampRate;
                  StaArray[j].dSens = ch[i].dGain;
                  StaArray[j].dGainCalibration = ch[i].dGainCalibration;
                  StaArray[j].dClipLevel = ch[i].dClipLevel;
                  StaArray[j].dTimeCorrection = ch[i].dTimeCorrection;
                  StaArray[j].iStationType = ch[i].iStationType;
                  StaArray[j].iSignalToNoise = ch[i].iSignalToNoise;
                  StaArray[j].dScaleFactor = ch[i].dScaleFactor;
		  
                  if ( ch[i].lNumSamps+StaArray[j].lRawCircCtr <=
                       StaArray[j].lRawCircSize )
                  {
                     StaArray[j].dEndTime=DateToModJulianSec (ch[i].stStartTime)
                      + (((double)ch[i].lNumSamps-1.)/ch[i].dSampRate) -
                      3506630400.;         
                     for ( k=0; k<ch[i].lNumSamps; k++ )
                        /* EXPANSION SEGURA A 64 BITS */
                        StaArray[j].plRawCircBuff[k+StaArray[j].lRawCircCtr] = (long)lTemp32[k];
                     StaArray[j].lRawCircCtr += ch[i].lNumSamps;
                     StaArray[j].lSampIndexF += ch[i].lNumSamps;
                  }
                  else
                     logit( "", "%s-data buffer overwrite prevented %ld %ld %ld\n",
                            StaArray[j].szStation, ch[i].lNumSamps, StaArray[j].lRawCircCtr,
                            StaArray[j].lRawCircSize );
                  iRead[j]++;
               }
               break;
            }
      }      
      fclose( hFile );
      IncFileTime:
      dFileTime += ((double) iFileSize*60.);     
      if ( dFileTime > dMax+((double) iTotalTime*60.) ) iAllRead = 1;
   }   
   return( 0 );
}

int ReadDiskDataForMTSolo( int iFileSize, int iTotalTime, char *szPath,
                           char *szSuffix, double dStartTime, int iNumStas,
                           STATION StaArray[], int *iNumRead )
{
   CHNLHEADER ch[MAX_STATIONS];        /* CHNLHEADER info (see diskrw.h) */
   static  FILE    *hFile;             /* Handle to seismic data file */
   DISKHEADER dh;                      /* DISKHEADER info (see diskrw.h) */
   static  double  dFileTime;          /* 1/1/70 time at file start */
   double  dInt;                       /* Time interval between samps */
   int     i, j, k, l;
   static  int     iAllRead;           /* 1 -> all needed data files read */
   int     iInt;                       /* # samps since start of second*/
   static  int     iMaxToRead;         /* Number of files to read */
   static  int     iRead[MAX_STATIONS];
   time_t  iTime;                      /* time (1/1/70) at min P time */   
   long    lStart;                     /* Starting data index of buffer */
   
   /* PARCHE DE 64 BITS: INT32_T EN LUGAR DE LONG */
   int32_t lTemp32[MAX_TEMP];             

   static  char    *pszFile;           /* Data file names */
   struct  tm *tm;                     /* time struct for the file name time */

   iAllRead = 0;
   for ( i=0; i<iNumStas; i++ )
   {
      iRead[i] = 0;
      StaArray[i].lRawCircCtr = 0;
      StaArray[i].lSampIndexF = 0;
      StaArray[i].dEndTime = 0.;
   }
   iMaxToRead = iTotalTime/iFileSize + 1;

   iTime = (time_t) (floor( dStartTime ));
   tm = TWCgmtime( iTime );
   dInt = 1. / StaArray[0].dSampRate;
   iInt = (int) ((dStartTime - floor( dStartTime )) / dInt + 0.00001);
   dFileTime = dStartTime - ((double) (tm->tm_min % iFileSize)*60.) -
              (double) tm->tm_sec - ((double) iInt*dInt);

   for ( k=0; k<iMaxToRead; k++ )
   {
      pszFile = CreateFileName( dFileTime, iFileSize, szPath, szSuffix );

      if ( (hFile = fopen( pszFile, "rb" )) == NULL ) 
      {
         logit( "t", "File %s not opened in ReadDiskDataForMTSolo\n", pszFile );
         *iNumRead = k;	 
         if ( k > 0 ) return( 0 );  
         else         return( 1 );
      }
	  
      if ( fread( &dh, sizeof (DISKHEADER), 1, hFile ) < 1 )
      {
         fclose( hFile );
         logit( "t", "DISKHEADER read failed in file %s\n", pszFile );
         *iNumRead = k;	 
         return( -1 );
      }
    
      for ( i=0; i<dh.iNumChans; i++ )
      {
         if ( (int) fread( &ch[i], dh.iChnHdrSize, 1, hFile ) < 1 ) 
         {
            fclose( hFile );
            logit( "t", "CHNLHEADER read failed, file %s\n", pszFile );
            *iNumRead = k;	 
            return( -1 );
         }
         if ( i == MAX_STATIONS )
         {
            fclose( hFile );
            logit( "t", "Too many stations in file %s, %ld\n", pszFile,
                   dh.iNumChans );
            *iNumRead = k;	 
            return( -1 );
         }
      }
      
      for ( i=0; i<dh.iNumChans; i++ )
      {
         if ( ch[i].lNumSamps > MAX_TEMP ) 
         {
            logit( "t", "%s lNumSamps (%ld) > MAX_TEMP\n", ch[i].szStation,
                                                           ch[i].lNumSamps );
            fclose( hFile );
            *iNumRead = k;	 
    	    return( -1 );
         }
         /* USANDO BUFFER DE 32 BITS */
         if ( (int) fread( lTemp32, ch[i].iBytePerSamp, ch[i].lNumSamps, hFile ) < ch[i].lNumSamps )
         {
            logit( "t", "File %s fread3 fail, i=%ld\n", pszFile, i );
            fclose( hFile );
            *iNumRead = k;	 
            return( -1 );
         }

         for ( j=0; j<iNumStas; j++ )
            if ( !strcmp( ch[i].szStation, StaArray[j].szStation ) &&	 
                 !strcmp( ch[i].szChannel, StaArray[j].szChannel ) &&	 
                 !strcmp( ch[i].szNetID, StaArray[j].szNetID ) )
            {	 
               if ( k == 0 )
               {        
                  lStart = (long) ((dStartTime-dFileTime) /
                                   (1./ch[i].dSampRate) + 0.01);
                  StaArray[j].dEndTime = GetTimeFromFileName( pszFile );
                  StaArray[j].dStartTime = dStartTime;
                  NewDateFromModSec( &StaArray[j].stStartTime, 
                                      StaArray[j].dStartTime + 3506630400. );
               }
               else lStart = 0;
               for ( l=lStart; l<ch[i].lNumSamps; l++ )
               {
                  if ( StaArray[j].lRawCircCtr < StaArray[j].lRawCircSize )
                  {
                     /* EXPANSION SEGURA A 64 BITS */
                     StaArray[j].plRawCircBuff[StaArray[j].lRawCircCtr] = (long)lTemp32[l];
                     StaArray[j].lRawCircCtr += 1;
                     StaArray[j].lSampIndexF += 1;
                     StaArray[j].dEndTime += (1./ch[i].dSampRate);
                  }
               }
               break;
            }
      }      
      fclose( hFile );
      dFileTime += ((double) iFileSize*60.);     
   }   
   *iNumRead = k+1;	 
   return( 0 );
}

int ReadDiskHeader( char *pszFile, STATION Sta[], int iMaxRead )
{
   CHNLHEADER ch;                    /* CHNLHEADER information (see diskrw.h) */
   DISKHEADER dh;                    /* DISKHEADER info (see diskrw.h) */
   FILE	     *hFile;                 /* Handle to seismic data file */
   int        i;
   LATLON     ll, llOut;

   if ( (hFile = fopen( pszFile, "rb" )) == NULL ) 
   {
      logit ("t", "File %s not opened in ReadDiskHeader\n", pszFile);
      return -1;
   }

   if ( fread( &dh, sizeof (DISKHEADER), 1, hFile ) < 1 ) 
   {	
      fclose( hFile );
      logit( "t", "DISKHEADER read failed: file %s\n", pszFile );
      return -1;
   }
   
   if ( dh.iNumChans > iMaxRead )   
   {	
      fclose( hFile );
      logit( "t", "Too many channels (%ld) in %s\n", dh.iNumChans, pszFile );
      return -1;
   }

   for ( i=0; i<dh.iNumChans; i++ )
   {
      if ( (int) fread( &ch, dh.iChnHdrSize, 1, hFile ) < 1 )  
      {
         fclose( hFile );
         logit( "t", "CHNLHEADER read failed, file %s\n", pszFile);
         return -1;
      }
      strcpy( Sta[i].szStation, ch.szStation );
      strcpy( Sta[i].szChannel, ch.szChannel );
      strcpy( Sta[i].szNetID, ch.szNetID );
      ll.dLat = ch.dLat;   
      ll.dLon = ch.dLon;   
      if ( DateToModJulianSec( dh.stStartTime ) < 4488393600. )
      {
         GeoGraphic( &llOut, &ll );
         Sta[i].dLat = llOut.dLat;
         Sta[i].dLon = llOut.dLon;
         Sta[i].dElevation = ch.dElevation*EARTHRAD;
      }
      else
      {
         Sta[i].dLat = ll.dLat;
         Sta[i].dLon = ll.dLon;                
         Sta[i].dElevation = ch.dElevation;
      }
      Sta[i].lSampsInLastPacket = ch.lNumSamps;
      Sta[i].dSampRate = ch.dSampRate;
      Sta[i].dSens = ch.dGain;
      Sta[i].dGainCalibration = ch.dGainCalibration;
      Sta[i].dClipLevel = ch.dClipLevel;
      Sta[i].dTimeCorrection = ch.dTimeCorrection;
      Sta[i].dScaleFactor = ch.dScaleFactor;  
      Sta[i].iStationType = ch.iStationType;
      Sta[i].iSignalToNoise = ch.iSignalToNoise;
      Sta[i].iPickStatus = ch.iPickStatus;
      Sta[i].iTrigger = ch.iTrigger;
      Sta[i].iDisplayStatus = 1;
      if ( DateToModJulianSec( dh.stStartTime ) < 4477766400. &&
           Sta[i].dSampRate < 3. && Sta[i].dScaleFactor > 0.005 )
         Sta[i].dScaleFactor = 0.005;
   }
   fclose( hFile );
   return dh.iNumChans;
}                                  

int ReadLineupFile( char *pszFile, STATION *Sta )
{
   FILE   *hFile;              /* File handle */
   int     i;
   
   hFile = fopen( pszFile, "r" );
   if ( hFile == NULL )
   {
      logit( "", "%s could not be opened in ReadLineupFile\n", pszFile );
      return (-1);
   }
   
   i=0;
   while ( !feof( hFile ) )
   {
      fscanf( hFile, "%s %s %s %lf %lf %lf %lf %lf %lf %lf %lf %lf %d %d %d %d %d %lf %lf %lf %d\n",
       Sta[i].szStation, Sta[i].szChannel, Sta[i].szNetID, &Sta[i].dLat,
       &Sta[i].dLon, &Sta[i].dElevation, &Sta[i].dSampRate, &Sta[i].dSens,
       &Sta[i].dGainCalibration, &Sta[i].dClipLevel, &Sta[i].dTimeCorrection,
       &Sta[i].dScaleFactor, &Sta[i].iStationType, &Sta[i].iSignalToNoise,
       &Sta[i].iPickStatus, &Sta[i].iFiltStatus, &Sta[i].iAlarmStatus,
       &Sta[i].dAlarmAmp, &Sta[i].dAlarmDur, &Sta[i].dAlarmMinFreq,
       &Sta[i].iComputeMwp );
      i++;
   }       

   fclose( hFile );
   return (i);
}
