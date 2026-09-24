#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <earthworm.h>
#include <transport.h>
#include "loc_wcatwc.h"

/* fPP defined in iaspei91.h.  Must be defined as extern for compilation */
extern float fPP[27436];  
extern char  szStnRem[MAX_PBUFFS][MAX_STN_REM][TRACE_STA_LEN];/* Removed stns */

/* --- INICIO PARCHE DE 64 BITS (PROTOTIPOS MATEMATICOS FALTANTES) --- */
/* Declarar estas funciones evita que GCC asuma que retornan int y trunque los punteros de 64 bits */
void InitialLocator( int, int, int, PPICK *, HYPO *, double, double );
void QuakeSolveIasp( int, PPICK *, HYPO *, EQDEPTHDATA *, int );
void IsItGoodSoln( int, PPICK *, HYPO *, int );
void FindBadPs( int, int, PPICK *, HYPO *, double, double, int, EQDEPTHDATA * );
void ZeroMagnitudes( PPICK *, int );
void ComputeMagnitudes( int, PPICK *, HYPO * );
void GetPreferredMag( HYPO * );
int  IsItSameQuake( HYPO *, HYPO * );
void QuakeLog( int, PPICK *, HYPO *, CITY *, CITY *, char *, char *, char *, char *, int, char * );
void QuakeLog2( char *, HYPO * );
int  SortAllByPTime( const void *, const void * );
void RemoveP( PPICK *, int *, int );
void LoadPagerString( HYPO *, char *, CITY *, CITY *, GPARM * );
void MakeH71Msg( HYPO *, char * );
void MakeTWCMsg( HYPO *, char * );
void NearestCitiesEC( LATLON *, CITY *, CITYDIS * );
void NearestCities( LATLON *, CITY *, CITYDIS * );
char *namnum( double, double, int *, char *, char *, char * );
int  GetRegion( double, double );
/* --- FIN PARCHE DE 64 BITS --- */

     /**************************************************************
      * AddInMw()                            *
      * *
      * This function reads the Mw data file, determines if the    *
      * Mws are from the same quake as being located, and adds Mw  *
      * information to arrays.                                     *
      * *
      * Arguments:                                                 *
      * pszFile     File with Mw results                          *
      * pHypo       Computed hypocentral parameters               * * *
      **************************************************************/
      
void AddInMw( char *pszFile, HYPO *pHypo )
{
   double  dMw[MAX_STATIONS];           /* Mw from data file */
   double  dMwSumMod;                   /* Mw summation */
   FILE    *hFile;                      /* Mw File handle */
   int     i;
   int     iMwCountMod;                 /* # Mws from file(within .6 of avg.) */
   HYPO    HypoT;                       /* Quake params. in Mw file */
   char    szNet[6], szStn[6], szChn[6];/* Stn, Channel from Mw file */

/* Initialize Mw variables */
   pHypo->dMwAvg = 0.;
   pHypo->iNumMw = 0;
   iMwCountMod = 0;
   dMwSumMod = 0.;

/* Try to open the Mw file */
   if ( (hFile = (FILE *) fopen( pszFile, "r" )) == NULL )
   {
      logit( "t", "Couldn't open %s\n", pszFile );
      return;
   }
   
/* Read Mw quake location */
   fscanf( hFile, "%lf\n", &HypoT.dLat );
   fscanf( hFile, "%lf\n", &HypoT.dLon );
   fscanf( hFile, "%lf\n", &HypoT.dOriginTime );  /* O-time in 1/1/70 seconds */

/* Is the quake that produced the Mws the same as that located here? */
   if ( IsItSameQuake( &HypoT, pHypo ) )
   {
      while ( !feof( hFile ) )
      {                                   /* Yes, so add in Mw */
         fscanf( hFile, "%s %s %s %lf\n", szStn, szChn, szNet,
                 &dMw[pHypo->iNumMw] );
         pHypo->dMwAvg += dMw[pHypo->iNumMw];
	 pHypo->iNumMw += 1;
      }
      if ( pHypo->iNumMw ) pHypo->dMwAvg /= (double) pHypo->iNumMw;
      if ( pHypo->dMwAvg > 0. )
         for ( i=0; i<pHypo->iNumMw; i++ )
            if ( dMw[i] > 0. )
               if ( fabs( pHypo->dMwAvg-dMw[i] ) < 0.6 )
               {
                  iMwCountMod++;
                  dMwSumMod += dMw[i];
               }
      if ( iMwCountMod ) pHypo->dMwAvg = dMwSumMod / (double) iMwCountMod;
      if ( iMwCountMod ) pHypo->iNumMw = iMwCountMod;
   }
fclose (hFile);      	/* Close file and return */
return;
}

     /**************************************************************
      * CheckPBuffTimes()                      *
      * *
      * After a solution has been made, see if any other P's in    *
      * other buffers go with this location.  If so, copy them into*
      * the buffer with the latest solution.                       *
      * *
      * Also, remove Ps that had high residuals and find a new     *
      * buffer for them.                                           *
      * *
      * June, 2007: Added code to better associate P-picks using   *
      * nearest station approach.                      *
      * Sept., 2004: When picks are dropped due to bad residual    *
      * (but not too bad - i.e. bad P-pick) don't move*
      * to another buffer; just eliminate it.         *
      * *
      * Arguments:                                                 *
      * pPBuf       P-pick buffers to copy PStruct into           *
      * iBufCnt     Number of P-picks in each buffer so far       *
      * Hypo        Computed hypocenters for each buffer          *
      * iActive     Last buffer to get a hypocenter solution      *
      * Gparm       Configuration parameter structure             *
      * iLastCnt    P buffer counter                              *
      * iNumRem     Number of stations permanently removed        *
      * pszPStnArray Array with nearest station lookup table      *
      * iNumPStn    Number of stations in pick file               *
      * iNumNearStn Number of near stations to compare            *
      * iMaxStn     Maximum of Ppicks per buffer                  *
      * *
      **************************************************************/
	  
void CheckPBuffTimes( PPICK **pPBuf, int iPBufCnt[], HYPO Hypo[], int iActive,
                      GPARM *Gparm, int iLastCnt[], int iNumRem[],
                      char pszPStnArray[][MAX_NUM_NEAR_STN][TRACE_STA_LEN],
                      int iNumPStn, int iNumNearStn, int iMaxStn )
{
   AZIDELT azidelt;                /* Distance/azimuth between 2 points */
   double  dCorr;                  /* Distance correction for P tables */
   double  dMin;                   /* Minimum P-time in all buffers */
   double  dTimeExpected;          /* P-time for this station */
   double  dTTDif;                 /* P-time difference between 2 points */
   double  dTTDifMax;              /* Max P-time diff. given delta for 2 pts. */
   double  dTTDifQ;                /* Expected P time for this point */
   int     i, j, k, kk, iNum, iTemp;/* Index counters */
   int     iAlreadyUsed;           /* 1->This buffer already has been ordered */
   int     iBuff[MAX_PBUFFS];      /* Order of buffers (Starting with buffer
                                      with the most picks, buffers with a good
                                      location are designated -2) */
   static  int  iDLev;             /* Index of 0 deg. in desired depth level */
   int     iMatch, iPCnt, iNearbyPMatch;/* Flags to indicate correct buffer */
   int     iMin;                   /* Index of buffer with minimum P-time */
   int     iStnIndex;              /* Index of this P in nearby station table */
   long    lTime;                  /* Preset 1/1/70 time in seconds */   
	   
/* Remove picks with high residual */	   
   if ( iPBufCnt[iActive] > Gparm->MinPs+1 )
      for ( k=0; k<iPBufCnt[iActive]; k++ )
      {
/* First, check to see if this looks like a bad P-pick for this event.
   That is, the pick should be associated with this event, but the P-time
   is off by 5-20 seconds (but the pick was not forced in by manual pick
   in hypo_display).  If it does seem like a bad P, remove it and send it
   nowhere so that it does not join up with other bad Ps to make
   a false location in another buffer. */   	  
         if ( pPBuf[iActive][k].iUseMe < 2  &&
              pPBuf[iActive][k].dPTime > 0. &&
              Hypo[iActive].iGoodSoln >= 2  &&
              Hypo[iActive].iNumPs    >= 10 &&
              fabs( pPBuf[iActive][k].dRes ) > 10. &&
              fabs( pPBuf[iActive][k].dRes ) < 20. )
         {
            logit( "", "Pick %s removed completely\n",
             pPBuf[iActive][k].szStation );
            strcpy( szStnRem[iActive][iNumRem[iActive]],
                    pPBuf[iActive][k].szStation );
            iNumRem[iActive]++;
            logit( "", "%s added to Buffer %ld - %ld removed stations\n",
             szStnRem[iActive][iNumRem[iActive]-1], iActive, iNumRem[iActive] );
            goto Remove;
         }
			  
/* Now check to see if this is way out and not likely just a bad P, but
   probably associated with another quake.  Then, try other buffers. */
         if (((((fabs( pPBuf[iActive][k].dRes ) > 10. &&
             Hypo[iActive].iGoodSoln >= 1) ||
            (Hypo[iActive].iNumPs >= 10 &&
             Hypo[iActive].iGoodSoln >= 2 &&
             fabs( pPBuf[iActive][k].dRes ) > 7.5)) &&
             pPBuf[iActive][k].iUseMe < 2) ||
            (fabs( pPBuf[iActive][k].dRes ) > 180.)) &&
             pPBuf[iActive][k].dPTime > 0. )
         {
/* Find where to put pick */	 
/* Check all buffers to see if this fits in an existing solution */		 		 
            for ( i=0; i<Gparm->NumPBuffs; i++ ) iBuff[i] = -1;   
            for ( i=0; i<Gparm->NumPBuffs; i++ )
            {
               iTemp = iActive+i;
               if ( iTemp >= Gparm->NumPBuffs ) iTemp -= Gparm->NumPBuffs;
	  
/* Do we have a good sol'n for this buffer */
//               if ( Hypo[iTemp].iNumPs >= Gparm->MinPs+2 &&
               if ( Hypo[iTemp].iNumPs >= Gparm->MinPs &&
                    Hypo[iTemp].iGoodSoln >= 2 && iTemp != iActive )
               {
                  iBuff[iTemp] = -2;

/* Get depth level to use in travel time table */
                  iDLev = (int) ( (Hypo[iTemp].dDepth+0.01) /
                          (double) IASP_DEPTH_INC) * IASP_NUM_PER_DEP;
	  
/* Check the P-time with what would be expected for this quake; if it
   fits keep it.  Otherwise, go to next buffer */	  
                  azidelt = GetDistanceAz( (LATLON *) &Hypo[iTemp],
                                           (LATLON *) &pPBuf[iActive][k] );
                  dTTDif = pPBuf[iActive][k].dPTime -
                           Hypo[iTemp].dOriginTime;
                  dCorr = azidelt.dDelta*(1./IASP_DIST_INC) - 
                   floor( azidelt.dDelta*(1./IASP_DIST_INC) );
                  dTTDifQ = fPP[(int) (azidelt.dDelta*(1./IASP_DIST_INC))+
                   iDLev] + dCorr*(fPP[(int) (azidelt.dDelta*
                   (1./IASP_DIST_INC))+1+iDLev] - fPP[(int)
                   (azidelt.dDelta*(1./IASP_DIST_INC))+iDLev]);				
				   
/* Is this station already in buffer? If it is, go to next buffer*/		 
                  for ( j=0; j<iPBufCnt[iTemp]; j++ )
                     if ( !strcmp( pPBuf[iTemp][j].szStation,
                                   pPBuf[iActive][k].szStation ) &&
                          !strcmp( pPBuf[iTemp][j].szChannel,
                                   pPBuf[iActive][k].szChannel ) )
                        goto NextBuffer;
					
                  if ( fabs( dTTDifQ - dTTDif ) <= 5. )
                  {
                     if ( iPBufCnt[iTemp] < iMaxStn )
                     {
                        CopyPBuf( &pPBuf[iActive][k],
                                  &pPBuf[iTemp][iPBufCnt[iTemp]] );
                        iPBufCnt[iTemp]++;
                        if ( Gparm->Debug )
                           logit( "", "Pick Added in Check (post7) %s %lf - %ld\n",
                            pPBuf[iTemp][iPBufCnt[iTemp]-1].szStation,
                            pPBuf[iTemp][iPBufCnt[iTemp]-1].dPTime, iTemp );
                        goto Remove;
                     }
                     else
                     {
                        logit( "", "1 - buffer %ld filled with picks\n", iTemp );
                        goto Remove;
                     }
                  }
               }
NextBuffer:;}
	  
/* If we don't have a sol'n which matches this pick, try buffers to see which
   it could fit in. But first, order the buffers by Number of Ps. */
            for ( i=0; i<Gparm->NumPBuffs; i++ )
               if ( iBuff[i] != -2 ) /* Skip those where there is already a soln */
               {
                  iNum = 0;
                  for ( j=0; j<Gparm->NumPBuffs; j++ )
                     if ( iBuff[j] != -2 )
                     {
                        iAlreadyUsed = 0;
                        for ( kk=0; kk<i; kk++ )
                           if ( iBuff[kk] == j ) iAlreadyUsed = 1;
                        if ( iAlreadyUsed == 0 )
                           if ( iPBufCnt[j] > iNum )
                           {
                              iBuff[i] = j;
                              iNum = iPBufCnt[j];
                           }
                     }
               }
/* It didn't fit an existing solution, so see which buffer it can fit in */	  
            for ( i=0; i<Gparm->NumPBuffs; i++ )
            {
               iTemp = iBuff[i];
/* If we don't have a sol'n yet in buffer or there are not many picks */	  
               if ( iTemp >= 0 && iTemp != iActive )
               {              /* -2->good soln for buffer, -1->Buffer empty */
/* Here, check buffers by for a nearby station and P-time differences */	  
                  iMatch = 0;
                  iNearbyPMatch = 0;
                  iPCnt = 0;
/* Find the index of this new Ppick in nearby station lookup table */
                  for ( iStnIndex=0; iStnIndex<iNumPStn; iStnIndex++ )
                     if ( !strcmp( pPBuf[iActive][k].szStation, pszPStnArray[iStnIndex][0] ) )
                        break;
                  if ( iStnIndex == iNumPStn ) iNearbyPMatch = 1;
/* See if any of the nearby stations are already in this buffer */	 
                  for ( j=0; j<iPBufCnt[iTemp]; j++ )
                     for ( kk=1; kk<iNumNearStn; kk++ )
                        if ( !strcmp( pPBuf[iTemp][j].szStation, pszPStnArray[iStnIndex][kk] ) )
                           iNearbyPMatch = 1;   

/* Get depth level to use in travel time table */
                  iDLev = (int) ( ((double) DEPTHKM+0.01) /
                          (double) IASP_DEPTH_INC) * IASP_NUM_PER_DEP;
			  
/* Check the P-times in the buffer to see if they are within theoretical
   limits.  But only check if there is a nearby station in this buffer.  Also
   check to see if this station is already in the buffer. */		  
                  if ( iNearbyPMatch == 1 )
                     for ( j=0; j<iPBufCnt[iTemp]; j++ )
                     {
                        azidelt = GetDistanceAz( (LATLON *) &pPBuf[iActive][k],
                                                 (LATLON *) &pPBuf[iTemp][j] );
                        dTTDif = fabs( pPBuf[iActive][k].dPTime -
                                       pPBuf[iTemp][j].dPTime );
                        dTTDifMax = fPP[(int) (azidelt.dDelta*
                                    (1./IASP_DIST_INC))+1+iDLev];
                        if ( dTTDif > dTTDifMax && pPBuf[iTemp][j].dPTime > 0. )
                           iPCnt++;
                        if ( !strcmp( pPBuf[iActive][k].szStation,
                                      pPBuf[iTemp][j].szStation ) ) iMatch = 1;
                     }
/* Are we within the constraints to add the pick to this buffer */
                  if ( iMatch == 0 && iPCnt == 0 && iNearbyPMatch == 1 )	
/* We are within the constraints to add the pick to this buffer */
                  {
                     if ( iPBufCnt[iTemp] < iMaxStn )
                     {
                        CopyPBuf( &pPBuf[iActive][k],
                                  &pPBuf[iTemp][iPBufCnt[iTemp]] );
                        iPBufCnt[iTemp]++;
                        if ( Gparm->Debug )
                           logit( "", "Pick Added in Check %s %lf - %ld\n",
                            pPBuf[iTemp][iPBufCnt[iTemp]-1].szStation,
                            pPBuf[iTemp][iPBufCnt[iTemp]-1].dPTime, iTemp );
                        goto Remove;
                     }
                     else
                     {
                        logit( "", "2 - buffer %ld filled with picks\n", iTemp );
                        goto Remove;
                     }
                  }
               }
            }

/* If we get here, the pick doesn't fit in any existing buffers.  
   Try to find the first buffer available with no picks and put it there. */
            for ( i=0; i<Gparm->NumPBuffs; i++ )
            {
               iTemp = iActive+i;
               if ( iTemp >= Gparm->NumPBuffs ) iTemp -= Gparm->NumPBuffs;
               if ( iTemp != iActive )
               {
                  if ( iPBufCnt[iTemp] == 0 )
                  {
                     CopyPBuf( &pPBuf[iActive][k],
                               &pPBuf[iTemp][iPBufCnt[iTemp]] );
                     iPBufCnt[iTemp]++;
                     if ( Gparm->Debug )
                        logit( "", "Pick Added in Check (new buf) %s %lf - %ld\n",
                         pPBuf[iTemp][iPBufCnt[iTemp]-1].szStation,
                         pPBuf[iTemp][iPBufCnt[iTemp]-1].dPTime, iTemp );
                     goto Remove;
                  }
               } 
            }
			   
/* If we get here, all P buffers must have some Ps in them, so one buffer must
   be cleared out. Find the one with the oldest P's and re-init that one. */
            iMin = 0;
            dMin = 1.E20;
            time( &lTime );
            for ( i=0; i<Gparm->NumPBuffs; i++ )
               if ( Hypo[i].dMSAvg == 0. ||
                  ((double) lTime-Hypo[i].dOriginTime) > 7200.)
                  if ( i != iActive )
                     for ( j=0; j<iPBufCnt[i]; j++ )
                        if ( pPBuf[i][j].dPTime < dMin &&
                             pPBuf[i][j].dPTime > 0.0)
                        {
                           dMin = pPBuf[i][j].dPTime;
                           iMin = i;
                        }
            iPBufCnt[iMin] = 0;
            iNumRem[iMin] = 0;
            iLastCnt[iMin] = 0;
            Hypo[iMin].iVersion = 1;
            Hypo[iMin].iAlarmIssued = 0;
            Hypo[iMin].iQuakeID += Gparm->NumPBuffs;
            if ( Hypo[iMin].iQuakeID >= 10000 ) Hypo[iMin].iQuakeID -= 10000;
            for ( i=0; i<iMaxStn; i++ ) InitP( &pPBuf[iMin][i] );
            InitHypo( &Hypo[iMin] );
            CopyPBuf( &pPBuf[iActive][k], &pPBuf[iMin][iPBufCnt[iMin]] );
            iPBufCnt[iMin]++;
            if ( Gparm->Debug )
               logit( "", "Pick Added in Check (zeroed buf) %s %lf - %ld\n",
                pPBuf[iMin][iPBufCnt[iMin]-1].szStation,
                pPBuf[iMin][iPBufCnt[iMin]-1].dPTime, iMin );
	    
/* We've found a home for the bad P, so remove it from the active buffer */
Remove:     RemoveP( pPBuf[iActive], &iPBufCnt[iActive], k );
            k--;             /* Yes, the loop index is being decremented */
         }
      }

/* Next, bring in other picks if we're pretty sure this is a good location */
   if ( Hypo[iActive].iNumPs >= Gparm->MinPs && Hypo[iActive].iGoodSoln >= 2 )
   {
/* Get depth level to use in travel time table */
      iDLev = (int) ( (Hypo[iActive].dDepth+0.01) /
              (double) IASP_DEPTH_INC) * IASP_NUM_PER_DEP;
   
/* Loop through all other buffers */
      for ( i=0; i<Gparm->NumPBuffs; i++ )
         if ( i != iActive )
            for ( j=0; j<iPBufCnt[i]; j++ )
/* If pick already included in a good sol'n, don't move it around */			
               if ( ((Hypo[i].iGoodSoln >= 2 && pPBuf[i][j].iUseMe < 1) ||
                      Hypo[i].iGoodSoln < 2) && pPBuf[i][j].dPTime > 0. &&
                      Hypo[i].iNumPs < MAX_SCAVENGE )
               {				   
/* Is this station already in buffer? If it is, go to next buffer*/		 
                  for ( k=0; k<iPBufCnt[iActive]; k++ )
                     if ( !strcmp( pPBuf[i][j].szStation,
                                   pPBuf[iActive][k].szStation ) &&
                          !strcmp( pPBuf[i][j].szChannel,
                                   pPBuf[iActive][k].szChannel ) )
                        goto NextP;
						
/* Compute what the P-time should be for this site from the last computed
   quake */			
                  azidelt = GetDistanceAz( (LATLON *) &Hypo[iActive],
                                           (LATLON *) &pPBuf[i][j] );
                  dCorr = azidelt.dDelta*(1./IASP_DIST_INC) - 
                   floor( azidelt.dDelta*(1./IASP_DIST_INC) );
                  dTimeExpected = Hypo[iActive].dOriginTime +
                   fPP[(int) (azidelt.dDelta*(1./IASP_DIST_INC))+iDLev] +
                   dCorr*(fPP[(int) (azidelt.dDelta*(1./IASP_DIST_INC))+1+iDLev]
                   - fPP[(int) (azidelt.dDelta*(1./IASP_DIST_INC))+iDLev]);				
/* If the P-time at this station fits what is expected, it maybe associated
   with this quake.  Add it to the Active buffer and remove from old. */				
                  if ( fabs( dTimeExpected-pPBuf[i][j].dPTime ) <= 5. )
                  {
                     if ( iPBufCnt[iActive] < iMaxStn )
                     {
                        CopyPBuf( &pPBuf[i][j], &pPBuf[iActive][iPBufCnt[iActive]] );
                        iPBufCnt[iActive]++;
                        if ( Gparm->Debug )
                           logit( "o", "Moved %s from %ld to %ld; timedif=%lf\n",
                            pPBuf[i][j].szStation, i, iActive,
                            dTimeExpected-pPBuf[i][j].dPTime );
                        RemoveP( pPBuf[i], &iPBufCnt[i], j );
                     }
                     else
                     {
                        logit( "", "3 - buffer %ld filled with picks\n", iActive );
                        RemoveP( pPBuf[i], &iPBufCnt[i], j );
                     }
                  }
NextP:;        }
   }
}			

      /******************************************************************
       * LocateQuake()                         *
       * *
       * This function call the functions necessary to locate an       *
       * earthquake given a set of P arrival times.  The hypocenter    *
       * parameters are returned in a structure argument.              *
       * *
       * December, 2007: Add check to see if there was a recent large  *
       * quake and whether this has nearby stations.   *
       * April, 2006: Added region of reliability to locator for local *
       * versions of the locator.                         *
       * Also, removed RESPOND due to 24x7 coverage.      *
       * *
       * Arguments:                                                    *
       * pPBuf            One of the P Buffer structures              *
       * piPBufCnt        Number of Ps in this structure              *
       * Gparm            Configuration parameter structure           *
       * pHypo            Computed hypocentral parameters             *
       * iIndex           pPbuf index within array                    *
       * Ewh              Earthworm.d defines                         *
       * pcity            Array of reference city information         *
       * iLoc             1=locate quake, 0=mags only                 *
       * HypoStruct       Computed hypocenters for each buffer        *
       * iBufCntStruct    Number of P-picks in each buffer so far     *
       * pcityEC          Array of reference eastern city information *
       * pEqDepth         Array of depth data                         *
       * iNumMax          Maximum stations per p buffer               *
       * *
       * Return:                                                       *
       * -1 if problem in location; otherwise ActiveBuffer index      *
       * *
       ******************************************************************/
	   
int LocateQuake( PPICK *pPBuf, int *piPBufCnt, GPARM *Gparm, HYPO *pHypo,
                 int iIndex, EWH *Ewh, CITY *pcity, int iLoc, HYPO HypoStruct[],
                 int iBufCntStruct[], CITY *pcityEC, EQDEPTHDATA pEqDepth[],
                 int iNumMax )
{
   FILE    *hFile;          /* File handle */
   static  HYPO    HypoLast;/* Hypo parameters from last alarmed quake */
   int     i, j, iCnt;
   int     iBadPIndices[16];/* Indices of stations tossed out by FindBadP */
   int     iBufferMatch;    /* Index of P buffer with hypocenter same as this */
   static int iInRegion;    /* 1->location within proper region; 0->otherwise */
   int     iFirst;          /* 1-> First time through locator */
   int     iMatch;          /* index of present quake in OldQuake File */										
   int     iNewAlarm;       /* 1->location/mag different than last alarm */
   int     iNumNewPs;       /* Number of new Ps since last location */
   int     iOrigGoodSoln;   /* This buffer's location quality before location */
   int     iPCnt;           /* number of useable P-data points */										
   static int iRegion;      /* Quake epicenter region (WC/ATWC AOR) */
   static int iRespond;     /* 1=>this quake was large enough to email out */
   int     iSendIt;         /* 1->Big enough to put on alarm, 0->Don't send */
   static  LATLON ll, ll2;  /* Quake lat/lon in geographic coords */
   static  OLDQUAKE OldQuake[MAX_QUAKES]; /* Previous auto-located quakes */
   STATION *Sta = NULL;     /* Dummy variable for ReportAlarm call */
   char    szFile[128];      /* Created file name for P-time data */
   char    szPageMsg[256];  /* HYPO71SUM2K message for output */
   char    szMessage[512];  /* Pager message */
   char    szLastSta[5][8]; /* Latest station added to location */
   char    szTemp[32];      /* Temporary string */
   char    szTWCMsg[MAX_HYPO_SIZE]; /* TYPE_HYPOTWC message for output */
   
   logit("e", "DEBUG LQ 1: Entrando a LocateQuake (Picks: %d, iLoc: %d)\n", *piPBufCnt, iLoc);

   iFirst = 1;
   if ( iLoc == 1 ) pHypo->iMagOnly = 0;
   else             pHypo->iMagOnly = 1;

/* Reset previously automatically eliminated stations */
   if ( iLoc == 1 )
   {
      for ( i=0; i<*piPBufCnt; i++ )
         if ( pPBuf[i].iUseMe == 0 ) pPBuf[i].iUseMe = 1;

/* Save this information in case a good solution goes bad */
      iOrigGoodSoln = pHypo->iGoodSoln;
      if ( (pHypo->iNumPs+pHypo->iNumBadPs) >= Gparm->MinPs )
      {
         iNumNewPs = *piPBufCnt - (pHypo->iNumPs+pHypo->iNumBadPs);
         if ( iNumNewPs > 5 ) iNumNewPs = 5;
         for ( i=0; i<iNumNewPs; i++ )
            strcpy( szLastSta[i], pPBuf[*piPBufCnt-(i+1)].szStation );
      }
/* Are there are enough p-times to locate quake? */ 
ReTry:            
      iPCnt = 0;
      for ( i=0; i<*piPBufCnt; i++ )
         if ( pPBuf[i].iUseMe > 0 && pPBuf[i].dPTime > 0. ) iPCnt++;
      if ( iPCnt < Gparm->MinPs )
      {
         logit( "", "Not enough Ps; need %ld, have %ld; buffer %ld; PCnt %ld\n", 
                     Gparm->MinPs, *piPBufCnt, iIndex, iPCnt );
         if ( Gparm->Debug )
            for ( i=0; i<*piPBufCnt; i++ )
               logit( "", "i=%ld, iUseMe=%ld, PTime=%lf\n", i, pPBuf[i].iUseMe,
                      pPBuf[i].dPTime );
         return -1;
      }
	  
/* Order Ps within array from first to last */	  
      logit("e", "DEBUG LQ 2: Llamando a qsort para ordenar %d picks\n", *piPBufCnt);
      qsort( (void *) pPBuf, *piPBufCnt, sizeof( PPICK ), SortAllByPTime );
	
/* First locate quake letting initial loc. be first P station */
      pHypo->iDepthControl = 3;                           /* Fixed at 40km */
      pHypo->dDepth = DEPTHKM;                        /* Set for 40 kilometers */
      InitHypo( pHypo );                                 /* Init structure */
      
      logit("e", "DEBUG LQ 3: Llamando a InitialLocator (Metodo 1)\n");
      InitialLocator( *piPBufCnt, 3, 1, pPBuf, pHypo, 0., 0. );/* Get start pt. */
      
      logit("e", "DEBUG LQ 4: Llamando a QuakeSolveIasp (Metodo 1)\n");
      QuakeSolveIasp( *piPBufCnt, pPBuf, pHypo, pEqDepth, 1 ); /* Solve location */
      
      logit("e", "DEBUG LQ 5: Llamando a IsItGoodSoln\n");
      IsItGoodSoln( *piPBufCnt, pPBuf, pHypo, Gparm->MinPs );/* Check Residual */
   
/* See if location was OK */
      if ( pHypo->iGoodSoln != 3 )
   
/* If not, re-locate using tripartite initial locator */
      {
         logit("e", "DEBUG LQ 6: Solucion no fue 3. Llamando a InitialLocator (Metodo 2)\n");
         InitHypo( pHypo );		
         InitialLocator( *piPBufCnt, 3, 2, pPBuf, pHypo, 0., 0. );
         QuakeSolveIasp( *piPBufCnt, pPBuf, pHypo, pEqDepth, 1 );
         IsItGoodSoln( *piPBufCnt, pPBuf, pHypo, Gparm->MinPs );
      }
   
/* If this solution was no good, try the Bad P finder
   (NOTE: NumPicks <= 6 => 1 knock-out; <= 10 => 2 ko; > 10 => 3 */
      if ( pHypo->iGoodSoln != 3 && MAX_TO_KO > 0 )
      {
         logit("e", "DEBUG LQ 7: Llamando a FindBadPs\n");
         InitHypo( pHypo );		
         FindBadPs( *piPBufCnt, 3, pPBuf, pHypo, 0., 0., Gparm->MinPs, pEqDepth );
         IsItGoodSoln( *piPBufCnt, pPBuf, pHypo, Gparm->MinPs );
      }
   
/* See if location OK, there are many picks, and good azimuthal control;
   if so, let the depth float. */
      if ( pHypo->iGoodSoln > 0 && pHypo->iAzm >= 180 &&
           pHypo->iNumPs >= Gparm->MinPs+2 )
      {
         logit("e", "DEBUG LQ 8: Liberando profundidad (Depth float)\n");
         pHypo->iDepthControl = 4;                        /* Float */
/* Bring eliminated picks back in */
         iCnt = 0;
         for ( i=0; i<*piPBufCnt; i++ )
            if ( pPBuf[i].iUseMe == 0 && pPBuf[i].dRes < 20. )
            {
               pPBuf[i].iUseMe = 1;
               iBadPIndices[iCnt] = i;
               iCnt++;
               if ( iCnt > 15 ) break;
            }
         QuakeSolveIasp( *piPBufCnt, pPBuf, pHypo, pEqDepth, 1 );
         IsItGoodSoln( *piPBufCnt, pPBuf, pHypo, Gparm->MinPs );
/* If this solution bombs, go back to fixed depth */
         if  ( pHypo->iGoodSoln < 2 )
         {
/* Remove originally eliminated picks */
            for ( i=0; i<iCnt; i++ )
               pPBuf[iBadPIndices[i]].iUseMe = 0;
            pHypo->iDepthControl = 3;                 /* Fixed at 40km */
            pHypo->dDepth = DEPTHKM;                  /* Set for 40 km */
            InitialLocator( *piPBufCnt, 3, 1, pPBuf, pHypo, 0., 0. );
            QuakeSolveIasp( *piPBufCnt, pPBuf, pHypo, pEqDepth, 1 );/* Solve location */
            IsItGoodSoln( *piPBufCnt, pPBuf, pHypo, Gparm->MinPs );/* Check Res */
         }
      }
/* If this location was good last time and went bad here, remove the last picks */     
      if ( iOrigGoodSoln >= 2 && pHypo->iGoodSoln <= 1 &&
           *piPBufCnt > Gparm->MinPs+2 && iFirst == 1 )
      {
         logit("e", "DEBUG LQ 9: Descartando ultimo pick y reintentando (ReTry)\n");
         for ( j=0; j<iNumNewPs; j++ )
            for ( i=0; i<*piPBufCnt; i++ )
               if ( !strcmp( szLastSta[j], pPBuf[i].szStation ) )
               {
                  RemoveP( pPBuf, piPBufCnt, i );
                  logit( "", "Latest pick, %s, induced bad loc. and removed\n",
                         szLastSta[j] );
               }
         iFirst = 0;
         goto ReTry;
      }
   }
   else    /* Zero res for MS only data */
   {
      for ( i=0; i<*piPBufCnt; i++ )
         if ( pPBuf[i].dPTime <= 1. ) pPBuf[i].dRes = 0.;
   }
   
/* We must have a solution now, good or not, so get magnitudes */   
   logit("e", "DEBUG LQ 10: Calculando magnitudes...\n");
   ZeroMagnitudes( pPBuf, iNumMax );
   ComputeMagnitudes( *piPBufCnt, pPBuf, pHypo );
   AddInMw( Gparm->szMwFile, pHypo );
   GetPreferredMag( pHypo );
   
/* Is this the same quake as is being located in another buffer? */
   iBufferMatch = -1;
   for ( i=0; i<Gparm->NumPBuffs; i++ )
      if ( i != iIndex && (HypoStruct[i].dLat != 0. || HypoStruct[i].dLon != 0.) )	  
         if ( IsItSameQuake( pHypo, &HypoStruct[i] ) == 1 )
            if ( iBufCntStruct[i] > *piPBufCnt )   /* Who has more picks */
               iBufferMatch = i;
   if ( iBufferMatch >= 0 ) logit( "et", "Buffer match - %ld\n", iBufferMatch );
   
/* Has there been a recent large quake and does this location not have any
   near (<40 deg) stations? If so, reset iGoodSoln to 1. */
   for ( i=0; i<Gparm->NumPBuffs; i++ )
      if ( i != iIndex && HypoStruct[i].dPreferredMag > 5.8 &&
           pHypo->dNearestDist > 40. && pHypo->iGoodSoln >= 2 &&
           pHypo->dOriginTime-HypoStruct[i].dOriginTime < 3600. &&
           pHypo->dOriginTime-HypoStruct[i].dOriginTime > 0. )	  
      {
         pHypo->iGoodSoln = 1;
         logit( "t", "Good Soln drop to 1 - recent large event %ld\n", iIndex );
      }

/* Is the quake location within the region that this locator is solving? */
   iInRegion = 0;
   GeoGraphic( &ll, (LATLON *) pHypo );
   iRegion = GetRegion( ll.dLat, ll.dLon );     /* For use later */
   if ( ll.dLon > 180. ) ll.dLon -= 360.;
   if ( Gparm->SouthernLat <= ll.dLat &&
        Gparm->NorthernLat >= ll.dLat &&
        Gparm->WesternLon <= ll.dLon &&
        Gparm->EasternLon >= ll.dLon ) iInRegion = 1;
   
/* Re-write the file monitored by ANALYZE with time of first P */
   if ( iLoc == 1 && pHypo->iVersion == 1 && iBufferMatch < 0 && iInRegion == 1 )
   {
      logit("e", "DEBUG LQ 11: Escribiendo en %s...\n", Gparm->szAutoLoc);
      if ( (hFile = fopen( Gparm->szAutoLoc, "w" )) != NULL )
      {
         fprintf( hFile, "%lf\n", pHypo->dFirstPTime );
         fclose( hFile );
      }
      else
         logit( "t", "Failed to open %s in AutoLoc\n", Gparm->szAutoLoc );
   }
	
/* If good sol'n, more than MinPs-1 picks used in location, < 13 picks, active
   location, in the proper region, and not just an Ms update, update the
   dummy file and P-time files. */
   if ( pHypo->iGoodSoln >= 2 && pHypo->iNumPs >= Gparm->MinPs &&
        iBufferMatch < 0  && iLoc == 1 && pHypo->iNumPs < 15 && iInRegion == 1 )
   {
      logit("e", "DEBUG LQ 12: Escribiendo Dummy Data en %s...\n", Gparm->szDummyFile);
      WriteDummyData( pHypo, Gparm->szDummyFile, 1, 1 );
   }
   if ( pHypo->iGoodSoln >= 2 && pHypo->iNumPs >= Gparm->MinPs &&
        iBufferMatch < 0  && iLoc == 1 && iInRegion == 1 )
   {	
      logit("e", "DEBUG LQ 13: Escribiendo P-Time Files (%s)...\n", Gparm->szRTPFile);
      WritePTimeFile( pHypo->iNumPs, pPBuf, Gparm->szRTPFile );      
      strcpy( szFile, Gparm->szPFilePath );      
      sprintf( szTemp, "%d", pHypo->iQuakeID );     /* Hypo ID - PARCHADO A %d */
      PadZeroes( 4, szTemp );
      strcat( szFile, szTemp );
      strcat( szFile, ".dat" );
      WritePTimeFile( pHypo->iNumPs, pPBuf, szFile );      
   }

/* If good sol'n and more than MinPs-1 picks used in location, fill H71SUM2K
   format and report event to hypo ring. */
   if ( pHypo->iGoodSoln >= 2 && pHypo->iNumPs >= Gparm->MinPs &&
        iBufferMatch < 0 && iInRegion == 1 )
   {
      logit("e", "DEBUG LQ 14: Enviando reportes al anillo...\n");
      MakeH71Msg( pHypo, szPageMsg );
      MakeTWCMsg( pHypo, szTWCMsg );
      ReportHypo( szPageMsg, szTWCMsg, Gparm->MyModId, Gparm->OutRegion,
                  Ewh->TypeHypoTWC, Ewh->TypeH71Sum2K, Ewh->MyInstId,
                  iLoc, pHypo->iNumPs );
   }
	  
/* Send a message to the alarm ring if warranted */
   if ( pHypo->iNumPs >= Gparm->MinPs && pHypo->iGoodSoln >= 2 && iLoc == 1 &&
        iBufferMatch < 0 && iInRegion == 1 )
   {        /* Is this quake large enough to respond to? */
      iRespond = 0;
      iSendIt = 0;
      if ( ((((iRegion < 5 || iRegion == 8)  && pHypo->dPreferredMag >= 6.0) ||
           (iRegion >= 10 && iRegion <= 13 && pHypo->dPreferredMag >  5.5) ||
           ((iRegion == 5 || iRegion == 9) && pHypo->dPreferredMag >= 6.0) ||
           (iRegion >= 14 && pHypo->dPreferredMag >= 6.0)) &&
            pHypo->iNumPs < 32 && (pHypo->iNumPs%6) == 0) ||
           (pHypo->dPreferredMag >= Gparm->MinMagToSend &&
            pHypo->iAlarmIssued == 0))
         iRespond = 1;
      if ( ((iRegion < 5 || iRegion == 8)  && pHypo->dPreferredMag >= 3.2) ||
           (iRegion >= 10 && iRegion <= 13 && pHypo->dPreferredMag >  3.2) ||
           ((iRegion == 5 || iRegion == 9) && pHypo->dPreferredMag >= 5.5) ||
           (iRegion >= 14 && pHypo->dPreferredMag >= 5.5) ) iSendIt = 1;
/* Is this a significantly different location/magnitude than the last
   alarm message sent out */                     
      GeoGraphic( &ll2, (LATLON *) &HypoLast );
      while ( ll2.dLon > 180. ) ll2.dLon -= 360.;
      iNewAlarm = 1; 	  
      if ( fabs( ll2.dLat-ll.dLat ) < 0.5 &&
           fabs( ll2.dLon-ll.dLon ) < 0.5 &&
           fabs( HypoLast.dPreferredMag-pHypo->dPreferredMag ) < 0.2 &&
           fabs( HypoLast.dOriginTime-pHypo->dOriginTime ) < 5.0 &&
           abs( HypoLast.iNumPs-pHypo->iNumPs ) < 5 ) iNewAlarm = 0;
      if ( pHypo->iNumPs < 31 && iSendIt )
      {                     
         if ( iNewAlarm == 1 ) 
         {                   /* Send message if it is different, or a respond */
            strcpy( szMessage, "\0" );
            LoadPagerString( pHypo, szMessage, pcity, pcityEC, Gparm );
            ReportAlarm( Sta, Gparm->MyModId, Gparm->AlarmRegion,
                Ewh->TypeAlarm, Ewh->MyInstId, 4, szMessage, iRespond );
            HypoLast = *pHypo;
         }
      }
   }
   
/* Set iAlarmIssued to 2 if no alarm has gone out yet (this will prevent
   "POOR LOC..." alarm from going out */
   if ( pHypo->iNumPs > 11 && pHypo->iGoodSoln >= 2 &&
        pHypo->iAlarmIssued == 0 && iBufferMatch < 0 && iInRegion == 1 )	  
      pHypo->iAlarmIssued = 2;
	    
/* Lots of picks for this event, but not a good solution. If enough
   picks, call in the troops */	  
   if ( pHypo->iNumPs > 12 && pHypo->iGoodSoln < 2 &&
        pHypo->iAlarmIssued == 0 && iLoc == 1 && iBufferMatch < 0 )
   {
      strcpy( szMessage, "\0" );
      sprintf( szMessage, "POOR LOCATION; %d STATIONS", pHypo->iNumPs );
      ReportAlarm( Sta, Gparm->MyModId, Gparm->AlarmRegion,
                   Ewh->TypeAlarm, Ewh->MyInstId, 4, szMessage, 0 );
      pHypo->iAlarmIssued = 1;
   }
	    
/* Send alarm if respond not yet sent and MS > 6.2 & reasonable number of Ms's*/
   if ( iLoc == 0 && pHypo->iAlarmIssued == 0 &&
        pHypo->dMSAvg > 6.2 && pHypo->iNumMS > 3 && iInRegion == 1 )
   {
      if ( iRegion < 20 )                            /* Pacific & Carib. only */
      {
         strcpy( szMessage, "\0" );
         LoadPagerString( pHypo, szMessage, pcity, pcityEC, Gparm );
         ReportAlarm( Sta, Gparm->MyModId, Gparm->AlarmRegion,
                      Ewh->TypeAlarm, Ewh->MyInstId, 4, szMessage, 0 );
         pHypo->iAlarmIssued = 1;
      }
   }

/* Only update last quake file if it looks like a good sol'n */
   if ( pHypo->iGoodSoln >= 2 && pHypo->iNumPs >= Gparm->MinPs &&
        iBufferMatch < 0 && iInRegion == 1 )
   {
      logit("e", "DEBUG LQ 15: Entrando a bloque OldQuakes (%s)...\n", Gparm->szOldQuakes);
      if ( (hFile = fopen( Gparm->szOldQuakes, "r" )) != NULL )
/* First read in old quake data */
      {
         for ( i=0; i<MAX_QUAKES; i++ ) {
            /* --- INICIO PARCHE DE 64 BITS EN FSCANF --- */
            if ( fscanf( hFile, "%lf %lf %lf %lf %d %s %d %d %d %d %lf "
                                "%lf %lf %d %lf %d %lf %d %lf %d %lf %d "
                                "%lf %d\n",
                &OldQuake[i].dOTime, &OldQuake[i].dLat, &OldQuake[i].dLon,
                &OldQuake[i].dPreferredMag, &OldQuake[i].iNumPMags,
                OldQuake[i].szPMagType, &OldQuake[i].iDepth,
                &OldQuake[i].iQuakeID, &OldQuake[i].iVersion,
                &OldQuake[i].iNumPs, &OldQuake[i].dAvgRes,
                &OldQuake[i].dAzm, &OldQuake[i].dMbAvg, &OldQuake[i].iNumMb,
                &OldQuake[i].dMlAvg, &OldQuake[i].iNumMl,
                &OldQuake[i].dMSAvg, &OldQuake[i].iNumMS,
                &OldQuake[i].dMwpAvg, &OldQuake[i].iNumMwp,
                &OldQuake[i].dMwAvg, &OldQuake[i].iNumMw,
                &OldQuake[i].d1stPTime, &OldQuake[i].iGoodSoln ) != 24 ) break;
            /* --- FIN PARCHE DE 64 BITS --- */
         }
         fclose( hFile );
			
/* Determine if this is the same quake as one already written to szOldQuake */
         iMatch = -1;
         for ( i=0; i<MAX_QUAKES; i++ )
            if ( pHypo->iQuakeID == OldQuake[i].iQuakeID &&
                 pHypo->iVersion >= OldQuake[i].iVersion &&  /* Probably same */
                 pHypo->dOriginTime < OldQuake[i].dOTime + 1200. &&
                 pHypo->dOriginTime > OldQuake[i].dOTime - 1200. )
               iMatch = i;			
               
         hFile = fopen( Gparm->szOldQuakes, "w" );
         if ( iMatch == -1 )          /* No Match - Put new one at top */
         {
            /* --- INICIO PARCHE DE 64 BITS EN FPRINTF --- */
            fprintf( hFile, "%lf %lf %lf %lf %d %s %d %d %d %d %lf "
                            "%lf %lf %d %lf %d %lf %d %lf %d %lf %d "
                            "%lf %d\n",
                     pHypo->dOriginTime, ll.dLat, ll.dLon, pHypo->dPreferredMag,
                     pHypo->iNumPMags, pHypo->szPMagType,
                     (int) (pHypo->dDepth + 0.5), pHypo->iQuakeID, 
                     pHypo->iVersion, pHypo->iNumPs, 
                     pHypo->dAvgRes, (double) pHypo->iAzm, pHypo->dMbAvg,
                     pHypo->iNumMb, pHypo->dMlAvg, pHypo->iNumMl, pHypo->dMSAvg,
                     pHypo->iNumMS, pHypo->dMwpAvg, pHypo->iNumMwp,
                     pHypo->dMwAvg, pHypo->iNumMw,
                     pHypo->dFirstPTime, pHypo->iGoodSoln );
            for ( i=0; i<MAX_QUAKES-1; i++ ) {
               if (OldQuake[i].dOTime <= 0.0) break; /* Avoid printing garbage */
               fprintf( hFile, "%lf %lf %lf %lf %d %s %d %d %d %d %lf "
                               "%lf %lf %d %lf %d %lf %d %lf %d %lf %d "
                               "%lf %d\n",
                        OldQuake[i].dOTime, OldQuake[i].dLat, OldQuake[i].dLon,
                        OldQuake[i].dPreferredMag, OldQuake[i].iNumPMags,
                        OldQuake[i].szPMagType, OldQuake[i].iDepth,
                        OldQuake[i].iQuakeID, OldQuake[i].iVersion,
                        OldQuake[i].iNumPs, OldQuake[i].dAvgRes,
                        OldQuake[i].dAzm, OldQuake[i].dMbAvg,OldQuake[i].iNumMb,
                        OldQuake[i].dMlAvg, OldQuake[i].iNumMl,
                        OldQuake[i].dMSAvg, OldQuake[i].iNumMS,
                        OldQuake[i].dMwpAvg, OldQuake[i].iNumMwp,
                        OldQuake[i].dMwAvg, OldQuake[i].iNumMw,
                        OldQuake[i].d1stPTime, OldQuake[i].iGoodSoln );
            }
            /* --- FIN PARCHE DE 64 BITS --- */
         }
         else             /* Re-write file patching in updated location */
         {         		
            for ( i=0; i<MAX_QUAKES; i++ ) {
               if ( i == iMatch ) {
                  /* --- INICIO PARCHE DE 64 BITS EN FPRINTF --- */
                  fprintf( hFile, "%lf %lf %lf %lf %d %s %d %d %d %d %lf "
                           "%lf %lf %d %lf %d %lf %d %lf %d %lf %d "
                           "%lf %d\n",
                    pHypo->dOriginTime, ll.dLat, ll.dLon, pHypo->dPreferredMag,
                    pHypo->iNumPMags, pHypo->szPMagType,
                    (int) (pHypo->dDepth + 0.5), pHypo->iQuakeID, 
                    pHypo->iVersion, pHypo->iNumPs, 
                    pHypo->dAvgRes, (double) pHypo->iAzm, pHypo->dMbAvg,
                    pHypo->iNumMb, pHypo->dMlAvg, pHypo->iNumMl, pHypo->dMSAvg,
                    pHypo->iNumMS, pHypo->dMwpAvg, pHypo->iNumMwp,
                    pHypo->dMwAvg, pHypo->iNumMw,
                    pHypo->dFirstPTime, pHypo->iGoodSoln );
               } else {
                  if (OldQuake[i].dOTime <= 0.0) break;
                  fprintf( hFile, "%lf %lf %lf %lf %d %s %d %d %d %d %lf "
                           "%lf %lf %d %lf %d %lf %d %lf %d %lf %d "
                           "%lf %d\n",
                    OldQuake[i].dOTime, OldQuake[i].dLat, OldQuake[i].dLon,
                    OldQuake[i].dPreferredMag, OldQuake[i].iNumPMags,
                    OldQuake[i].szPMagType, OldQuake[i].iDepth,
                    OldQuake[i].iQuakeID, OldQuake[i].iVersion,
                    OldQuake[i].iNumPs, OldQuake[i].dAvgRes,
                    OldQuake[i].dAzm, OldQuake[i].dMbAvg, OldQuake[i].iNumMb,
                    OldQuake[i].dMlAvg, OldQuake[i].iNumMl,
                    OldQuake[i].dMSAvg, OldQuake[i].iNumMS,
                    OldQuake[i].dMwpAvg, OldQuake[i].iNumMwp,
                    OldQuake[i].dMwAvg, OldQuake[i].iNumMw,
                    OldQuake[i].d1stPTime, OldQuake[i].iGoodSoln );
                  /* --- FIN PARCHE DE 64 BITS --- */
               }
            }
         }
         fclose( hFile );
      }
      else
         logit( "t", "Failed to open %s file in AutoLoc\n",
		         Gparm->szOldQuakes );
   }
   
/* Log results */
   if ( iLoc == 1 )
   {
      logit("e", "DEBUG LQ 16: Escribiendo el log final con QuakeLog...\n");
      QuakeLog( *piPBufCnt, pPBuf, pHypo, pcity, pcityEC, Gparm->szNameFile,
                 Gparm->szNameFileLC, Gparm->szIndexFile, Gparm->szLatFile, 1,
                 NULL );
      QuakeLog2( Gparm->szQLogFile, pHypo );
   }
   
/* Increment version */
   if ( iLoc == 1 ) pHypo->iVersion++;   
   
   logit("e", "DEBUG LQ 17: FIN DEL PROCESO. Localizacion %d exitosa.\n", pHypo->iQuakeID);
   
/* Return P buffer index */   
   if ( iBufferMatch >= 0 ) return iBufferMatch;
   else                     return iIndex;
}

      /******************************************************************
       * LoadPagerString()                          *
       * *
       * This function builds a quake message to send to the alarm ring.*
       * *
       * Arguments:                                                    *
       * pHypo            Computed hypocentral parameters             *
       * pszMsg           Pointer to string to hold message           *
       * pcity            Array of reference city information         *
       * pcityEC          Array of eastern reference city information *
       * Gparm            Configuration parameter structure           *
       * *
       ******************************************************************/
       
void LoadPagerString( HYPO *pHypo, char *pszMsg, CITY *pcity, CITY *pcityEC,
                      GPARM *Gparm )
{
   CITYDIS CityDis;           /* Dist/dir from nearest cities (epicenter) */
   char    cNS, cEW;          /* 'N', 'S', etc. indicators. for lat/lon */
   int     iFERegion;         /* Flinn-Engdahl Region number */
   int     iRegion;           /* Procedural Region number */
   int     iTemp;
   LATLON  ll;                /* Geographic epicenter */
   long    lTime;             /* 1/1/70 time */
   char    *psz;              /* General region of epicenter */
   char    szTemp[64];        /* Littoral epicentral location */
   static  struct  tm    *tm; /* Origin time in tm structure */
   
/* Convert o-time from 1/1/70 time to tm structure */
   lTime = (long) (pHypo->dOriginTime+0.5);
   tm = TWCgmtime( lTime );
   
/* Get nearest cities to epicenter */
   GeoGraphic( &ll, (LATLON *) pHypo );
   if ( ll.dLon < 0 ) ll.dLon += 360.;
   iRegion = GetRegion( ll.dLat, ll.dLon );
   if ( ll.dLon > 180. ) ll.dLon -= 360.;
   cNS = 'N';
   if ( ll.dLat < 0. ) cNS = 'S';
   cEW = 'E';
   if ( ll.dLon < 0. ) cEW = 'W';
   
/* Compute distance to nearest cities */   
   if ( iRegion >= 10 )                  /* WC&ATWC Eastern AOR */
      NearestCitiesEC( (LATLON *) pHypo, pcityEC, &CityDis ); 
   else
      NearestCities( (LATLON *) pHypo, pcity, &CityDis );
  
/* Is there a city within 320 miles of epicenter? */
   if ( CityDis.iDis[1] < 320 || CityDis.iDis[0] < 320 )
   {
/* Yes, there is */
      if ( CityDis.iDis[1] < CityDis.iDis[0] ) iTemp = 1;
      else                                     iTemp = 0;
      sprintf( szTemp, "%d %s %s", CityDis.iDis[iTemp],
               CityDis.pszDir[iTemp], CityDis.pszLoc[iTemp] );
   }
   else          /* There was no near-by city, so use general area */
   {
      psz = namnum( ll.dLat, ll.dLon, &iFERegion, Gparm->szIndexFile,
                    Gparm->szLatFile, Gparm->szNameFile );
      psz[strlen (psz)-1] = '\0';
      strcpy( szTemp, psz );
   }
   
/* Write the entire alarm message */
/* --- INICIO PARCHE DE 64 BITS EN SPRINTF --- */
   sprintf( pszMsg, "%s  M%s %3.1lf  %d STN  %.1lf%c %.1lf%c  "
    "%02d:%02d:%02dZ %d/%d  RES %.1lf  AZM %d ",
    szTemp, pHypo->szPMagType,
    pHypo->dPreferredMag, (int)pHypo->iNumPs, fabs( ll.dLat ), cNS, fabs( ll.dLon ),
    cEW, (int)tm->tm_hour, (int)tm->tm_min, (int)tm->tm_sec, (int)tm->tm_mon+1, (int)tm->tm_mday,
    pHypo->dAvgRes, (int)pHypo->iAzm );
/* --- FIN PARCHE DE 64 BITS --- */
}

     /**************************************************************
      * LoadUpPBuff()                      *
      * *
      * Load latest P-pick in the proper buffer.  If the pick is a *
      * repeat of a previous pick, replace the previous one (this  *
      * should have new magnitude information). Start with         *
      * iActiveBuffer and determine if the P-time fits in with the *
      * existing picks in that buffer.  If not, move to next       *
      * buffer. If it doesn't fit with any, move to the first      *
      * with no P-picks and start there.                           *
      * *
      * Two checks are made on the times before deciding which     *
      * buffer to use.  If the P-time is more than                 *
      * MaxTimeBetweenPicks from the last pick in the buffer, it   *
      * must go to the next.  Also, if it's time difference is     *
      * greater than possible (from the IASPEI91 P-time tables) to *
      * any pick in the buffer, it is moved to the next buffer.    *
      * *
      * July, 2007: Added code to discriminate phases from other   *
      * quakes which have already been located.        *
      * June, 2007: Added code to better associate P-picks using   *
      * nearest station approach.                      *
      * Sept., 2004: Check to see that the station was not         *
      * permanently removed from this buffer          *
      * *
      * Arguments:                                                 *
      * pPStruct    Structure containing input P-pick information *
      * pPBuf       P-pick buffers to copy PStruct into           *
      * iBufCnt     Number of P-picks in each buffer so far       *
      * Hypo        Computed hypocenters for each buffer          *
      * piActive    Last buffer to put a P-pick into              *
      * Gparm       Configuration parameter structure             *
      * Ewh         Earthworm.d defines                           *
      * city        reference city locations                      *
      * iLastCnt    P buffer counter                              *
      * iNumRem     Number of stations permanently removed        *
      * cityEC      Eastern reference city locations              *
      * pEqDepth    Array of depth data                           *
      * pszPStnArray Array with nearest station lookup table      *
      * iNumPStn    Number of stations in pick file               *
      * iNumNearStn Number of near stations to compare            *
      * iNumMax     Maximum number of station in P buffer         *
      * *
      **************************************************************/
	  
void LoadUpPBuff( PPICK *pPStruct, PPICK **pPBuf, int iPBufCnt[], HYPO Hypo[],
                  int *piActive, GPARM *Gparm, EWH *Ewh, CITY city[],
                  int iLastCnt[], int iNumRem[], CITY cityEC[],
                  EQDEPTHDATA pEqDepth[],
                  char pszPStnArray[][MAX_NUM_NEAR_STN][TRACE_STA_LEN],
                  int iNumPStn, int iNumNearStn, int iNumMax )
{
   AZIDELT azidelt;                /* Distance/azimuth between 2 points */
   double  dCorr;                  /* Distance correction for P tables */
   double  dMin;                   /* Oldest P-time in any buffer */
   double  dTTDif;                 /* P-time difference between 2 points */
   double  dTTDifMax;              /* Max P-time diff. given delta for 2 pts. */
   double  dTTDifQ;                /* Expected P time for this point */
   int     i, j, k, iNum, iTemp;   /* Index counters */
   int     iAlreadyUsed;           /* 1->This buffer already has been ordered */
   int     iBuff[MAX_PBUFFS];      /* Order of buffers (Starting with buffer
                                      with the most picks, buffers with a good
                                      location are designated -2) */
   int     iIndex;                 /* Index of buffer with latest P-pick */
   int     iMatch, iPCnt, iNearbyPMatch;/* Flags to indicate correct buffer */
   static  int  iDLev;             /* Index of 0 deg. in desired depth level */
   int     iStnIndex;              /* Index of this P in nearby station table */
   long    lTime;                  /* Preset 1/1/70 time in seconds */   
   static PHASE Phase;             /* Phase data */
   
/* Is it an Ms? */
   if ( pPStruct->dPTime < 0.1 )   
      for ( i=0; i<Gparm->NumPBuffs; i++ )
         if ( Hypo[i].iQuakeID == pPStruct->lPickIndex )  /* Yes */
         {		 
/* Is this station already in buffer? */		 
            for ( j=0; j<iPBufCnt[i]; j++ )
               if ( !strcmp( pPBuf[i][j].szStation, pPStruct->szStation ) &&
                    !strcmp( pPBuf[i][j].szChannel, pPStruct->szChannel ) )
               {
/* If there is a match, copy the new Ms info into the old pick structure */		 
                  pPBuf[i][j].dMSAmpGM = pPStruct->dMSAmpGM;
                  pPBuf[i][j].dMSPer   = pPStruct->dMSPer;
                  pPBuf[i][j].dMSTime  = pPStruct->dMSTime;
				  
/* Just re-compute the magnitudes and dump to TWC hypo ring */			
                  if ( LocateQuake( pPBuf[i], &iPBufCnt[i], Gparm, &Hypo[i], i,
                       Ewh, city, 0, Hypo, iPBufCnt, cityEC, pEqDepth,
                       iNumMax ) < 0 )
                     logit( "et", "Problem in LocateQuake\n" );
                  if ( Gparm->Debug )
                     logit( "", "Ms Updated %s - %ld\n",
                      pPBuf[i][j].szStation, i );
                  return;
               }
/* It must not be in buffer, so add it */
            if ( iPBufCnt[i] < iNumMax )
            {
               CopyPBuf( pPStruct, &pPBuf[i][iPBufCnt[i]] );
               iPBufCnt[i]++;
               if ( Gparm->Debug )
                  logit( "", "Pick Added for MS %s - %ld\n",
                   pPBuf[i][iPBufCnt[i]-1].szStation, i );
               return;
            }
            else
            {
               logit( "", "Add Ms - buffer %ld filled with picks\n", i );
               return;
            }
         }
		 
   if ( pPStruct->dPTime < 0.1 )
   {
      logit( "t", "P-time=0., no match for hypo=%ld\n", pPStruct->lPickIndex );
      return;
   }
   
/* Ignore this pick if it is older than PPICK_TIMEOUT seconds */   
   time( &lTime );
   if ( lTime-(long) pPStruct->dPTime > PPICK_TIMEOUT )
   {
      logit( "t", "Old P-time - %s\n", pPStruct->szStation );
      return;
   }
   
/* If this pick came from hypo_display, it should have a QuakeID */
   for ( i=0; i<Gparm->NumPBuffs; i++ )
      if ( Hypo[i].iQuakeID == pPStruct->iHypoID )
      {             /* Then, force it into this buffer */
         for ( j=0; j<iPBufCnt[i]; j++ )
            if ( !strcmp( pPBuf[i][j].szStation, pPStruct->szStation ) &&
                 !strcmp( pPBuf[i][j].szChannel, pPStruct->szChannel ) )
            {
/* If the Station was already in this buffer, update that pick */			
               CopyPBuf( pPStruct, &pPBuf[i][j] );
               if ( Gparm->Debug )
                  logit( "", "Pick Updated (in hypo_display) %s %lf - %ld\n",
                   pPBuf[i][j].szStation, pPBuf[i][j].dPTime, i );
               return;
            }
/* Otherwise, add a new pick to the buffer */			
         if ( iPBufCnt[i] < iNumMax )
         {
            CopyPBuf( pPStruct, &pPBuf[i][iPBufCnt[i]] );
            iPBufCnt[i]++;
            if ( Gparm->Debug )
               logit( "", "Pick Added (from hypo_display) %s %lf - %ld\n",
                pPBuf[i][iPBufCnt[i]-1].szStation,
                pPBuf[i][iPBufCnt[i]-1].dPTime, i );
            return;
         }
         else
         {
            logit( "", "In pick add from hypo - buffer %ld filled with picks\n",
                   i );	   
            return;
         }
      }
   
/* Determine if this pick is a repeat of a previous pick */
   for ( i=0; i<Gparm->NumPBuffs; i++ )
      for ( j=0; j<iPBufCnt[i]; j++ )
         if ( (pPBuf[i][j].lPickIndex == pPStruct->lPickIndex &&
               fabs( pPStruct->dPTime-pPBuf[i][j].dPTime ) <
               Gparm->MaxTimeBetweenPicks*60.) ||
              (!strcmp( pPBuf[i][j].szStation, pPStruct->szStation ) &&
               !strcmp( pPBuf[i][j].szChannel, pPStruct->szChannel ) &&
               fabs( pPStruct->dPTime-pPBuf[i][j].dPTime ) <
              (double) BUFFER_TIMEOUT) )
         {
/* If it is, copy the new information into the old pick structure; UNLESS
   the Pick was manually adjusted by hypo_display or develo (i.e., iUseMe=2)
   and this adjustment is from pick_wcatwc. */		 
            if ( pPStruct->iUseMe == 1 && pPBuf[i][j].iUseMe == 2 )
            {
               if ( Gparm->Debug )
                  logit( "", "Pick Update on %s rejected.\n",
                         pPBuf[i][j].szStation);
               return;
            }
            CopyPBuf( pPStruct, &pPBuf[i][j] );
            if ( Gparm->Debug )
               logit( "", "Pick Updated %s %lf - %ld\n",
                pPBuf[i][j].szStation, pPBuf[i][j].dPTime, i );
            return;
         }

   for ( i=0; i<Gparm->NumPBuffs; i++ ) iBuff[i] = -1;   
/* If we get here, no match was found for pick so find a buffer for it */
/* First, check all buffers to see if this fits in an existing solution */		 		 
   for ( i=0; i<Gparm->NumPBuffs; i++ )
   {
      iTemp = *piActive+i;
      if ( iTemp >= Gparm->NumPBuffs ) iTemp -= Gparm->NumPBuffs;
	  
/* Verify that this was not in the permanently removed list */	  
      for ( j=0; j<iNumRem[iTemp]; j++ )
         if ( !strcmp( szStnRem[iTemp][j], pPStruct->szStation ) ) 
         {
            logit( "", "%s was perm. removed from %ld\n", pPStruct->szStation,
                   iTemp );
            return;
         }
	                              
/* Do we have a good sol'n for this buffer */
      if ( Hypo[iTemp].iNumPs >= Gparm->MinPs && Hypo[iTemp].iGoodSoln >= 2 )
      {	  
/* Get depth level to use in travel time table */
         iDLev = (int) ( (Hypo[iTemp].dDepth+0.01) /
                 (double) IASP_DEPTH_INC) * IASP_NUM_PER_DEP;
         iBuff[iTemp] = -2;
/* Check the P-time with what would be expected for this quake; if it
   fits keep it.  Otherwise, go to next buffer */	  
         azidelt = GetDistanceAz( (LATLON *) &Hypo[iTemp], (LATLON *) pPStruct);
         dTTDif = pPStruct->dPTime - Hypo[iTemp].dOriginTime;
         dCorr = azidelt.dDelta*(1./IASP_DIST_INC) - 
          floor( azidelt.dDelta*(1./IASP_DIST_INC) );
         dTTDifQ = fPP[(int) (azidelt.dDelta*(1./IASP_DIST_INC))+iDLev] +
          dCorr*(fPP[(int) (azidelt.dDelta*(1./IASP_DIST_INC))+1+iDLev] -
          fPP[(int) (azidelt.dDelta*(1./IASP_DIST_INC))+iDLev]);
         if ( pPStruct->dFreq < FREQ_MIN || azidelt.dDelta < DELTA_TELE ||
              Hypo[iTemp].dDepth > 100. )
            if ( fabs( dTTDifQ - dTTDif ) <= 10. )
            {
               if ( iPBufCnt[iTemp] < iNumMax )
               {
                  CopyPBuf( pPStruct, &pPBuf[iTemp][iPBufCnt[iTemp]] );
                  iPBufCnt[iTemp]++;
                  if ( Gparm->Debug )
                     logit( "", "Pick Added (existing) %s %lf - %ld\n",
                      pPBuf[iTemp][iPBufCnt[iTemp]-1].szStation,
                      pPBuf[iTemp][iPBufCnt[iTemp]-1].dPTime, iTemp );
                  return;
               }
               else
               {
                  logit( "", "In pick add - buffer %ld filled with picks\n",
                         iTemp );	   
                  return;
               }
            }
      }
   }
   
/* Check to see if this could be a phase from a strong quake in another
   buffer.  Phases are normally low frequency arrivals */   
   if ( pPStruct->dFreq < FREQ_MIN )
      for ( i=0; i<Gparm->NumPBuffs; i++ )
      {
         iTemp = *piActive+i;
         if ( iTemp >= Gparm->NumPBuffs ) iTemp -= Gparm->NumPBuffs;
	  
/* Do we have a good sol'n for this buffer and is it a big quake which might
   produce phases */
         if ( Hypo[iTemp].iNumPs >= Gparm->MinPs+10 && Hypo[iTemp].iGoodSoln >= 2 &&
              Hypo[iTemp].dPreferredMag >= 6.0 )
         {
            GetPhaseTime( pPStruct, &Hypo[iTemp], &Phase, 3 );      
/* Check the P-time with phases that would be expected for this quake */
            dTTDif = pPStruct->dPTime;
            for ( j=0; j<Phase.iNumPhases; j++ )
            {                                      
               dTTDifQ = Phase.dPhaseTime[j];
               if ( fabs( dTTDifQ - dTTDif ) <= 20. )   
               {
                  if ( Gparm->Debug )
                     logit( "t", "Probable %s Phases for buffer %ld - %s %lf - removed from locator\n",
                      Phase.szPhase[j], iTemp, pPStruct->szStation,
                      pPStruct->dPTime );
                  return;
               }
            }                      
         }
      }
   
/* If we don't have a sol'n which matches this pick, try buffers to see which
   it could fit in. But first, order the buffers by Number of Ps. */
   for ( i=0; i<Gparm->NumPBuffs; i++ )
      if ( iBuff[i] != -2 )      /* Skip those where there is already a soln */
      {
         iNum = 0;
         for ( j=0; j<Gparm->NumPBuffs; j++ )
            if ( iBuff[j] != -2 )
            {
               iAlreadyUsed = 0;
               for ( k=0; k<i; k++ )
                  if ( iBuff[k] == j ) iAlreadyUsed = 1;
               if ( iAlreadyUsed == 0 )
                  if ( iPBufCnt[j] > iNum )
                  {
                     iBuff[i] = j;
                     iNum = iPBufCnt[j];
                  }
            }
      }
   
/* Get depth level to use in travel time table */
   iDLev = (int) ((double) (DEPTHKM+0.01) / IASP_DEPTH_INC)*IASP_NUM_PER_DEP;	   
   
   for ( i=0; i<Gparm->NumPBuffs; i++ )
   {
      iTemp = iBuff[i];
      if ( iTemp >= 0 )      /* -2->good soln for buffer, -1->Buffer empty */
      {	  
/* Here, check buffers for a nearby station and P-time differences */	  
         iMatch = 0;
         iNearbyPMatch = 0;
         iPCnt = 0;
/* Find the index of this new Ppick in nearby station lookup table */
         for ( iStnIndex=0; iStnIndex<iNumPStn; iStnIndex++ )
            if ( !strcmp( pPStruct->szStation, pszPStnArray[iStnIndex][0] ) )
               break;
         if ( iStnIndex == iNumPStn ) iNearbyPMatch = 1;
/* See if any of the nearby stations are already in this buffer */	 
         for ( j=0; j<iPBufCnt[iTemp]; j++ )
            for ( k=1; k<iNumNearStn; k++ )
               if ( !strcmp( pPBuf[iTemp][j].szStation, pszPStnArray[iStnIndex][k] ) )
                  iNearbyPMatch = 1;
/* Check the P-times in the buffer to see if they are within theoretical
   limits.  But only check if there is a nearby station in this buffer.  Also
   check to see if this station is already in the buffer. */		  
         if ( iNearbyPMatch == 1 )
            for ( j=0; j<iPBufCnt[iTemp]; j++ )
            {
               azidelt = GetDistanceAz( (LATLON *) pPStruct,
                                        (LATLON *) &pPBuf[iTemp][j] );
               dTTDif = fabs( pPStruct->dPTime - pPBuf[iTemp][j].dPTime );
/* Assume surface source */
               dTTDifMax = fPP[(int) (azidelt.dDelta*(1./IASP_DIST_INC))+1+0];
               if ( dTTDif > (dTTDifMax+10.) && pPBuf[iTemp][j].dPTime > 0.1 )
	          iPCnt++;
               if ( !strcmp( pPStruct->szStation, pPBuf[iTemp][j].szStation ) )
                  iMatch = 1;
            }                    		                                
/* Are we within the constraints to add the pick to this buffer */
         if ( iMatch == 0 && iPCnt == 0 && iNearbyPMatch == 1 )
         {
            if ( iPBufCnt[iTemp] < iNumMax )
            {
               CopyPBuf( pPStruct, &pPBuf[iTemp][iPBufCnt[iTemp]] );
               iPBufCnt[iTemp]++;
               if ( Gparm->Debug )
                  logit( "", "Pick Added %s %lf - %ld\n",
                   pPBuf[iTemp][iPBufCnt[iTemp]-1].szStation,
                   pPBuf[iTemp][iPBufCnt[iTemp]-1].dPTime, iTemp );
               return;
            }
            else
            {
               logit( "", "In add existing - buffer %ld filled with picks\n",
                      iTemp );	   
               return;
            }
         }
      }
   }

/* If we get here, the pick doesn't fit in any existing buffers.  First, 
   try to find the first buffer available with no picks and put it there. */
   for ( i=0; i<Gparm->NumPBuffs; i++ )
   {
      iTemp = *piActive+i;
      if ( iTemp >= Gparm->NumPBuffs ) iTemp -= Gparm->NumPBuffs;
         if ( iPBufCnt[iTemp] == 0 )
         {
            CopyPBuf( pPStruct, &pPBuf[iTemp][iPBufCnt[iTemp]] );
            iPBufCnt[iTemp]++;
            if ( Gparm->Debug )
               logit( "", "Pick Added (new buf) %s %lf - %ld\n",
                pPBuf[iTemp][iPBufCnt[iTemp]-1].szStation,
                pPBuf[iTemp][iPBufCnt[iTemp]-1].dPTime, iTemp );
            return;
         }
   }

/* If we get here, all buffers have some data, but this pick doesn't
   fit in any.  Find the buffer with the oldest data, zero it out,
   and put P there. (We really shouldn't get here, but sometimes we do.) */
   iIndex = 0;
   dMin = 1.E20;
   for ( i=0; i<Gparm->NumPBuffs; i++ )
/* Save this if MS could still be updating */
      if ( Hypo[i].dMSAvg == 0. ||
         ((double) lTime-Hypo[i].dOriginTime) > 7200. )
         for ( j=0; j<iPBufCnt[i]; j++ )
            if ( pPBuf[i][j].dPTime < dMin && pPBuf[i][j].dPTime > 0.0 )
            {
               dMin = pPBuf[i][j].dPTime;
               iIndex = i;
            }
   iPBufCnt[iIndex] = 0;
   iNumRem[iIndex] = 0;
   iLastCnt[iIndex] = 0;
   for ( i=0; i<iNumMax; i++ ) InitP( &pPBuf[iIndex][i] );
   InitHypo( &Hypo[iIndex] );
   Hypo[iIndex].iQuakeID += Gparm->NumPBuffs;
   if ( Hypo[iIndex].iQuakeID >= 10000 ) Hypo[iIndex].iQuakeID -= 10000;
   Hypo[iIndex].iVersion = 1;
   Hypo[iIndex].iAlarmIssued = 0;
   CopyPBuf( pPStruct, &pPBuf[iIndex][iPBufCnt[iIndex]] );
   iPBufCnt[iIndex]++;
   if ( Gparm->Debug )
      logit( "", "Pick Added (latest) %s %lf - %ld\n",
       pPBuf[iIndex][iPBufCnt[iIndex]-1].szStation,
       pPBuf[iIndex][iPBufCnt[iIndex]-1].dPTime, iIndex );
}				  

      /******************************************************************
       * MakeH71Msg()                          *
       * *
       * This function takes hypocenter parameters and puts them into a *
       * H71Sum2K message format.                                       *
       * *
       * Arguments:                                                    *
       * pHypo            Computed hypocentral parameters             *
       * pszMsg           H71SUM2K message in proper format           *
       * *
       ******************************************************************/
       
void MakeH71Msg( HYPO *pHypo, char *pszMsg )
{
   double       dRes;                  /* Average residual */
   double       dTemp;                 /* Lat/Lon minutes */
   LATLON       ll;                    /* Lat/lon (geographic) array */
   long         lTime;                 /* 1/1/70 time */
   char         szTemp[20];
   struct tm    *tm;                   /* Origin time in tm structure */

/* Clear message */
   strcpy( pszMsg, "\0" );

/* Convert o-time from 1/1/70 time to tm structure and fill in time fields */
   lTime = (long) (pHypo->dOriginTime);
   tm = TWCgmtime( lTime );
   itoaX( (int) tm->tm_year+1900, szTemp ); /* 0-3 year */
   PadZeroes( 4, szTemp );
   strcpy( pszMsg, szTemp );
   itoaX( (int) tm->tm_mon+1, szTemp );     /* 4-5 month */
   PadZeroes( 2, szTemp );
   strcat( pszMsg, szTemp );
   itoaX( (int) tm->tm_mday, szTemp );      /* 6-7 day */
   PadZeroes( 2, szTemp );             
   strcat( pszMsg, szTemp );
   strcat( pszMsg, " " );                   /* 8 blank */
   itoaX( (int) tm->tm_hour, szTemp );      /* 9-10 hour */
   PadZeroes( 2, szTemp );
   strcat( pszMsg, szTemp );
   itoaX( (int) tm->tm_min, szTemp );       /* 11-12 Minute */
   PadZeroes( 2, szTemp );
   strcat( pszMsg, szTemp );
   itoaX( (int) tm->tm_sec, szTemp );       /* 13-15 Second */
   PadZeroes( 3, szTemp );
   strcat( pszMsg, szTemp );
   strcat( pszMsg, "." );                   /* 16 . */
   itoaX( (int) ((pHypo->dOriginTime-floor( pHypo->dOriginTime ))*100.), szTemp );
   PadZeroes( 2, szTemp );                  /* 17-18 Hundredths Sec */
   strcat( pszMsg, szTemp );
   strcat( pszMsg, " " );                   /* 19 blank */
   
/* Convert lat/long from geocentric to geographic, then fill msg with loc. */
   GeoGraphic( &ll, (LATLON *) pHypo );
   itoaX( abs ((int) ll.dLat), szTemp );    /* 20-21 Latitude degrees */
   PadZeroes( 2, szTemp );
   strcat( pszMsg, szTemp );
   if ( ll.dLat > 0 ) strcpy( szTemp, " " );
   else               strcpy( szTemp, "S" );
   strcat( pszMsg, szTemp );               /* 22 S or blank for north */
   dTemp = 60. * fabs( ll.dLat - (int) ll.dLat );
   itoaX( (int) dTemp, szTemp );           /* 23-24 Latitude minutes */
   PadZeroes( 2, szTemp );
   strcat( pszMsg, szTemp );
   strcat( pszMsg, "." );                  /* 25 . */
   itoaX( (int) ((dTemp-(int)dTemp)*100.), szTemp );
   PadZeroes( 2, szTemp );                 /* 26-27 decimal minutes */
   strcat( pszMsg, szTemp );
   strcat( pszMsg, " " );                  /* 28 blank */
   while ( ll.dLon > 180. ) ll.dLon -= 360.;
   itoaX( abs( (int) ll.dLon ), szTemp );
   PadZeroes( 3, szTemp );                 /* 29-31 Longitude degrees */
   strcat( pszMsg, szTemp );
   if ( ll.dLon > 0 ) strcpy( szTemp, "E" );
   else               strcpy( szTemp, " " );
   strcat( pszMsg, szTemp );               /* 32 E or blank for W */
   dTemp = 60. * fabs( ll.dLon - (int) ll.dLon );
   itoaX( (int) dTemp, szTemp );           /* 33-34 Longitude minutes */
   PadZeroes( 2, szTemp );
   strcat( pszMsg, szTemp );
   strcat( pszMsg, "." );                  /* 35 . */
   itoaX( (int) ((dTemp-(int)dTemp)*100.), szTemp );
   PadZeroes( 2, szTemp );                 /* 36-37 decimal minutes */
   strcat( pszMsg, szTemp );
   strcat( pszMsg, " " );                  /* 38 blank */
   
/* Add depth to message (km) */   
   itoaX( (int) (pHypo->dDepth + 0.5), szTemp );
   PadZeroes( 3, szTemp );                 /* 39-41 Integer depth */
   strcat( pszMsg, szTemp );
   strcat( pszMsg, ".00" );                /* 42-44 Fill in expected format */
   strcat( pszMsg, " " );                  /* 45 blank */
   
/* Fill in magnitude type and magnitude */
   strncat( pszMsg, pHypo->szPMagType, 1 );
   strcat( pszMsg, "\0" );                 /* 46 magnitude type (b, w, etc.) */
   strcat( pszMsg, " " );                  /* 47 blank */
   itoaX( (int) pHypo->dPreferredMag, szTemp );
   strcat( pszMsg, szTemp );               /* 48 Integer mag. */
   strcat( pszMsg, "." );                  /* 49 . */
   itoaX( (int) ((pHypo->dPreferredMag-(int)pHypo->dPreferredMag)*100.),
                  szTemp );                /* 50-51 decimal mag. */
   PadZeroes( 2, szTemp );
   strcat( pszMsg, szTemp );
   
/* Number of P's used in location */
   itoaX( pHypo->iNumPs, szTemp );         /* 52-54 Number Ps used in loc */
   PadZeroes( 3, szTemp );
   strcat( pszMsg, szTemp );
   strcat( pszMsg, " " );                  /* 55 blank */
   
/* Azimuthal coverage of stations about the epicenter (converted to gap) */
   itoaX( (360-pHypo->iAzm), szTemp );
   PadZeroes( 3, szTemp );                 /* 56-58 Azimuthal gap */
   strcat( pszMsg, szTemp );
   
/* Nearest station's distance in degrees */
   itoaX( (int) pHypo->dNearestDist, szTemp );
   PadZeroes( 3, szTemp );                 /* 59-61 integer nearest distance */
   strcat( pszMsg, szTemp );
   strcat( pszMsg, "." );                  /* 62 . */
   itoaX( (int) ((pHypo->dNearestDist-(int)pHypo->dNearestDist)*10.),
                  szTemp );                /* 63 decimal distance */
   PadZeroes( 1, szTemp );
   strcat( pszMsg, szTemp );
   
/* Add Residual */
   dRes = pHypo->dAvgRes;
   if ( dRes > 99.99 ) dRes = 99.99;       /* Fit it in the space given */
   itoaX( (int) dRes, szTemp );            /* 64-65 integer average residual */
   PadZeroes( 2, szTemp );
   strcat( pszMsg, szTemp );
   strcat( pszMsg, "." );                  /* 66 . */
   itoaX( (int) ((dRes-(int)dRes)*100.), szTemp );
   PadZeroes( 2, szTemp );                 /* 67-68 decimal residual */
   strcat( pszMsg, szTemp );
   
/* Errors not computed by loc_wcatwc */
   strcat( pszMsg, "            " );       /* 69-80 blank */
   
/* Flags indicating regional center, quake id, and version */   
   strcat( pszMsg, "P" );                  /* 81 Data Source code (P=Palmer) */
   strcat( pszMsg, "  " );                 /* 82-83 blank */
   itoaX( pHypo->iQuakeID, szTemp);        /* 84-92 Quake ID */
   PadZeroes( 9, szTemp );
   strcat( pszMsg, szTemp );
   itoaX( pHypo->iVersion, szTemp);        /* 93-94 version */
   PadZeroes( 2, szTemp );
   strcat( pszMsg, szTemp );
   pszMsg[95] = '\n';
   pszMsg[96] = '\0';
}

      /******************************************************************
       * MakeTWCMsg()                          *
       * *
       * This function takes hypocenter parameters and puts them into a *
       * TYPE_HYPOTWC message format.                                   *
       * *
       * Arguments:                                                    *
       * pHypo            Computed hypocentral parameters             *
       * pszMsg           HYPOTWC message in proper format            *
       * *
       ******************************************************************/
       
void MakeTWCMsg( HYPO *pHypo, char *pszMsg )
{
/* Clear message */
   strcpy( pszMsg, "\0" );

/* Fill message */
/* --- INICIO PARCHE DE 64 BITS EN SPRINTF --- */
   sprintf( pszMsg, "%d %d %lf %lf %lf %lf %d %d %lf %d %lf %s %d %lf "
                    "%d %lf %d %lf %d %lf %d %lf %d %d  \0",
    pHypo->iQuakeID, pHypo->iVersion, 
    pHypo->dOriginTime, pHypo->dLat, pHypo->dLon,
    pHypo->dDepth, pHypo->iNumPs, pHypo->iAzm,
    pHypo->dAvgRes, pHypo->iGoodSoln, 
	pHypo->dPreferredMag, pHypo->szPMagType, pHypo->iNumPMags,
    pHypo->dMSAvg, pHypo->iNumMS, pHypo->dMwpAvg, pHypo->iNumMwp, 
    pHypo->dMbAvg, pHypo->iNumMb, pHypo->dMlAvg, pHypo->iNumMl, pHypo->dMwAvg,
	pHypo->iNumMw, pHypo->iMagOnly );	
/* --- FIN PARCHE DE 64 BITS --- */
}

     /**************************************************************
      * RemoveP()                          *
      * *
      * Remove a PPICK structure from a P buffer.                  *
      * *
      * Arguments:                                                 *
      * pPBuf       P-pick buffer to remove PPICK from            *
      * iBufCnt     Number of P-picks in the buffer               *
      * iIndex      Index of P structure to remove from pPBuf     *
      * *
      **************************************************************/
	  
void RemoveP( PPICK *pPBuf, int *iPBufCnt, int iIndex )
{
   int     i;
   
   for ( i=0; i<*iPBufCnt; i++ )
      if ( i > iIndex )
         CopyPBuf( &pPBuf[i], &pPBuf[i-1] );
   InitP( &pPBuf[*iPBufCnt-1] );
   *iPBufCnt -= 1;
}
