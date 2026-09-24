#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <kom.h>
#include <transport.h>
#include <earthworm.h>
#include <trace_buf.h>
#include "disk_wcatwc.h"
#ifndef _WIN32
#include <sys/stat.h>
#include <sys/types.h>
#endif

      /******************************************************************
       * InitDiskFile()                      *
       * *
       * This function initializes a data file with header and zeroes  *
       * for data.  All data is output in this program as 4 byte data. *
       * The format is explained in WriteDiskData. Data files must be  *
       * created before data is entered into the file.  Initially all  *
       * data is set to zero.  WriteDiskData fills the file at the     *
       * appropriate location.  Data is written like this since we     *
       * never know when the data will get here (how late, that is).   *
       * *
       * April, 2006: Replaced gmtime with safer function.             * *
       * *
       * This function is called when the first packet which will be   *
       * written to the file arrives.                                  *
       * *
       * Arguments:                                                    *
       * pszFile          File name to initialize                     *
       * Sta              Station control data array                  *
       * dFileTime        1/1/70 (seconds) time of file (nominal)     *
       * iNumStas         Number of stations to write to disk         *
       * pszRootDir       Seismic data file root directory            *
       * iFileSize        File length in minutes                      *
       * dPacketTime      1/1/70 (seconds) start time of data         *
       * chn              CHNLHEADER Array                            *
       * iDebug           Debug switch                                *
       * *
       * Return:            0 if initialization OK, -1 if problem      * * *
       ******************************************************************/

int InitDiskFile( char *pszFile, STATION StaArray[], double dFileTime,
                  int iNumStas, char *pszRootDir, int iFileSize, 
                  double dPacketTime, CHNLHEADER chn[], int iDebug ) 
{
   static DISKHEADER  dh;     /* DISKHEADER info (see disk_header.h) */
   FILE        *hFile;        /* Handle to seismic data file */
   int         i;             /* Counter */
   time_t      itime;         /* time (1/1/70) at start of file */   
   
   /* PARCHE 64 BITS: Usar int32_t (4 bytes) explicitamente en lugar de long (8 bytes) */
   int32_t     *plZero;       /* Pointer to data buffer of zeroes */
   
   char        *psz;          /* String to extract directory name from */
   char        szDir[80];     /* Directory part of pszFile */
   static struct tm *tmLoc;   /* time structure for the file name time */

/* Initialize header structures for new file (time is nominal Time; i.e
   milliseconds are not used) */
   dh.iNumChans = iNumStas;
   dh.iChnHdrSize = sizeof (CHNLHEADER);
   
   itime = (time_t) (floor( dFileTime ) );
/* tmLoc = gmtime( &itime ); */
   tmLoc = TWCgmtime( itime );/* gmtime replaced with a local "safe" function */
   ConvertTM2ST( tmLoc, &dh.stStartTime );
   
   for ( i=0; i<iNumStas; i++ )
   {
      strcpy( chn[i].szStation, StaArray[i].szStation );
      strcpy( chn[i].szChannel, StaArray[i].szChannel );
      strcpy( chn[i].szNetID, StaArray[i].szNetID );
      chn[i].dSampRate      = StaArray[i].dSampRate;
      chn[i].lNumSamps      = (long)((double)iFileSize*60.*chn[i].dSampRate + 0.01);
      
      /* PARCHE 64 BITS: Forzar a que los bytes por sample sean siempre 4 (int32_t) */
      chn[i].iBytePerSamp   = sizeof (int32_t);   
      
      chn[i].iTrigger       = 0;
      chn[i].iSignalToNoise = StaArray[i].iSignalToNoise;
      chn[i].iPickStatus    = StaArray[i].iPickStatus;
      chn[i].iStationType   = StaArray[i].iStationType;
      chn[i].dLat           = StaArray[i].dLat;
      chn[i].dLon           = StaArray[i].dLon;
      chn[i].dElevation     = StaArray[i].dElevation;
      chn[i].dGain          = StaArray[i].dSens;
      chn[i].dGainCalibration = StaArray[i].dGainCalibration;
      chn[i].dClipLevel     = StaArray[i].dClipLevel;
      chn[i].dTimeCorrection = StaArray[i].dTimeCorrection;
      chn[i].dScaleFactor   = StaArray[i].dScaleFactor;
      CopyDate (&dh.stStartTime, &chn[i].stStartTime);
   }		/* NOTE: Individual channel start times re-set in WriteDiskData */

/* See if directory already has been created */
   psz = strrchr( pszFile, '\\' );   /* Get location of last \ in string */
   if (psz == NULL) psz = strrchr( pszFile, '/' ); /* Fallback para Linux */
   
   if (psz != NULL) {
       strncpy( szDir, pszFile, (psz-pszFile) ); /* Extract just the dir from the file name */
       szDir[psz-pszFile] = '\0';
       #ifdef _WIN32
       if ( !CreateDirectory( szDir, NULL ) )
       #else
       if ( mkdir( szDir, 0755 ) != 0 )
       #endif
          if ( iDebug ) logit( "t", "Dir: %s not created in InitDiskFile\n", szDir );
   }

   /* ------ INICIO DE DEPURACION ------ */
   if ( iDebug ) logit("e", "\nDEBUG 1: Intentando abrir archivo con fopen: %s\n", pszFile);

/* Try to open data file */
   if ( ( hFile = fopen( pszFile, "wb" ) ) == NULL )
   {
      logit( "t", "File: %s not opened in InitDiskFile\n", pszFile );
      return( -1 );
   }

   if ( iDebug ) {
       logit("e", "DEBUG 2: fopen exitoso. Escribiendo DISKHEADER en memoria...\n");
       logit("e", "DEBUG 3: Ejecutando fwrite de la cabecera DISKHEADER (Size: %lu bytes)...\n", (unsigned long)sizeof(dh));
   }

/* Write the file headers */
   fwrite( &dh, sizeof(dh), 1, hFile );
   
   if ( iDebug ) logit("e", "DEBUG 4: fwrite de DISKHEADER exitoso. Escribiendo %d CHNLHEADERs...\n", iNumStas);
   
   for ( i=0; i<iNumStas; i++ )
      fwrite( &chn[i], sizeof(CHNLHEADER), 1, hFile );

   if ( iDebug ) logit("e", "DEBUG 5: Cabeceras escritas. Inicializando bloques de datos a cero...\n");
   /* ------ FIN DE DEPURACION ------ */

/* Initialize the data to zero */
   for ( i=0; i<iNumStas; i++ )
   {
      /* PARCHE 64 BITS: Asignar memoria como int32_t */
      plZero = (int32_t *) malloc( chn[i].iBytePerSamp*chn[i].lNumSamps );
      if ( plZero == NULL )
      {
         logit( "et", "disk_wcatwc: Cannot allocate waveform buffer\n" );
         return( -1 );
      }
      memset( plZero, 0, chn[i].iBytePerSamp*chn[i].lNumSamps );
      fwrite( plZero, 1, chn[i].iBytePerSamp*chn[i].lNumSamps, hFile );
      free( plZero );
   }
   fclose( hFile );
   return( 0 );
}

      /******************************************************************
       * WriteDiskData()                      *
       * *
       * This function patches data into a file which is created if    *
       * necessary.  There is a contingency here to write to two files *
       * if the packet happens to span two files.  NOTE: All data is   *
       * written as 4 byte data, even if it is 12 bit data.            *
       * *
       * The format is an internal WC&ATWC seismic data format.  The   *
       * format looks like:                                            *
       * *
       * structure DISKHEADER;                                         *
       * structure CHNLHEADER * iNumStations;                          *
       * chn 0 samp0, chn 0 samp1, ... chn 0 lNumSamps-1;              *
       * chn 1 samp0, chn 1 samp1, ... chn 1 lNumSamps-1;              *
       * .                                              *
       * .                                              *
       * .                                              *
       * chn NSta-1 samp0, chn NSta-1 samp1, ... chn NSta-1 lNumSamps-1*
       * *
       * Samp0 from each channel is the same time (well, almost), etc. *
       * *
       * The data is written in binary (always i4).  The file name     *
       * describes what time the file holds.  iFileSize is read in     *
       * from the .d file.  This tells how many minutes of seismic     *
       * data there are per file.  The names are in the format         *
       * [szRootDirectory]\Dyymmdd\Smdhhmm[FileSuffix]yy.              *
       * The first sample in the file is the first sample after the    *
       * time given in the file name.  The file name time is called    *
       * the nominal time.                                             *
       * *
       * April, 2006: Replaced gmtime with safe function.              *
       * March, 2006: Fixed bug which affected disk writes when sample *
       * rates where not logical numbers.                 *
       * *
       * Arguments:                                                    *
       * Sta              Control array for packet                    *
       * WaveLong         Array of data                               *
       * Gparm             Structure with initialization data         *
       * Wavehead         Header of earthworm data packet             *
       * iNumStas         Number of stations to write to disk         *
       * StaArray         Complete array of station data              *
       * chn              CHNLHEADER Array                            *
       * *
       * Return:            0 if data written OK, -1 if problem        * * *
       ******************************************************************/

int WriteDiskData( STATION *Sta, long *WaveLong, GPARM *Gparm, 
                   TRACE_HEADER *Wavehead, int iNumStas, STATION StaArray[],
                   CHNLHEADER *chn ) 
{
   FILE     *hFile;                     /* Handle to seismic data file */
   DISKHEADER dh;                       /* DISKHEADER info (see disk_wcatwc.h)*/
   static   double   dFileTime;         /* 1/1/70 time at file start */
   double   dInt;                       /* Time interval between samps */
   int      i;
   static   int      iFirst = 1;        /* Set to 0 after first init */
   static   int      iIndex;            /* This channels index in StaArray */
   static   int      iInt;              /* # data samps due to milliseconds */
   static   long     lNumToWrite;       /* # samps to write to the first file */
   static   long     lSampsToShift;     /* # file samps to skip in this trace */
   static   long     lPos;              /* Position in file to patch data */
   static   SYSTEMTIME st;              /* Exact start time of trace in file */
   char     *pszFile;                   /* 1st File to write data */
   struct tm *tm;                       /* time struct for the file name time */
   time_t   itime;                      /* time (1/1/70) at start of packet */   

   /* PARCHE 64 BITS: Tratar el puntero WaveLong como un array de int32_t puros 
      ya que en disk_wcatwc.c hicimos la conversion previa. */
   int32_t  *Wave32 = (int32_t *)WaveLong;

/* Compute file time (that is, the time of the first sample in the file -
   Milliseconds are important here). */
   dInt = 1. / Sta->dSampRate;          /* Time interval between samps */
   itime = (time_t) (floor( Wavehead->starttime ) );
   tm = TWCgmtime( itime );/* gmtime replaced with a local "safe" function */
//   tm = gmtime( &itime );
//   iInt = (int) ((Wavehead->starttime - floor( Wavehead->starttime )) / dInt +
//                 0.0000000001);
//   iInt += (int) ((((double) (tm->tm_min % Gparm->FileLength)*60.) +
//                    (double)  tm->tm_sec) / dInt + 0.0000000001);
   iInt = (int) ((((double) (tm->tm_min % Gparm->FileLength)*60.) +
                   (double)  tm->tm_sec +
                    Wavehead->starttime - floor( Wavehead->starttime )) /
                    dInt + 0.0000000001);
   dFileTime = Wavehead->starttime - ((double) iInt * dInt);
//   dFileTime = Wavehead->starttime - ((double) (tm->tm_min % Gparm->FileLength)*
//               60.) - (double) tm->tm_sec - ((double) iInt * dInt);
   itime = (time_t) (floor( dFileTime ) );
   tm = TWCgmtime( itime );/* gmtime replaced with a local "safe" function */
//   tm = gmtime( &itime );
   ConvertTM2ST( tm, &st );
   st.wMilliseconds = (int) ((dFileTime - floor( dFileTime )) * 1000.);

/* Get file name for this packet (at least the first file to write) */
   pszFile = CreateFileName( dFileTime, Gparm->FileLength,
                             Gparm->DiskWritePath, Gparm->FileSuffix );

/* Try to open the data file (we must check to see if it was already created)
   (1st time through, file is re-initialized.  This must be done in case
   number of stations, samp rates,... have changed) */
   if ( ( hFile = fopen( pszFile, "rb+" ) ) == NULL || iFirst )
   {
      if ( Gparm->Debug > 0 )
         logit( "t", "Must init %s - %lf - %s SR=%lf - WH-start %lf\n",
                pszFile, dFileTime, Wavehead->sta, Sta->dSampRate,
                Wavehead->starttime );
                
      if ( Gparm->Debug ) logit("e", "DEBUG 0: Llamando a InitDiskFile para generar la estructura inicial (%s)...\n", pszFile);

/* Then we must create file since this is probably the 1st packet to go in */
      if ( InitDiskFile( pszFile, StaArray, dFileTime, iNumStas,
                         Gparm->DiskWritePath, Gparm->FileLength, 
                         Wavehead->starttime, chn, Gparm->Debug ) < 0 )
      {
         logit( "t", "File: %s not opened in WriteDiskData\n", pszFile );
         return( -1 );
      }
      else
      {
         if ( ( hFile = fopen( pszFile, "rb+" ) ) == NULL )
         {
            logit( "t", "File: %s not opened after init\n", pszFile );
            return( -1 );
         }
         iFirst = 0;
      }
   }

/* Find the proper position in the file to write */
   lPos = sizeof (DISKHEADER);
   lPos += iNumStas * sizeof (CHNLHEADER);
   iIndex = (Sta - StaArray);
   for ( i=0; i<iIndex; i++ )
      /* PARCHE 64 BITS: Usar sizeof(int32_t) en vez de sizeof(long) para calcular los offsets correctos de archivo */
      lPos += sizeof (int32_t) * (long) ((double) Gparm->FileLength * 60. * chn[i].dSampRate + 0.0000001);
      
   lSampsToShift = (long) ((Wavehead->starttime - dFileTime) * Sta->dSampRate + 0.0000001);
//   lPos += sizeof (int32_t) * lSampsToShift;
   lPos += sizeof (int32_t) * iInt;
   
/* Will the data go into the next file? */
   lNumToWrite = Wavehead->nsamp;
//   if ( lSampsToShift+Wavehead->nsamp > chn[iIndex].lNumSamps ) 
//      lNumToWrite = chn[iIndex].lNumSamps - lSampsToShift;
   if ( iInt+Wavehead->nsamp > chn[iIndex].lNumSamps ) 
      lNumToWrite = chn[iIndex].lNumSamps - iInt;

/* Flag sample rates that are not the same in the .sta file as in the
   packet that just arrived.  These should be made to match by adjusting
   the .sta file */
   if ( Wavehead->samprate < Sta->dSampRate-0.01 || 
        Wavehead->samprate > Sta->dSampRate+0.01 )
      logit( "t", "Sample Rates not equal for %s %s. sta=%lf, packet=%lf\n",
             Sta->szStation, Sta->szChannel, Sta->dSampRate,
			 Wavehead->samprate );
 
/* If open !NULL, file already there.  Read the file headers */
   fread( &dh, sizeof (dh), 1, hFile );
   for ( i=0; i<iNumStas; i++ )
      fread( &chn[i], sizeof (CHNLHEADER), 1, hFile );
   rewind( hFile );

/* Update the file start time for this channel (time in dh is nominal time, each
   channel may have a different actual start time).*/
   CopyDate (&st, &chn[iIndex].stStartTime);

/* Re-write the file headers */
   fwrite( &dh, sizeof (dh), 1, hFile );
   for ( i=0; i<iNumStas; i++ )
      fwrite( &chn[i], sizeof (CHNLHEADER), 1, hFile );

/* Move to the correct position in the file */
   if ( fseek( hFile, lPos, SEEK_SET ) )
   {
      logit( "t", "fseek failed for File: %s\n", pszFile );
      fclose( hFile );
      return( -1 );
   }

/* Write the data */
   /* PARCHE 64 BITS: Escribir exactamente 4 bytes por muestra usando el cast a Wave32 */
   if ( (long) fwrite( Wave32, sizeof (int32_t), lNumToWrite, hFile) <
         lNumToWrite )
   {
      logit( "t", "fwrite failed for File: %s\n", pszFile);
      fclose( hFile );
      return( -1 );
   }
   fclose( hFile );

/* Now, update the next file if this packet was split between two files.
   (On second write, don't bother updating header, this will be done
   in a later write) */
   if ( lNumToWrite < Wavehead->nsamp )
   {
      pszFile = CreateFileName( Wavehead->starttime +
       (double) Wavehead->nsamp / Wavehead->samprate, Gparm->FileLength, 
        Gparm->DiskWritePath, Gparm->FileSuffix);

/* Find the proper position in the file to write */
      lPos = sizeof (DISKHEADER);
      lPos += iNumStas * sizeof (CHNLHEADER);
      for ( i=0; i<iIndex; i++ )
         /* PARCHE 64 BITS: Asegurar offsets de 4 bytes */
         lPos += sizeof (int32_t) * (long) ((double) Gparm->FileLength * 60. * chn[i].dSampRate + 0.0000001);

/* Try to open the data file (we must check to see if it was already created) */
      if ( ( hFile = fopen( pszFile, "rb+" ) ) == NULL )
      {

/* Then we must create file since this is probably the 1st packet to go in */
         if ( Gparm->Debug > 0 )
            logit( "t", "Must init %s - %lf - %s SR=%lf - WH-start %lf\n",
                   pszFile, dFileTime, Wavehead->sta, Sta->dSampRate,
                   Wavehead->starttime );
                   
         if ( Gparm->Debug ) logit("e", "DEBUG 0: Llamando a InitDiskFile - Archivo 2 (%s)...\n", pszFile);
                   
         if ( InitDiskFile( pszFile, StaArray,
              dFileTime+(double)Gparm->FileLength*60.,
              iNumStas, Gparm->DiskWritePath, Gparm->FileLength, 
              Wavehead->starttime, chn, Gparm->Debug ) < 0 )
         {
            logit( "t", "File: %s not opened in WriteDiskData - 2\n", pszFile );
            return( -1 );
         }
         else
         {
            if ( ( hFile = fopen( pszFile, "rb+" ) ) == NULL )
            {
               logit( "t", "File: %s not opened after init-2\n", pszFile );
               return( -1 );
            }
         }
      }
 
/* If open !NULL, file already there.  Move to the correct spot and patch data*/
      if ( fseek( hFile, lPos, SEEK_SET ) )
      {
         logit( "t", "fseek failed for File2: %s\n", pszFile );
         fclose( hFile);
         return( -1 );
      }

/* Write the data */
      /* PARCHE 64 BITS: Volvemos a asegurar que se escriban 4 bytes por muestra */
      if ( (long) fwrite (&Wave32[lNumToWrite], sizeof (int32_t), 
         Wavehead->nsamp-lNumToWrite, hFile) < 
         (Wavehead->nsamp-lNumToWrite))
      {
         logit( "t", "fwrite failed for File2: %s, index=%ld\n", pszFile,
		        iIndex );
         fclose( hFile );
         return( -1 );
      }
      fclose (hFile);
   }
return( 0 );
}
