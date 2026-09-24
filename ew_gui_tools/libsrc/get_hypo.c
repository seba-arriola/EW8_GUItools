/************************************************************************
  * GET_HYPO.C                                                           *
  * *
  * This is a group of functions which provide tools for                 *
  * reading hypocenters from a ring and loading them in a structure.     *
  * *
  * Made into earthworm module 4/2001.                                   *
  * *
  ************************************************************************/
  
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <earthworm.h>
#include <transport.h>
#include "earlybirdlib.h"

     /**************************************************************
      * CopyHypo                                  *
      * *
      * Copy hypocenter structure into main hypocenter array.      *
      * *
      * Arguments:                                                 *
      * HStruct     Hypocenter from ring                          *
      * Hypo        Array of Hypocenter data structures           *
      * *
      * Returns:     Index of updated quake                        *
      * *
      **************************************************************/

int CopyHypo( HYPO *HStruct, HYPO Hypo[] )
{
   int     i;
   int     iMatch;             /* Flag to set if match found */

/* Check if this is a new version of an old quake */
   iMatch = -1;
   for ( i=0; i<MAX_QUAKES; i++ )
      if ( HStruct->iQuakeID == Hypo[i].iQuakeID &&
           HStruct->iVersion >= Hypo[i].iVersion &&
           HStruct->dOriginTime < Hypo[i].dOriginTime + 1200. &&
           HStruct->dOriginTime > Hypo[i].dOriginTime - 1200. )
         iMatch = i;

/* Shift array if this is a new quake (move each entry down one) */
   if ( iMatch < 0 )
   {
      for ( i=MAX_QUAKES-2; i>=0; i-- )
      {
         strcpy( Hypo[i+1].szPMagType, Hypo[i].szPMagType );
         Hypo[i+1].iQuakeID      = Hypo[i].iQuakeID;
         Hypo[i+1].iVersion      = Hypo[i].iVersion;
         Hypo[i+1].dLat          = Hypo[i].dLat;
         Hypo[i+1].dLon          = Hypo[i].dLon;
         Hypo[i+1].dCoslat       = Hypo[i].dCoslat;
         Hypo[i+1].dSinlat       = Hypo[i].dSinlat;
         Hypo[i+1].dCoslon       = Hypo[i].dCoslon;
         Hypo[i+1].dSinlon       = Hypo[i].dSinlon;
         Hypo[i+1].dOriginTime   = Hypo[i].dOriginTime;
         Hypo[i+1].dDepth        = Hypo[i].dDepth;
         Hypo[i+1].iNumPs        = Hypo[i].iNumPs;
         Hypo[i+1].iAzm          = Hypo[i].iAzm;
         Hypo[i+1].dAvgRes       = Hypo[i].dAvgRes;
         Hypo[i+1].iGoodSoln     = Hypo[i].iGoodSoln;
         Hypo[i+1].dPreferredMag = Hypo[i].dPreferredMag;
         Hypo[i+1].iNumPMags     = Hypo[i].iNumPMags;
         Hypo[i+1].dMSAvg        = Hypo[i].dMSAvg;
         Hypo[i+1].iNumMS        = Hypo[i].iNumMS;
         Hypo[i+1].dMbAvg        = Hypo[i].dMbAvg;
         Hypo[i+1].iNumMb        = Hypo[i].iNumMb;
         Hypo[i+1].dMlAvg        = Hypo[i].dMlAvg;
         Hypo[i+1].iNumMl        = Hypo[i].iNumMl;
         Hypo[i+1].dMwpAvg       = Hypo[i].dMwpAvg;
         Hypo[i+1].iNumMwp       = Hypo[i].iNumMwp;      
         Hypo[i+1].dMwAvg        = Hypo[i].dMwAvg;
         Hypo[i+1].iNumMw        = Hypo[i].iNumMw;      
         Hypo[i+1].iMagOnly      = Hypo[i].iMagOnly;      
      }
      iMatch = 0;
   }

/* Now, copy (or update) the new hypo into the array */   
   strcpy( Hypo[iMatch].szPMagType, HStruct->szPMagType );
   Hypo[iMatch].iQuakeID      = HStruct->iQuakeID;
   Hypo[iMatch].iVersion      = HStruct->iVersion;
   Hypo[iMatch].dLat          = HStruct->dLat;
   Hypo[iMatch].dLon          = HStruct->dLon;
   Hypo[iMatch].dCoslat       = HStruct->dCoslat;
   Hypo[iMatch].dSinlat       = HStruct->dSinlat;
   Hypo[iMatch].dCoslon       = HStruct->dCoslon;
   Hypo[iMatch].dSinlon       = HStruct->dSinlon;
   Hypo[iMatch].dOriginTime   = HStruct->dOriginTime;
   Hypo[iMatch].dDepth        = HStruct->dDepth;
   Hypo[iMatch].iNumPs        = HStruct->iNumPs;
   Hypo[iMatch].iAzm          = HStruct->iAzm;
   Hypo[iMatch].dAvgRes       = HStruct->dAvgRes;
   Hypo[iMatch].iGoodSoln     = HStruct->iGoodSoln;
   Hypo[iMatch].dPreferredMag = HStruct->dPreferredMag;
   Hypo[iMatch].iNumPMags     = HStruct->iNumPMags;
   Hypo[iMatch].dMSAvg        = HStruct->dMSAvg;
   Hypo[iMatch].iNumMS        = HStruct->iNumMS;
   Hypo[iMatch].dMbAvg        = HStruct->dMbAvg;
   Hypo[iMatch].iNumMb        = HStruct->iNumMb;
   Hypo[iMatch].dMlAvg        = HStruct->dMlAvg;
   Hypo[iMatch].iNumMl        = HStruct->iNumMl;
   Hypo[iMatch].dMwpAvg       = HStruct->dMwpAvg;
   Hypo[iMatch].iNumMwp       = HStruct->iNumMwp;
   Hypo[iMatch].dMwAvg        = HStruct->dMwAvg;
   Hypo[iMatch].iNumMw        = HStruct->iNumMw;
   Hypo[iMatch].iMagOnly      = HStruct->iMagOnly;

   return iMatch;      
}

     /**************************************************************
      * HypoStruct()                                *
      * *
      * Fill in HYPO structure from HYPOTWC message.               *
      * *
      * Arguments:                                                 *
      * HIn         HypoTWC message from ring                     *
      * pHypo       Hypocenter data structure                     *
      * *
      * Return - 0 if OK, -1 if copy problem                       *
      **************************************************************/

int HypoStruct( char *HIn, HYPO *pHypo )
{
/* Break up incoming message (PARCHADO A %d PARA VARIABLES INT EN 64 BITS)
   *************************/
   if ( sscanf( HIn, "%d %d %lf %lf %lf %lf %d %d %lf %d %lf %s %d "
                     "%lf %d %lf %d %lf %d %lf %d %lf %d %d",
    &pHypo->iQuakeID, &pHypo->iVersion, &pHypo->dOriginTime,
    &pHypo->dLat, &pHypo->dLon, &pHypo->dDepth, &pHypo->iNumPs, &pHypo->iAzm,
    &pHypo->dAvgRes, &pHypo->iGoodSoln, &pHypo->dPreferredMag,
     pHypo->szPMagType, &pHypo->iNumPMags,
    &pHypo->dMSAvg, &pHypo->iNumMS, &pHypo->dMwpAvg, &pHypo->iNumMwp, 
    &pHypo->dMbAvg, &pHypo->iNumMb, &pHypo->dMlAvg, &pHypo->iNumMl,
    &pHypo->dMwAvg, &pHypo->iNumMw, &pHypo->iMagOnly  ) != 24 )
   {
      logit( "t", "Not correct # fields in message: %s\n", HIn );
      return -1;
   }    
   GetLatLonTrig( (LATLON *) pHypo );  /* Get sin/cos of lat and lon */
   return 0;
}

      /******************************************************************
       * InitHypo()                          *
       * *
       * This function initializes a HYPO structure.                   *
       * *
       * Arguments:                                                    *
       * Hypo             Quake hypocenter parameter structure        *
       * *
       ******************************************************************/
	
void InitHypo( HYPO *pHypo )
{
   pHypo->dLat          = 0.;
   pHypo->dLon          = 0.;
   pHypo->dCoslat       = 0.;
   pHypo->dSinlat       = 0.;
   pHypo->dCoslon       = 0.;
   pHypo->dSinlon       = 0.;
   pHypo->dOriginTime   = 0.0;
   pHypo->iNumPs        = 0;
   pHypo->iNumBadPs     = 0;
   pHypo->iFinalMade    = 0;
   pHypo->dAvgRes       = 0.;
   pHypo->iAzm          = 0;
   pHypo->dNearestDist  = 0.;
   pHypo->dFirstPTime   = 0.;
   pHypo->iGoodSoln     = 0;
   pHypo->dMbAvg        = 0.0;
   pHypo->dMlAvg        = 0.0;
   pHypo->dMSAvg        = 0.0;
   pHypo->dMwAvg        = 0.0;
   pHypo->dMwpAvg       = 0.0;
   pHypo->iNumMb        = 0;
   pHypo->iNumMl        = 0;
   pHypo->iNumMS        = 0;
   pHypo->iNumMw        = 0;
   pHypo->iNumMwp       = 0;
   pHypo->iNumMbClip    = 0;
   pHypo->iNumMlClip    = 0;
   pHypo->iNumMSClip    = 0;
   pHypo->iNumMwpClip   = 0;
   pHypo->iNumMwClip    = 0;
   strcpy( pHypo->szLat, "\0" );
   strcpy( pHypo->szLon, "\0" );
   strcpy( pHypo->szOTimeRnd, "\0" );
}

     /**************************************************************
      * LoadHypo()                                *
      * *
      * Fill in HYPO structure from QuakeFile.                     *
      * *
      * Arguments:                                                 *
      * pszQFile    Quake file name                               *
      * Hypo        Hypocenter data structure                     *
      * *
      **************************************************************/

void LoadHypo( char *pszQFile, HYPO Hypo[] )	  
{
   double  dAzm;                        /* Azimuthal coverage */
   double  dTemp;                       /* Dummy variable */
   FILE    *hFile;                      /* File handle */
   int     i;
   LATLON  ll;                          /* Input location */

/* Open quake file */
   if ( (hFile = fopen( pszQFile, "r" )) == NULL )
   {
      logit ("t", "Quake file, %s, not opened.\n", pszQFile);
      return;
   }
   
/* Read in quake information (PARCHADO A %d PARA VARIABLES INT EN 64 BITS) */   
   for ( i=0; i<MAX_QUAKES; i++ )
   {
      if ( fscanf( hFile, "%lf %lf %lf %lf %d %s %lf %d %d %d %lf"
                          " %lf %lf %d %lf %d %lf %d %lf %d %lf %d"
                          " %lf %d\n",
                   &Hypo[i].dOriginTime, &ll.dLat, &ll.dLon,
                   &Hypo[i].dPreferredMag, &Hypo[i].iNumPMags,
                    Hypo[i].szPMagType, &Hypo[i].dDepth,
                   &Hypo[i].iQuakeID, &Hypo[i].iVersion,
                   &Hypo[i].iNumPs, &Hypo[i].dAvgRes, &dAzm,
                   &Hypo[i].dMbAvg, &Hypo[i].iNumMb,
                   &Hypo[i].dMlAvg, &Hypo[i].iNumMl,
                   &Hypo[i].dMSAvg, &Hypo[i].iNumMS,
                   &Hypo[i].dMwpAvg, &Hypo[i].iNumMwp,
                   &Hypo[i].dMwAvg, &Hypo[i].iNumMw,
                   &dTemp, &Hypo[i].iGoodSoln ) != 24 ) break;
      if ( Hypo[i].dOriginTime == 0. ) break;
      GeoCent( &ll );
      Hypo[i].dLat = ll.dLat;
      Hypo[i].dLon = ll.dLon;
      Hypo[i].dCoslat = cos( Hypo[i].dLat );
      Hypo[i].dSinlat = sin( Hypo[i].dLat );
      Hypo[i].dCoslon = cos( Hypo[i].dLon );
      Hypo[i].dSinlon = sin( Hypo[i].dLon );
	  Hypo[i].iAzm = (int) dAzm;
      Hypo[i].iMagOnly = 0;
   }
   fclose( hFile );
}
