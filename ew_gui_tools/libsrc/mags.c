    /******************************************************************
     *                              mags.c                            *
     *                                                                *
     * Contains magnitude determination functions for use in          *
     * many routines.                                                 *
     *                                                                *
     *   By:   Whitmore - Jan., 2001                                  *
     ******************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <earthworm.h>
#include "earlybirdlib.h"

/* Global Variables */
/* WC&ATWC long period seismometer response */
double dLPResp[] = {.18,.31,.42,.52,.64,.75,.86,.91,1.1,1.13,1.15,1.17,
        1.18,1.19,1.2,1.16,1.12,1.08,1.04,1.0,.95,.90,.85,.80,.75,
        .71,.67,.63,.59,.55 };
/* Response of the basic long period filter (14s-28s) used in lpproc */
double dLPFResp[] = {.001,.001,.001,.001,.001,.013,.023,.033,0.082,0.105,
        0.197,0.303,0.513,0.724,0.855,1.000,1.013,1.026,1.026,1.026,1.020,
        1.013,1.020,1.026,0.974,0.921,0.816,0.724,0.605,0.500};
/* Honolulu LP (???) response */	
double dLPHResp[] = {.013,.025,.039,.05,.062,.075,.088,.1,.117,.137,         
        .187,.26,.32,.48,.613,.68,.77,.87,.98,1.,.98,.94,.89,.83,
        .735,.7,.65,.58,.5,.444 };	// Honolulu Long period seis. response
/* WC&ATWC short period high-gain response */	
double dSPResp[] = {0.4,2.66,3.58,3.02,2.61,2.05,1.75,1.46,1.2,1.0,
        .8,.64,.53,.43,.36,.31,.27,.21,.19,.16,.13,.11,.1,.09,
        .08,.072,.063,.056,.048,.046,.043,.039,.036,.032,.030,
        .027,.025,.0225,.021,.020 };
/* Response of the basic short period filter (5Hz-2.0s) used in pick_wcatwc */
double dSPFResp[] = {0.0833,0.7420,0.9924,1.0076,1.0076,1.0076,1.0076,1.0076,
        1.0076,1.0076,1.0038,1.0000,1.0000,1.0000,0.9621,0.9242,0.8864,0.8409,
        0.7803,0.7121,0.6515,0.5833,0.5227,0.4621,0.4167,0.3712,0.3333,0.2954,
        0.2727,0.2424,0.2200,0.1970,0.1818,0.1667,0.1515,0.1363,0.1250,0.1136,
        0.1023,0.0909};
/* Response of another short period filter (5Hz-1.5s) used in pick_wcatwc */
double dSPF2Resp[] = {0.076,0.705,0.977,1.0,1.0,1.0,1.0,0.985,0.977,0.970,
        .909,.833,.735,.629,.523,.492,.455,.439,.409,.394,.349,.311,.280,
        .242,.220,.197,.159,.136,.121,.106,.087,.072,.059,.055,.051,
        .048,.045,.0425,.0417,.0378 };
/* WC&ATWC Short period, low gain response */
double dSPLResp[] = {.08,.18,.40,.70,1.01,1.26,1.33,1.3,1.16,1.0,.88,
        .76,.64,.52,.39,.35,.31,.26,.21,.16,.14,.125,.112,.10,.091,
        .082,.073,.064,.055,.047};
/* Distance in degrees, array, s-p times up to 160s */
double dSPDist[] = {.1,.2,.25,.3,.4,.46,.48,.5,.6,.7,.8,.85,.9,1.0,1.1,
        1.2,1.3,1.4,1.5,1.6,1.65,1.7,1.8,1.9,2.0,2.1,2.2,2.3,2.4,2.5,
        2.55,2.6,2.7,2.8,2.9,3.,3.1,3.2,3.3,3.4,3.45,3.5,3.6,3.7,3.8,
        3.9,4.,4.1,4.2,4.3,4.4,4.45,4.5,4.6,4.7,4.8,4.9,5.0,5.1,5.2,
        5.3,5.35,5.4,5.5,5.6,5.7,5.8,5.9,6.0,6.1,6.2,6.3,6.4,6.5,6.6,
        6.7,6.8,6.9,7.0,7.1,7.2,7.3,7.4,7.5,7.6,7.7,7.8,7.9,8.0,8.1,
        8.2,8.25,8.3,8.4,8.5,8.6,8.7,8.75,8.8,8.9,9.0,9.1,9.2,9.25,
        9.3,9.4,9.5,9.6,9.7,9.75,9.8,9.85,9.9,10.0,10.1,10.2,10.25,10.3,10.4,
        10.5,10.6,10.7,10.8,10.9,11.0,11.1,11.2,11.25,11.3,11.4,11.5,11.6,11.7,
        11.8,11.9,12.0,12.1,12.2,12.3,12.4,12.5,12.6,12.7,12.8,12.9,13.0,13.1,
        13.2,13.3,13.4,13.5,13.55,13.6,13.7,13.8,13.9,14.0,14.1,14.2,14.3};
/* Richter b-values for Mb and MB */
int iBVal[2500];     

 /**************************************************************************
  *                             AutoMwp()                                  *
  *                                                                        *
  * This function computes the basic information necessary to compute an   *
  * Mwp (Tsuboi, et al, 1995 BSSA) from a single seismic signal.  The data *
  * is raw broadband data with the DC computed elsewhere.  The function    *
  * can be called from several programs.  Variable iS indicates which      *
  * buffers to use in processing.                                          *
  *                                                                        *
  * This technique assumes a flat velocity response over the               *
  * frequency range the P wave.  Generally this means less than 60s,       *
  * though for bigger quakes (M>6.5) it may be longer.  STS1 type          *
  * seismometers are fine for this technique.  Broadband seismometers with *
  * LP cutoff periods less than the STS1 can be used with this technique,  *
  * but the larger the earthquake, the greater the magnitude under-        *
  * estimation.  Care must be used when using these stations.              *
  *                                                                        *
  * Here we do not deconvolve the instrument response.  Results may        *
  * improve for instruments with lower period LP cutoffs if the response   *
  * is taken into account.  However, early testing on this showed problems *
  * integrating the displacement data due to enhanced LP noise after       *
  * deconvolution.                                                         *
  *                                                                        *
  * Any DC offset in the data will render the integrations useless.  So the*
  * offset is removed prior to integrating.  When called from pick_wcatwc, *
  * the offset is already removed.  When called from any other program,    *
  * the offset is removed here.                                            *
  *                                                                        *
  * Sensitivities of the NSN stations or any cooperating stations can be   *
  * retrieved through the autodrm of NEIC.                                 *
  *                                                                        *
  * Email autodrm with the following sequence of commands:                 *
  * BEGIN                                                                  *
  * DATE1 yyyymmddhhmm                                                     *
  * DATE2 yyyymmddhhmm                                                     *
  * WAVEF stn BHZ                                                          *
  * EMAIL wcatwc@noaa.gov                                                *
  * STOP                                                                   *
  *                                                                        *
  * The WAVEF retrieval option will give the magnification in nm/count at  *
  * 1.0Hz reference frequency.  This must be converted to counts/          *
  * m/s for use in this program.  This converted value is listed in        *
  * StaDataFile.  The converted value = (1/X)(1E9)/(2*PI) where X is the   *
  * displacement response at 1Hz.                                          *
  *                                                                        *
  * March, 2004: Added new method to compute S:N ratio for Knight method.  *
  *                                                                        *
  * February, 2004: Added Bill Knight's method to determine window length, *
  *                 de-trend displacement data, and determine peaks in     *
  *                 integrated displacement data.  This method should      *
  *                 eliminate need for window length refinement during     *
  *                 processing.                                            *
  *                                                                        *
  *  Arguments:                                                            *
  *     Sta              Pointer to station being processed                *
  *     pPBuf            Pointer to P-pick buffer                          *
  *     dSN              Mwp Signal-to-noise ratio                         *
  *     iMwpSeconds      # seconds to evaluate signal for Mwp              *
  *     iS               Calling program (0=pick_wcatwc, 1=develo, hypo_d.)*
  *                                                                        *
  **************************************************************************/
  
void AutoMwp( STATION *Sta, PPICK *pPBuf, double dSN, int iMwpSeconds, int iS )
{
   double  dHighSave;          /* Largest amplitude in int. disp. signal */
   double  dLowSave;           /* Lowest amplitude in integrated disp. signal */
   double  dOldestTime;        /* Oldest time (1/1/70 seconds) in buffer */
   double  dTotalDisp;         /* Running total of integrations */
   double  dVelMSData[MAXMWPARRAY];// vel. signal (m/s)
   double  dWinLen;            /* Returned integration window length */
   double  dZDispData[MAXMWPARRAY];  /* Detrended displacement signal */
   long    i, iRC;
   int     iHighSave, iLowSave;
   long    lBIndex;            /* Buffer index to start MDF evaluation */
   long    lBNum;              /* Number of samples to evaluate for MDF */
   long    lNum;               /* Number of samples to evaluate; refined in
                                  wavelet_decomp */
   long    lNumInBuff;         /* # samples ahead of P in buffer */
   long    lPIndex;            /* Buffer index of P-time */
   long    lTemp, lTemp2;      /* Temporary counters */
   static  double  x, xbar, x2_bar, xcount, S_to_N_prior;
   int contador_aux;
   
   
/* Is P-time within buffer? */
   if ( iS == 1 )           /* develo and hypo_display */
   {
      dOldestTime = Sta->dEndTime -
       ((double) Sta->lRawCircSize/Sta->dSampRate) + 1./Sta->dSampRate;
      if ( pPBuf->dPTime < dOldestTime || pPBuf->dPTime > Sta->dEndTime )
         return;
   }                                
   
/* What is buffer index of P-time? */   
   if ( iS == 1 )           /* develo and hypo_display */
   {
      lPIndex = Sta->lSampIndexF - (long) ((Sta->dEndTime-pPBuf->dPTime) * 
                Sta->dSampRate) - 1;
      while ( lPIndex <  0 )                 lPIndex += Sta->lRawCircSize;
      while ( lPIndex >= Sta->lRawCircSize ) lPIndex -= Sta->lRawCircSize; 
   }
   else lPIndex = 0;

 
/////////////////////////////////////////////////
   
	logit( "","Station %s, original Mwp time = %d, distance at the moment of iteration = %lf ""\n", Sta->szStation, iMwpSeconds, pPBuf->dDelta);
	
	
	/*if (pPBuf->dDelta==0.0 || pPBuf->dDelta<2.0)
		return;*/
	
	
	
	if (iMwpSeconds<200 && pPBuf->dDelta>0.0)
	{
		for (contador_aux=0; contador_aux<160; contador_aux++ )
		   if (dSPDist[contador_aux] >= pPBuf->dDelta )break;
	
		if( contador_aux < iMwpSeconds)// && iMwpSeconds>2 && iMwpSeconds<200 && contador_aux<=159)
		{
			iMwpSeconds = contador_aux - 2;
			logit( "","Station %s at a distance of %lf degrees, recalculating for Mwp time = %d ""\n", Sta->szStation, pPBuf->dDelta, iMwpSeconds);
		}
	}
	
   



	
//////////////////////////////////////////////////////////////////////
   
  
   
   
   
   
   
   
   
/* How many points to evaluate? */   
   if ( iS == 1 )           /* develo and hypo_display */
   {
      lNumInBuff = (long) ((Sta->dEndTime-pPBuf->dPTime) * Sta->dSampRate);
      lNum = (long) (Sta->dSampRate * (double) iMwpSeconds);
      if ( lNum > lNumInBuff )      /* There is more data needed to compute Mwp */
      {
         logit( "", "%s Not enough samples in AutoMwp; lNum=%ld, "
                    "lNumInBuff=%ld\n", Sta->szStation, lNum, lNumInBuff );
         return;
      }
   }
   else lNum = Sta->lMwpCtr;
   
/* Initialize return values */
   pPBuf->dMwpIntDisp = 0.;
   pPBuf->dMwpMag = 0.;
   pPBuf->dMwpTime = 0.;

/* As of 3/04, signal-to-noise ratios are computed by comparing rms prior to
   P compared to 20s post P. */   
/* Compute the background noise level.  Several ways have been tested.
   Here we use the max noise difference prior to the start of the signal
   and compare it to the maximum signal.  */   
   if ( iS == 1 )           /* develo and hypo_display */
   {
      lBNum = (long) ((double) MWP_BACKGROUND_TIME * Sta->dSampRate);
      lBIndex = lPIndex - lBNum;
      while ( lBIndex < 0 ) lBIndex += Sta->lRawCircSize; 
      x2_bar = 0.;
      xcount = 0.;
      S_to_N_prior = 1.e+50;
      for ( i=0; i<lBNum; i++ )
      {
         lTemp = i + lBIndex;
         if ( lTemp >= Sta->lRawCircSize ) lTemp -= Sta->lRawCircSize;   
         x = ((double) Sta->plRawCircBuff[lTemp]-Sta->dAveLDCRaw) / Sta->dSens;
         x2_bar += (x * x);
         xcount += 1.;
      }
      if ( xcount > 500. ) 
      {
         x2_bar /= xcount;
         S_to_N_prior = sqrt( x2_bar );
      }
   }
   else                           /* pick_wcatwc */
      S_to_N_prior = Sta->dAveRawNoiseOrig;
   
/* Integrate velocity signal to get displacement. Convert counts to m/s before
   conversion. */
   dTotalDisp = 0.;
   for ( i=0; i<lNum-1; i++ )
   {
      if ( iS == 1 )           /* develo and hypo_display */
      {
         lTemp = i + lPIndex;
         if ( lTemp >= Sta->lRawCircSize ) lTemp -= Sta->lRawCircSize;
         lTemp2 = lTemp + 1;
         if ( lTemp2 >= Sta->lRawCircSize ) lTemp2 -= Sta->lRawCircSize;
         Sta->pdRawDispData[i] = dTotalDisp + 1./Sta->dSampRate*0.5*
          ((double) (Sta->plRawCircBuff[lTemp]-Sta->dAveLDCRaw)/Sta->dSens +
           (double) (Sta->plRawCircBuff[lTemp2]-Sta->dAveLDCRaw)/Sta->dSens);
         dVelMSData[i] = (double) (Sta->plRawCircBuff[lTemp]-Sta->dAveLDCRaw) /
                         Sta->dSens;
      }
      else                     /* pick_wcatwc */
      {
         Sta->pdRawDispData[i] = dTotalDisp + 1./Sta->dSampRate*0.5*
          ((double) Sta->plRawData[i]/Sta->dSens +
           (double) Sta->plRawData[i+1]/Sta->dSens);
         dVelMSData[i] = (double) Sta->plRawData[i] / Sta->dSens;
      }
      dTotalDisp = Sta->pdRawDispData[i];
   }
   
/* Remove any drift from displacement signal with linear removal (and
   check for proper Signal-to-Noise ratio here). */   
   iRC = detrend( 200., 1./Sta->dSampRate, Sta->pdRawDispData, lNum,
                  S_to_N_prior, dVelMSData, dZDispData, 1, dSN );
   if ( iRC == -1 )          /* S:N too low */
      return;
			
/* Update raw disp data array with detrended data */
   for (i=0; i<lNum; i++) Sta->pdRawDispData[i] = dZDispData[i];			
   
/* Determine window length based on signal frequency content (if S:N high) */
   if ( iRC == -2 )     /* -2 -> moderate S:N */
     dWinLen = 20.;
   else                 /* High S:N */
     dWinLen = wavelet_decomp( (double) iMwpSeconds, dZDispData, lNum,
                                        1./Sta->dSampRate );		  
   if (dWinLen <= 0.)
   {
      logit( "", "AutoMwp - dWinLen = %lf\n", dWinLen );
      return;
   }
   if ( dWinLen > (double) iMwpSeconds ) dWinLen = (double)iMwpSeconds;
   lNum = (long) ( dWinLen*Sta->dSampRate );
   if ( iS == 0 )                       /* pick_wcatwc */
      if ( lNum > Sta->lMwpCtr ) lNum = Sta->lMwpCtr;
   if ( lNum > MAXMWPARRAY ) lNum = MAXMWPARRAY-1;

/* Integrate displacement to get int. disp. using detrended signal */
   dTotalDisp = 0;
   for (i=0; i<lNum-1; i++)
      {
      Sta->pdRawIDispData[i] = dTotalDisp + 1./Sta->dSampRate*0.5*
       (dZDispData[i]+dZDispData[i+1]);
      dTotalDisp = Sta->pdRawIDispData[i];
      }
   
/* Get amplitude between first and second peaks of integegrated
   displacement trace (or first peak amp, if that is higher than difference).
   Ignore any later bumps for diverging signal.
   The two-peak approach is an addition to the original Mwp technique 
   described by Tsuboi et al. (1998).  It accounts for pP arrivals. */
   iRC = integrate (&dHighSave, &dLowSave, &iHighSave, &iLowSave, lNum,
                     dZDispData, 1./Sta->dSampRate, dWinLen);
   if (iRC < 0)
   {
      logit( "", "AutoMwp - Error in integrate\n" );
      return;
   }
      
   pPBuf->dMwpIntDisp = fabs( dHighSave - dLowSave );
   
/* Call the Integration time the time to the 2nd peak if there
   was one (the first peak otherwise) */
   pPBuf->dMwpTime = max( (double) iHighSave/Sta->dSampRate, 
                          (double) iLowSave/Sta->dSampRate );
						  
/* If no good peaks were found or the integration time is very small,
   this trace will not be used */
   if ( (iHighSave == 0 && iLowSave == 0) || pPBuf->dMwpTime < 2.01 )
   {
      pPBuf->dMwpIntDisp = 0.;
      pPBuf->dMwpMag = 0.;
      pPBuf->dMwpTime = 0.;
   }
}

      /******************************************************************
       *                   ComputeAvgMm()                               *
       *                                                                *
       * This function takes Mm magnitudes previously computed and      *
       * returns a modified average and # used in average.              *
       *                                                                *
       *  Arguments:                                                    *
       *   iNum           Number of stations evaluated                  *
       *   Mm             Array of Mm data                              *
       *   piNumMm        Number of stations used in Mm average         *
       *                                                                *
       *  Return:         Modified Mm average                           *
       *                                                                *
       ******************************************************************/
        
double ComputeAvgMm( int iNum, MMSTUFF Mm[], int *piNumMm )
{                                  
   double  dMmAvg;                /* Total Mm Average */
   double  dMmSumMod;             /* Modified avg sum */
   int     i;                                  
   int     iMmCountMod;           /* # stations included in modified avg. */

/* Initialize averaging variables */
   *piNumMm          = 0;
   dMmAvg            = 0.;
   iMmCountMod       = 0;
   dMmSumMod         = 0.0;

/* Loop over all stations and compute each magnitude where applicable */
   for ( i=0; i<iNum; i++ )
   {              /* Is there Mm data */
      if ( Mm[i].dMmMax > 0.0 )
      {
         *piNumMm += 1;
         dMmAvg += Mm[i].dMmMax;
      }
   }

/* Compute total magnitude averages */
   if ( *piNumMm > 0 )  dMmAvg /= (double) *piNumMm;

/* Here, the modified averages are computed.  The modified average is the
   average of all magnitudes of one type throwing out those that are far away
   from the average.  In specific, any magnitude more than 0.6 units away
   from the average is thrown out */
   for ( i=0; i<iNum; i++ )
      if ( Mm[i].dMmMax > 0.0 )
         if ( fabs( dMmAvg-Mm[i].dMmMax ) < 0.6 )
         {
            iMmCountMod++;
            dMmSumMod += Mm[i].dMmMax;
         }

/* Return with number used in modifed Mm, and Mm */
   if ( iMmCountMod >= 2 ) 
   {
      *piNumMm = iMmCountMod;
      return( dMmSumMod / (double) iMmCountMod );
   }
   else return( dMmAvg );
}
   
      /******************************************************************
       *                   ComputeAvgMS()                               *
       *                                                                *
       * This function takes MS magnitudes previously computed and      *
       * returns a modified average and # used in average.              *
       *                                                                *
       *  Arguments:                                                    *
       *   iPNum          Number of P-data                              *
       *   P              Array of P-data structures                    *
       *   piNumMS        Number of stations used in MS average         *
       *                                                                *
	   *  Return:         Modified MS average                           *
       *                                                                *
       ******************************************************************/
        
double ComputeAvgMS( int iPNum, PPICK P[], int *piNumMS )
{                                  
   double  dMSAvg;                /* Total MS Average */
   double  dMSSumMod;             /* Modified avg sum */
   int     i;                                  
   int     iMSCountMod;           /* # stations included in modified avg. */

/* Initialize averaging variables */
   *piNumMS          = 0;
   dMSAvg            = 0.;
   iMSCountMod       = 0;
   dMSSumMod         = 0.0;

/* Loop over all stations and compute each magnitude where applicable */
   for ( i=0; i<iPNum; i++ )
   {              /* Is there MS data */
      if ( P[i].dMSMag > 0.0 )
         if ( P[i].dDelta >= 4.0 )         /* Is it far enough for MS */
         {
            *piNumMS = *piNumMS + 1;
            dMSAvg += P[i].dMSMag;
         }
   }                    /* close station loop */

/* Compute total magnitude averages */
   if ( *piNumMS > 0 )  dMSAvg /= (double) *piNumMS;

/* Here, the modified averages are computed.  The modified average is the
   average of all magnitudes of one type throwing out those that are far away
   from the average.  In specific, any magnitude more than 0.6 units away
   from the average is thrown out */
   for ( i=0; i<iPNum; i++ )
      if ( P[i].dMSMag > 0. )
         if ( P[i].iMSClip == 0 && fabs( dMSAvg-P[i].dMSMag ) < 0.6 )
         {
            iMSCountMod++;
            dMSSumMod += P[i].dMSMag;
         }

/* Return with number used in modifed MS, and MS */
   if ( iMSCountMod >= 2 ) 
   {
      *piNumMS = iMSCountMod;
      return( dMSSumMod / (double) iMSCountMod );
   }
   else return( dMSAvg );
}
   
      /******************************************************************
       *                   ComputeMagnitudes()                          *
       *                                                                *
       * This function takes magnitude information from the PPICK       *
       * structure to calculate magnitudes and compute averages.  Since *
       * the amplitude information used is normally from the digital    *
       * analysis system, the stations are tested to see if they are    *
       * clipped.  A modified average is computed here which throws     *
       * magnitudes out of the average which are far from the norm.     *
       *                                                                *
       * Sept., 2008: Decreased Mwp valid distance from 100 to 90 deg.  *
       * January, 2008: Added check to make sure that Ml is not computed*
       *                until after S has arrived.                      *
       * May, 2007: Combined with ComputeMagnitudesA from UTILS.        *
       * April, 2005: Use only STS1/KS54000/KS36000 for Mwp > 7.5       *
       * March, 2002: Mwp eliminator based on integration time changed  *
       *              so that high times are eliminated first, the avg  *
       *              re-computed, then low times eliminated.           *
       *                                                                *
       *  Arguments:                                                    *
       *   iPNum          Number of P-data                              *
       *   P              Array of P-data structures                    *
       *   pHypo          Hypocenter parameters                         *
       *                                                                *
       ******************************************************************/
        
void ComputeMagnitudes( int iPNum, PPICK P[], HYPO *pHypo )
{
   AZIDELT azidelt;               /* Distance/az epicenter to station */
   static  double  dAveIntTime;   /* Avg Mwp integration time */
   static  double  dAveIntTimeT;  /* Summation for dAveIntTime */
   double  dSD;                   /* Standard deviation of Mwp */
                                  /* Modified avgs for each type of magnitude */
   double  dMwpAvgRaw;            /* Un-modified Mwp Average */
   double  dMbSumMod, dMlSumMod, dMSSumMod, dMwpSumMod, dMwSumMod;
   int     i, j;
                                  /* # stations included in modified avg. */
   int     iMbCountMod, iMlCountMod, iMSCountMod, iMwpCountMod, iMwCountMod;
   int     iMwpCounted[MAX_STATIONS]; /* 1 if use in avg., 0 if int. time bad */
   int     iNumMwpRaw;            /* # of stations in un-modified Mwp Average */
   int     iRegion;               /* Region (see GetRegion) of epicenter */
   LATLON  ll;                    /* Epicentral location in geographic coords.*/
   long    lTime;                 /* Preset 1/1/70 time in seconds */   

/* Get quake region for possible Ml correction */
   GeoGraphic( &ll, (LATLON *) pHypo );
   if ( ll.dLon < 0 ) ll.dLon += 360.;
   iRegion = GetRegion( ll.dLat, ll.dLon );
      
/* Initialize averaging variables */
   pHypo->dMbAvg     = 0.;              /* In case InitHypo not called */
   pHypo->iNumMb     = 0;
   pHypo->iNumMbClip = 0;
   pHypo->dMlAvg     = 0.;
   pHypo->iNumMl     = 0;
   pHypo->iNumMlClip = 0;
   pHypo->dMSAvg     = 0.;
   pHypo->iNumMS     = 0;
   pHypo->iNumMSClip = 0;
   pHypo->dMwpAvg    = 0.;
   pHypo->iNumMwp    = 0;
   pHypo->dMwAvg     = 0.;
   pHypo->iNumMw     = 0;
   pHypo->iNumMwClip = 0;
   iNumMwpRaw        = 0;
   iMbCountMod       = 0;
   iMlCountMod       = 0;
   iMSCountMod       = 0;
   iMwpCountMod      = 0;
   iMwCountMod       = 0;
   dMbSumMod         = 0.0;
   dMlSumMod         = 0.0;
   dMSSumMod         = 0.0;
   dMwSumMod         = 0.0;
   dMwpSumMod        = 0.0;
   dMwpAvgRaw        = 0.0;
   dAveIntTime       = 0.0;
   dAveIntTimeT      = 0.0;
   dSD               = 0.0;
   for ( i=0; i<MAX_STATIONS; i++ ) iMwpCounted[i] = 0;
   
/* Get present time for Ml */   
   time( &lTime );

/* Loop over all stations and compute each magnitude where applicable */
   for ( i=0; i<iPNum; i++ )
   {   /* First get epicentral distance and azimuth, and fill structure */
      azidelt = GetDistanceAz( (LATLON *) pHypo, (LATLON *) &P[i] );
      P[i].dDelta = azidelt.dDelta;
      P[i].dAz = azidelt.dAzimuth;
      /* Is the station used in the epicentral solution */
      if ( P[i].iUseMe > 0 )
      {                 /* Is there Mb data */
         if ( P[i].dMbAmpGM > 0.0 )           /* Is there Mb data */
            if ( P[i].dDelta >= 12.0 )        /* Is it far enough for Mb */
            {
               P[i].dMbMag = ComputeMbMag( P[i].szChannel, 0., P[i].dMbAmpGM,
                P[i].dMbPer, P[i].dDelta, pHypo->dDepth );
               if ( P[i].iMbClip == 1 )
                  pHypo->iNumMbClip++;
               else
               {
                  pHypo->iNumMb++;
                  pHypo->dMbAvg += P[i].dMbMag;
               }
            }
                        /* Is there Ml data */
         if ( P[i].dMlAmpGM > 0.0 )
            if ( P[i].dDelta <= 4.0 )         /* Is it close enough for Ml */
            {
               for ( j=0; j<160; j++ )        /* Get S-P time for this dist. */
                  if ( dSPDist[j] >= P[i].dDelta ) break;
/* See if enough time has passed after the P to see the s/Lg
   (20s is added to account for latency and S build-up) */
   
               if ( (lTime-(long)P[i].dPTime > (long) j+20) && (strcmp( P[i].szChannel, "BHZ" )==0 || strcmp( P[i].szChannel, "HHZ" )==0) )
               {
                  P[i].dMlMag = ComputeMlMag( P[i].szChannel, 0., P[i].dMlAmpGM,
                                              P[i].dMlPer, P[i].dDelta );
/* Adjust Ml based on epicenter region (empirical) */
                  if ( iRegion == 0 || iRegion == 1 ) P[i].dMlMag += 0.3;
                  if ( iRegion >= 10 && iRegion <= 13 ) P[i].dMlMag -= 0.3;
                  if ( P[i].iMlClip == 1 )
                     pHypo->iNumMlClip++;
                  else
                  {
                     pHypo->iNumMl++;
                     pHypo->dMlAvg += P[i].dMlMag;
                  }
               }
            }
                        /* Is there MS data */
         if ( P[i].dMSAmpGM > 0.0 )
            if ( P[i].dDelta >= 4.0 )         /* Is it far enough for MS */
            {
               P[i].dMSMag = ComputeMSMag( P[i].szChannel, 0.,
                P[i].dMSAmpGM, P[i].dMSPer, P[i].dDelta );
               if ( P[i].iMSClip == 1 )
                  pHypo->iNumMSClip++;
               else
               {
                  pHypo->iNumMS++;
                  pHypo->dMSAvg += P[i].dMSMag;
               }
            }
                        /* Is there Mwp data */
         if ( P[i].dMwpIntDisp > 0. && P[i].dMwpTime > 0. )
            if ( P[i].dDelta <= 90. && P[i].dDelta >= 3. )         /* Is it close enough for Mwp */
/* Check to see if the Mwp integration time is such that the S wave will
   arrive within the window.  If it does, eliminate it. */
            {
               for ( j=0; j<160; j++ )        /* Get S-P time for this dist. */
                  if ( dSPDist[j] >= P[i].dDelta ) break;
               if ( P[i].dMwpTime <= (double) j || j >= 159 )
               {
                  P[i].dMwpMag = ComputeMwpMag( P[i].dMwpIntDisp, P[i].dDelta );
                        /* Can't really check for Mwp clipping */
                  pHypo->iNumMwp++;
                  pHypo->dMwpAvg += P[i].dMwpMag;
                  dAveIntTimeT += P[i].dMwpTime;
                  iMwpCounted[i] = 1;
               }
               else     /* Log it in error file */
                  logit( "t", "Mwp Time > S-P; MwpTime=%lf, S-P=%lf,"
                         " %s\n", P[i].dMwpTime, (double) j, P[i].szStation );
            }
/*          Mws computed in ANALYZE or earthworm's mm and sent as a number*/		
         if ( P[i].dMwMag > 0.0 )
         {
            if ( P[i].iMwClip == 1 )
               pHypo->iNumMwClip++;
            else
            {
               pHypo->iNumMw++;
               pHypo->dMwAvg += P[i].dMwMag;
            }
         }
      }                 /* close test for station use in epicentral location */
   }                    /* close station loop */ 

/* The Mwp average is computed a little bit different than the rest.  First,
   we check to see if the integration time is reasonable.  If yes, this
   magnitude is included in the averaging scheme.  If no, it is not included.
   Instead of using 0.6 as a limit from the average for computed magnitudes,
   here we use 1 standard deviation. All Mwp's more than 1 standard deviation
   from the average are thrown out.  Then the average is recomputed.  */
   if (pHypo->iNumMwp)
   {
      dMwpAvgRaw = pHypo->dMwpAvg / (double) pHypo->iNumMwp;
      iNumMwpRaw = pHypo->iNumMwp;
   } 
   if ( pHypo->iNumMwp ) dAveIntTime = dAveIntTimeT / (double) pHypo->iNumMwp;
   if ( pHypo->iNumMwp > 4 )
   {
      for ( i=0; i<iPNum; i++ )
         if ( P[i].dMwpMag > 0. )
            if ( P[i].dMwpTime-dAveIntTime > 3.*dAveIntTime/2. ) /* Too high? */
            {        /* Then get rid of this one from average */
               dAveIntTimeT -= P[i].dMwpTime;
               pHypo->dMwpAvg -= P[i].dMwpMag;
               pHypo->iNumMwp -= 1;
               iMwpCounted[i] = 0;
               logit( "", "%s Integration time toss out, %lf, NumMwp=%ld, "
                      "TempAve=%lf, ave=%lf\n", P[i].szStation,
                      P[i].dMwpTime, pHypo->iNumMwp, dAveIntTimeT, dAveIntTime); 
            }
      if ( pHypo->iNumMwp ) dAveIntTime = dAveIntTimeT / (double)pHypo->iNumMwp;
   }
   if ( pHypo->iNumMwp > 4 )
      for ( i=0; i<iPNum; i++ )
         if ( P[i].dMwpMag > 0. && iMwpCounted[i] == 1 )
            if ( dAveIntTime-P[i].dMwpTime > 3.*dAveIntTime/4. )  /* Too low? */
            {        /* Then get rid of this one from average */
               pHypo->dMwpAvg -= P[i].dMwpMag;
               pHypo->iNumMwp -= 1;
               iMwpCounted[i] = 0;
               logit( "", "%s Integration time toss out 2, %lf, NumMwp=%ld, "
                      "ave=%lf\n", P[i].szStation, P[i].dMwpTime,
                      pHypo->iNumMwp, dAveIntTime ); 
            }

/* If Mwp over 8.0, only use STS1/KS54000/KS36000 data, or if over 7.5, STS2/3T
   are ok */
   /*if ( pHypo->iNumMwp > 3 )
   {                                         
      for ( i=0; i<iPNum; i++ )
         if ((P[i].iStationType != 1 && P[i].iStationType != 5 &&
              P[i].iStationType != 6 && P[i].dMwpMag > 0. &&
              iMwpCounted[i] == 1    &&
             (pHypo->dMwpAvg/(double) pHypo->iNumMwp) > 8.0) ||
             (P[i].iStationType != 1 && P[i].iStationType != 5 &&
              P[i].iStationType != 2 && P[i].iStationType != 4 &&
              P[i].iStationType != 13 && P[i].iStationType != 14 &&
              P[i].iStationType != 16 && P[i].iStationType != 17 &&
              P[i].iStationType != 6 && P[i].dMwpMag > 0. &&
              iMwpCounted[i] == 1    && 
             (pHypo->dMwpAvg/(double) pHypo->iNumMwp) > 7.5) )
         {
            pHypo->dMwpAvg -= P[i].dMwpMag;
            pHypo->iNumMwp -= 1;
            iMwpCounted[i] = 0;
            logit( "", "%s Magnitude/StationType tossout\n",
                   P[i].szStation); 
         }
   }    */      
                                                               
/* Compute total magnitude averages */
   if ( pHypo->iNumMb )  pHypo->dMbAvg /= (double) pHypo->iNumMb;
   if ( pHypo->iNumMl )  pHypo->dMlAvg /= (double) pHypo->iNumMl;
   if ( pHypo->iNumMS )  pHypo->dMSAvg /= (double) pHypo->iNumMS;
   if ( pHypo->iNumMwp ) pHypo->dMwpAvg /= (double) pHypo->iNumMwp;
   if ( pHypo->iNumMw )  pHypo->dMwAvg /= (double) pHypo->iNumMw;

/* Get standard deviation for Mwp's */
   if ( pHypo->iNumMwp > 2 )
   {
      for ( i=0; i<iPNum; i++ )
         if ( P[i].dMwpMag > 0. && iMwpCounted[i] == 1 )
            dSD += ((P[i].dMwpMag-pHypo->dMwpAvg) *
                    (P[i].dMwpMag-pHypo->dMwpAvg));
      dSD = sqrt( dSD / (double) pHypo->iNumMwp );
      logit( "", "Mwp SD = %lf, avg = %lf\n", dSD, pHypo->dMwpAvg );
   }

/* Here, the modified averages are computed.  The modified average is the
   average of all magnitudes of one type throwing out those that are far away
   from the average.  In specific, any magnitude more than 0.6 units away
   from the average is thrown out (except for Mwp which is 1 std dev.). */
   for ( i=0; i<iPNum; i++ )
      if ( P[i].iUseMe > 0 ) /* Is the stn used and are there mags for it? */
      {
         if ( P[i].dMbMag > 0. )
            if ( P[i].iMbClip == 0 && fabs( pHypo->dMbAvg-P[i].dMbMag ) < 0.6 )
            {
               iMbCountMod++;
               dMbSumMod += P[i].dMbMag;
            }
         if ( P[i].dMlMag > 0. )
            if ( P[i].iMlClip == 0 && fabs( pHypo->dMlAvg-P[i].dMlMag ) < 0.6 )
            {
               iMlCountMod++;
               dMlSumMod += P[i].dMlMag;
            }
         if ( P[i].dMSMag > 0. )
            if ( P[i].iMSClip == 0 && fabs( pHypo->dMSAvg-P[i].dMSMag ) < 0.6 )
            {
               iMSCountMod++;
               dMSSumMod += P[i].dMSMag;
            }
         if ( P[i].dMwpMag > 0. )
            if ( fabs( pHypo->dMwpAvg-P[i].dMwpMag ) < dSD &&
                 iMwpCounted[i] == 1 )
            {
               iMwpCountMod++;
               dMwpSumMod += P[i].dMwpMag;
            }
//         if ( P[i].dMwMag > 0. )
//            if ( P[i].iMwClip == 0 && fabs (pHypo->dMwAvg-P[i].dMwMag) < 0.6 )
///* If Mwp over 8.0, only use STS1/KS54000/KS36000 data, or if over 7.5, STS2/3T
//   are ok */
//               if ( (P[i].iStationType == 1 && P[i].iStationType == 5 &&
//                     P[i].iStationType == 6 && pHypo->dMwAvg > 8.0) ||
//                    (P[i].iStationType == 1 && P[i].iStationType == 5 &&
//                     P[i].iStationType == 2 && P[i].iStationType == 4 &&
//                     P[i].iStationType == 13 && P[i].iStationType == 14 &&
//                    P[i].iStationType == 16 && P[i].iStationType == 17 &&
//                    P[i].iStationType == 6 && pHypo->dMwAvg > 7.5) ||
//                    pHypo->dMwAvg <= 7.5 )
//              {
//                 iMwCountMod++;
//                 dMwSumMod += P[i].dMwMag;
//              }
      }

/* Update structure with modified average if enough stations in avg. */
   if ( iMbCountMod >= 2 )
   {
      pHypo->dMbAvg = dMbSumMod / (double) iMbCountMod;
      pHypo->iNumMb = iMbCountMod;
   }
   if ( iMlCountMod >= 2 )
   {
      pHypo->dMlAvg = dMlSumMod / (double) iMlCountMod;
      pHypo->iNumMl = iMlCountMod;
   }
   if ( iMSCountMod >= 2 ) 
   {
      pHypo->dMSAvg = dMSSumMod / (double) iMSCountMod;
      pHypo->iNumMS = iMSCountMod;
   }
   if ( iMwpCountMod > 2 )
   {
      pHypo->dMwpAvg = dMwpSumMod / (double) iMwpCountMod;
      pHypo->iNumMwp = iMwpCountMod;
   }
   else
   {
      pHypo->dMwpAvg = dMwpAvgRaw;
      pHypo->iNumMwp = iNumMwpRaw; 
   }      
   if ( iMwCountMod >= 2 )
   {
      pHypo->dMwAvg = dMwSumMod / (double) iMwCountMod;
      pHypo->iNumMw = iMwCountMod;
   }
}

      /******************************************************************
       *                     ComputeMbMag()                             *
       *                                                                *
       * This function computes the short period, body wave magnitude,  *
       * Mb.  In these programs, two body wave magnitudes are computed  *
       * (Mb and MB).  The smaller case b implies short period p-wave   *
       * Richter magnitudes while the large case implies long period.   *
       *                                                                *
       *  Arguments:                                                    *
       *   pszChan        Seismic data channel (e.g. BHZ, LHZ...)       *
       *   dGain          Seismometer amplification (see note at top)   *
       *                  (if gain = 0., MbAmp must be ground motion    *
       *                   in nm (peak-to-trough)                       *
       *   dMbAmp         Signal amplitude (computer units or nm        *
       *                   depending on dGain)                          *
       *   dMbPer         Period of cycle of interest (seconds)         *
       *   dDelta         Epicentral distance in degrees                *
       *   dDepth         Hypocenter depth in km                        *
       *                                                                *
       *  Return:                                                       *
       *   double - Mb Magnitude                                        *
       ******************************************************************/
	   
double ComputeMbMag( char *pszChan, double dGain, double dMbAmp,
                     double dMbPer, double dDelta, double dDepth )
{
   double  dAmp;             /* Ground displacement (nm) */
   double  dMbPerT;          /* Local variable so limits can be applied */
   double  dResponse;        /* Normalized response of seis. at dMbPer */
   int     iDelta;           /* Rounded epicentral distance */
   int     iDep;             /* Epicentral depth b-value index */

/* Set period to local variable so it can be changed */
   dMbPerT = dMbPer;
   dAmp    = dMbAmp;
   
/* Force period from 0.1 second to 3.0 seconds */
   if ( dMbPer < 0.1 )     dMbPerT = 0.1;
   else if ( dMbPer > 3. ) dMbPerT = 3.;
   dMbPerT *= 10.;

/* Determine which response curve to use */
   if ( !strcmp( pszChan, "SHZ" ) || !strcmp( pszChan, "SMZ" ) )	
      dResponse = dSPResp[(int) (dMbPerT-1.+0.5)]; /* WC/ATWC high sp resp. */
   else if ( !strcmp( pszChan, "SLZ" ) )	
      dResponse = dSPLResp[(int) (dMbPerT-1.+0.5)];/* WC/ATWC low sp resp. */
   else if ( !strcmp( pszChan, "BHZ" ) || !strcmp( pszChan, "BHE" ) ||
             !strcmp( pszChan, "BHN" ) || !strcmp( pszChan, "HHZ" ) ||
             !strcmp( pszChan, "BHZ10" ) || 
             !strcmp( pszChan, "BHZ00" ) || !strcmp( pszChan, "BHZXX" ) ||
             !strcmp( pszChan, "HHE" ) || !strcmp( pszChan, "HHN" ) )	
      dResponse = dSPFResp[(int) (dMbPerT-1.+0.5)];/* Sp filter resp. for bb */
   else	
      dResponse = dSPFResp[(int) (dMbPerT-1.+0.5)];/* Assume sp filter */

/* Convert seismometer amplitude to ground displacement in nm (if necessary) */
   if ( dGain < 1.E7 && dGain > 0. )  /* Use analog or heli gain */
      dAmp = (dMbAmp*1000.0) / (dGain*dResponse);
   else	if ( dGain > 0. )             /* Use digital broadband gains */
      dAmp = (dMbAmp*1.e9) / (2.*PI*dGain*dResponse*(1./(dMbPerT/10.0)));
   
/* Get indices to use in B-value table */
   iDelta = (int) (dDelta + 0.49999) - 1; /* Round dist to nearest int (-1) */
   if ( iDelta >= 99 ) iDelta = 99;       /* B value table begins at 1 degree */
   if ( iDelta < 0 )   iDelta = 0;
   iDep = (int) ((dDepth + 37.5) / 25.) - 1; /* Get B-value depth index */
   if ( iDep >= 24 ) iDep = 24;

/* Compute and return Mb magnitude */
   if ( dAmp <= 1.0 ) dAmp = 1.0;
   return( log10( dAmp/(dMbPerT/10.) ) + 0.1*(double) iBVal[iDelta*25 + iDep] );
}

      /******************************************************************
       *                     ComputeMBMag()                             *
       *                                                                *
       * This function computes the long period, body wave magnitude,   *
       * MB.  In these programs, two body wave magnitudes are computed  *
       * (Mb and MB).  The smaller case b implies short period p-wave   *
       * Richter magnitudes while the large case implies long period.   *
       *                                                                *
       *  Arguments:                                                    *
       *   pszChan        Seismic data channel (e.g. BHZ, LHZ...)       *
       *   dGain          Seismometer amplification (see note at top)   *
       *                  (if gain = 0., MBAmp must be ground motion    *
       *                   in nm (peak-to-trough)                       *
       *   dMBAmp         Signal amplitude (computer units or nm        *
       *                   depending on dGain)                          *
       *   dMBPer         Period of cycle of interest (seconds)         *
       *   dDelta         Epicentral distance in degrees                *
       *   dDepth         Hypocenter depth in km                        *
       *                                                                *
       *  Return:                                                       *
       *   double - MB Magnitude                                        *
       ******************************************************************/
	   
double ComputeMBMag( char * pszChan, double dGain, double dMBAmp,
                     double dMBPer, double dDelta, double dDepth )
{
   double  dAmp;             /* Ground displacement (nm) */
   double  dMBPerT;          /* Local variable so limits can be applied */
   double  dResponse;        /* Normalized response of seis. at dMBPer */
   int     iDelta;           /* Rounded epicentral distance */
   int     iDep;             /* Epicentral depth b-value index */

/* Set period to local variable so it can be changed */
   dMBPerT = dMBPer;
   dAmp    = dMBAmp;
   
/* Force period from 1.0 second to 30.0 seconds */
   if ( dMBPer < 1. )       dMBPerT = 1.;
   else if ( dMBPer > 30. ) dMBPerT = 30.;
   
/* Determine which response curve to use */
   else if ( !strcmp( pszChan, "LHZ" ) || !strcmp( pszChan, "LLZ" ) )	
      dResponse = dLPResp[(int) (dMBPerT-1.+0.5)];  /* WC/ATWC lp response */
   else if ( !strcmp( pszChan, "BHZ" ) || !strcmp( pszChan, "BHE" ) ||
             !strcmp( pszChan, "BHN" ) || !strcmp( pszChan, "HHZ" ) ||
             !strcmp( pszChan, "BHZ10" ) || 
             !strcmp( pszChan, "BHZ00" ) || !strcmp( pszChan, "BHZXX" ) ||
             !strcmp( pszChan, "HHE" ) || !strcmp( pszChan, "HHN" ) )	
      dResponse = dLPFResp[(int) (dMBPerT-1.+0.5)]; /* LP filter response */
   else
      dResponse = dLPFResp[(int) (dMBPerT-1.+0.5)]; /* Assume lp filter resp. */

/* Convert seismometer amplitude to ground displacement in nm (if necessary) */
   if ( dGain < 1.E7 && dGain > 0. )     /* Use analog or heli gain */
      dAmp = (dMBAmp*1000.0) / (dGain*dResponse);
   else if ( dGain > 0. )                 /* Use digital broadband gains */
      dAmp = (dMBAmp*1.e9) / (2.*PI*dGain*dResponse*(1./dMBPerT));

/* Get indices to use in B-value table */
   iDelta = (int) (dDelta + 0.49999) - 1; /* Round dist. to nearest int (-1) */
   if ( iDelta >= 99 ) iDelta = 99;       /* B value table begins at 1 degree */
   if ( iDelta < 0 )   iDelta = 0;
   iDep = (int) ((dDepth + 37.5) / 25.) - 1; /* Get B-value depth index */
   if ( iDep >= 24 ) iDep = 24;

/* Compute and return MB magnitude */
   if ( dAmp <= 1.0 ) dAmp = 1.0;
   return( log10( dAmp/dMBPerT ) + 0.1*(double) iBVal[iDelta*25 + iDep] );
}

 /***********************************************************************
  *                         ComputeMbMl()                               *
  *                                                                     *
  *      Compute magnitude parameters (period and amplitude) for mb     *
  *      and Ml computations.                                           *
  *                                                                     *
  *  November, 2004: Reverted back to taking highest velocity amplitude *
  *                  as in ANALYZE and has been done historically at    *
  *                  WC/ATWC.                                           *
  *                                                                     *
  *  Arguments:                                                         *
  *     Sta              Pointer to station being processed             *
  *     iIndex           # samples since P-time                         *
  *     pPBuf            PPick data buffer                              *
  *     iMbCycles        Number of cycles to allow for Mb (after,its Ml)*
  *                                                                     *
  ***********************************************************************/
  
void ComputeMbMl( STATION *Sta, int iIndex, PPICK *pPBuf, int iMbCycles )
{     
/* Process period and amplitude */
   Sta->lCycCnt++;

/* Compute full period in seconds*10 */
   Sta->lPer = (long) ((double) ((2 * (Sta->lSampsPerCyc+1)) / 
                Sta->dSampRate)*10. + 0.0001);
					 
/* Max period is 3s (due to response curves) */
   if ( Sta->lPer > 30 ) Sta->lPer = 30;
			   
/* Min period is 0.3s. (This is done for compatibility with previous
   magnitude computations performed at WC/ATWC. It doesn't make much sense,
   but magnitudes are more accurate and consistent with this edict.) */
   if ( Sta->lPer < 3 ) Sta->lPer = 3;
			   
/* Reset cycle counter for Ml period/amplitudes if Mb has passed */
   if ( Sta->lCycCnt == iMbCycles ) Sta->dMaxPk = 0.;   
		 
/* Compare present ground motion amplitude to maximum, update max and mag
   params if needed */
   if ( MbMlGroundMotion( Sta->szChannel, Sta->dSens, Sta->lPer,
      labs( Sta->lMDFRunning ) ) >= Sta->dMaxPk )
   {
      Sta->dMaxPk = MbMlGroundMotion( Sta->szChannel, Sta->dSens, Sta->lPer,
                    labs( Sta->lMDFRunning ) );

/* If we are within the first MbCycles 1/2 cycles, get mb; else get Ml */
      if ( Sta->lCycCnt < iMbCycles )
      {
         pPBuf->dMbPer = (double) Sta->lPer / 10.;
         pPBuf->dMbAmpGM = Sta->dMaxPk;
         pPBuf->iMbClip = 0;
         if ( (double) labs( Sta->lMDFRunning ) >= Sta->dClipLevel &&
               Sta->dClipLevel > 0. )
            pPBuf->iMbClip = 1;
         pPBuf->dMbTime =  pPBuf->dPTime + (double) iIndex/Sta->dSampRate;			   
      }

/* Fill Ml variables if past MbCycles */            
      else 
      {
         pPBuf->dMlPer = (double) Sta->lPer / 10.;
         pPBuf->dMlAmpGM = Sta->dMaxPk;
         pPBuf->iMlClip = 0;
         if ( (double) labs( Sta->lMDFRunning ) >= Sta->dClipLevel &&
               Sta->dClipLevel > 0. )
            pPBuf->iMlClip = 1;
         pPBuf->dMlTime =  pPBuf->dPTime + (double) iIndex/Sta->dSampRate;			   
      }       
   }
return;
}

      /******************************************************************
       *                     ComputeMlMag()                             *
       *                                                                *
       * This function computes the local magnitude, Ml, from short     *
       * period s/Lg waves.  The formulas from "Determining magnitude   *
       * values from the Lg phase from short period vertical            *
       * seismographs", by John G. Sindorf, EARTHQUAKE NOTES, pp. 5-8,  *
       * V43, #3, 1972, are used here to get the Ml.                    *
       *                                                                *
       *  Arguments:                                                    *
       *   pszChan        Seismic data channel (e.g. BHZ, LHZ...)       *
       *   dGain          Seismometer amplification (see note at top)   *
       *                  (if gain = 0., MlAmp must be ground motion    *
       *                   in nm (peak-to-trough)                       *
       *   dMlAmp         Signal amplitude (computer units or nm        *
       *                   depending on dGain)                          *
       *   dMlPer         Period of cycle of interest (seconds)         *
       *   dDelta         Epicentral distance in degrees                *
       *                                                                *
       *  Return:                                                       *
       *   double - Ml Magnitude                                        *
       ******************************************************************/
	   
double ComputeMlMag( char *pszChan, double dGain, double dMlAmp,
                     double dMlPer, double dDelta )
{
   double  dAmp;              /* Ground displacement (nm) */
   double  dDeltaT, dMlPerT;  /* Local var. so limits can be applied */
   double  dResponse;         /* Normalized response of seis. at dMlPer */

/* Set some local variables so they can be changed */
   dMlPerT = dMlPer;
   dDeltaT = dDelta;
   dAmp    = dMlAmp;

/* Force period from 0.1 second to 3.0 seconds */
   if ( dMlPer < 0.1 )     dMlPerT = 0.1;
   else if ( dMlPer > 3. ) dMlPerT = 3.;
   dMlPerT *= 10.;

/* Make sure distance is reasonable */
   if ( dDelta <= 0.0 ) dDeltaT = 0.5;

/* Determine which response curve to use */
   if ( !strcmp( pszChan, "SHZ" ) || !strcmp( pszChan, "SMZ" ) )	
      dResponse = dSPResp[(int) (dMlPerT-1.+0.5)]; /* WC/ATWC high sp resp. */
   else if ( !strcmp( pszChan, "SLZ" ) )	
      dResponse = dSPLResp[(int) (dMlPerT-1.+0.5)];/* WC/ATWC low sp resp. */
   else if ( !strcmp( pszChan, "BHZ" ) || !strcmp( pszChan, "BHE" ) ||
             !strcmp( pszChan, "BHN" ) || !strcmp( pszChan, "HHZ" ) ||
             !strcmp( pszChan, "BHZ10" ) || 
             !strcmp( pszChan, "BHZ00" ) || !strcmp( pszChan, "BHZXX" ) ||
             !strcmp( pszChan, "HHE" ) || !strcmp( pszChan, "HHN" ) )	
      dResponse = dSPFResp[(int) (dMlPerT-1.+0.5)];/* Sp filter resp. for bb */
   else	
      dResponse = dSPFResp[(int) (dMlPerT-1.+0.5)];/* Assume sp filter */

/* Convert seismometer amplitude to ground displacement in nm (if necessary) */
   if ( dGain < 1.E7 && dGain > 0. ) /* Use analog or heli gain */
      dAmp = (dMlAmp*1000.0) / (dGain*dResponse);
   else if ( dGain > 0. )            /* Use digital broadband gains */
      dAmp = (dMlAmp*1.e9) / (2.*PI*dGain*dResponse*(1./(dMlPerT/10.0)));

/* Compute and return Ml magnitude */
   if ( dAmp <= 1.0 ) dAmp = 1.0;
   if ( dDeltaT < 1.65 )
      return( log10( dAmp/(dMlPerT/10.) )-0.066 +
	           0.8*(log10( dDeltaT*dDeltaT )) );
   else
      return( log10( dAmp/(dMlPerT/10.) ) - 0.364 +
	           1.5*(log10( dDeltaT*dDeltaT )) );
}

      /******************************************************************
       *                     ComputeMSMag()                             *
       *                                                                *
       * This function computes surface wave magnitudes.  The           *
       * traditional surface wave magnitude formula is used with a      *
       * distance correction factor proposed by Whitmore and Sokolowski *
       * (1987).                                                        *
       *                                                                *
       *  Arguments:                                                    *
       *   pszChan        Seismic data channel (e.g. BHZ, LHZ...)       *
       *   dGain          Seismometer amplification (see note at top)   *
       *                  (if gain = 0., MSAmp must be ground motion    *
       *                   in um (peak-to-trough)                       *
       *   dMSAmp         Signal amplitude (computer units or um        *
       *                   depending on dGain - !!! Units different for *
       *                   MS)                                          *
       *   dMSPer         Period of cycle of interest (seconds)         *
       *   dDelta         Epicentral distance in degrees                *
       *                                                                *
       *  Return:                                                       *
       *   double - MS Magnitude                                        *
       ******************************************************************/
	   
double ComputeMSMag( char *pszChan, double dGain, double dMSAmp,
                     double dMSPer, double dDelta )
{
   double  dAmp;             /* Ground displacement (um) */
   double  dCor;             /* Distance correction (for <16 degrees) */
   double  dDeltaT, dMSPerT; /* Local variables so limits can be applied */
   double  dResponse;        /* Normalized response of seis. at dMSPer */

/* Set some local variables so they can be changed */
   dMSPerT = dMSPer;
   dDeltaT = dDelta;
   dAmp    = dMSAmp;
   
/* Force period from 1.0 second to 30.0 seconds */
   if ( dMSPer < 1. )       dMSPerT = 1.;
   else if ( dMSPer > 30. ) dMSPerT = 30.;
   
/* Prevent log (0) */
   if ( dDelta == 0.0 ) dDeltaT = 0.1;

/* Compute distance correction */
   if ( dDeltaT <= 16.0 ) dCor = 0.53 - (0.033 * dDeltaT);
   else                   dCor = 0.0;

/* Determine which response curve to use */
   if ( !strcmp( pszChan, "LHZ" ) || !strcmp( pszChan, "LLZ" ) )	
      dResponse = dLPResp[(int) (dMSPerT-1.+0.5)]; /* WC/ATWC lp response */
   else if ( !strcmp( pszChan, "BHZ" ) || !strcmp( pszChan, "BHE" ) ||
             !strcmp( pszChan, "BHN" ) || !strcmp( pszChan, "HHZ" ) ||
             !strcmp( pszChan, "BHZ10" ) || 
             !strcmp( pszChan, "BHZ00" ) || !strcmp( pszChan, "BHZXX" ) ||
             !strcmp( pszChan, "HHE" ) || !strcmp( pszChan, "HHN" ) )	
      dResponse = dLPFResp[(int) (dMSPerT-1.+0.5)];/* LP filter response */
   else
      dResponse = dLPFResp[(int) (dMSPerT-1.+0.5)];/* Assume LP filter resp. */

/* Convert seismometer amplitude to ground displacement in um (if necessary)
   (- reponse) */
   if ( dGain < 1.E7 && dGain > 0. )  /* Use analog or heli gain */
      dAmp = (dMSAmp*1000.0) / (dGain*dResponse);
   else if ( dGain > 0. )             /* Use digital broadband gains */
      dAmp = (dMSAmp*1.e9) / (2.*PI*dGain*dResponse*(1./dMSPerT));

/* Compute and return MS magnitude (in MS formula, Amp in MicroMeters) */
   if ( dAmp <= 1.0 ) dAmp = 1.0;
   return( log10( dAmp/dMSPerT ) + 1.66*log10( dDeltaT ) + 3.0 + dCor );
}

      /******************************************************************
       *                     ComputeMwMag()                             *
       *                                                                *
       * This function computes moment magnitudes.  Either Brune's      *
       * technique or Kanamori's technique is used depending on the     *
       * epicentral distance.  If the distance is greater than 15       *
       * degrees, a slight modification of the original technique       *
       * proposed by Brune in 1969 is used.  Input must be from a 100s  *
       * period vertical seismometer.  For distances less than 15       *
       * degrees, Kanamori's 1985 technique is used.  Method 2 with a   *
       * constant of 7.9 was found to work best.                        *
       *                                                                *
       *  Arguments:                                                    *
       *   dGain          Seismometer amplification (see note at top)   *
       *                  (if gain = 0., MwAmp must be ground motion    *
       *                   in mm (peak-to-trough)                       *
       *   dMwAmp         Signal amplitude (computer units or mm        *
       *                   depending on dGain - !!! Units different for *
       *                   Mw)                                          *
       *   dMwPer         Period of cycle of interest (seconds)         *
       *   dDelta         Epicentral distance in degrees                *
       *                                                                *
       *  Return:                                                       *
       *   double - Mw Magnitude                                        *
       ******************************************************************/
	   
double ComputeMwMag( double dGain, double dMwAmp, double dMwPer, double dDelta )
{
   double  dAmp;       /* Ground displacement (units depends on Mw type) */
/* Moment computed from 100s Rayleigh wave amp */
   double  dBruneFactor[51] = {24.3,24.4,24.5,24.6,24.7,24.8,24.9,25.0,
    25.1,25.2,25.3,25.4,25.5,25.6,25.7,25.8,25.9,26.0,26.1,26.2,26.4,
    26.6,26.9,27.2,27.4,27.5,27.6,27.7,27.8,27.9,28.0,28.1,28.2,
    28.3,28.4,28.5,28.6,28.7,28.8,28.9,29.0,29.1,29.2,29.3,29.4,
    29.5,29.6,29.7,29.8,29.9,30.0};
/* Distance/amplitude correction factors */
   double  dBruneMag[50] = {-4.5,-4.4,-4.29,-4.19,-4.1,-4.,-3.9,-3.8,
    -3.7,-3.6,-3.5,-3.4,-3.3,-3.19,-3.09,-2.99,-2.89,-2.79,-2.69,-2.59,
    -2.42,-2.18,-1.94,-1.71,-1.49,-1.3,-1.21,-1.12,-1.04,-0.95,
    -0.85,-0.77,-0.67,-0.58,-0.49,-0.4,-0.31,-0.22,-0.13,-0.04,0.05,0.13,
    0.22,0.31,0.4,0.5,0.58,0.67,0.76,0.85};
/* Distance correction factors */
   double  dBruneVal[181] = {0.05,0.05,0.06,0.07,0.07,0.08,0.08,0.09,
    0.09,0.10,0.11,0.12,0.13,0.14,0.15,0.15,0.16,0.17,0.18,0.19,0.20,0.21,0.22,
    0.23,0.25,0.26,0.27,0.28,0.29,0.30,0.31,0.32,0.33,0.34,0.35,0.36,0.37,
    0.38,0.39,0.40,0.41,0.42,0.43,0.44,0.45,0.46,0.47,0.48,0.49,0.50,0.51,
    0.52,0.53,0.54,0.55,0.56,0.57,0.58,0.59,0.60,0.61,0.62,0.63,0.64,0.65,
    0.68,0.69,0.70,0.72,0.73,0.74,0.75,0.77,0.78,0.79,0.80,0.81,0.82,0.84,
    0.85,0.87,0.88,0.90,0.91,0.93,0.94,0.96,0.97,0.99,1.01,1.03,1.04,1.05,
    1.07,1.08,1.09,1.10,1.12,1.13,1.13,1.14,1.14,1.15,1.16,1.17,1.17,1.18,
    1.19,1.19,1.20,1.21,1.21,1.22,1.23,1.24,1.25,1.26,1.26,1.27,1.28,1.28,
    1.29,1.29,1.30,1.30,1.30,1.31,1.31,1.32,1.32,1.32,1.32,1.33,1.33,1.33,
    1.33,1.34,1.34,1.34,1.34,1.32,1.32,1.32,1.31,1.30,1.29,1.28,1.27,1.27,
    1.26,1.23,1.21,1.18,1.16,1.13,1.10,1.08,1.05,1.03,1.00,0.96,0.92,
    0.88,0.84,0.80,0.76,0.72,0.68,0.64,0.60,0.56,0.52,0.48,0.44,0.40,0.36,
    0.32,0.28,0.24,0.24,0.32};
   double  dDeltaT, dMwPerT; /* Local variables so limits can be applied */
   double  dMwMag, dMagMm, dBruneMoment;   /* Temporary values */
   double  dResponse;        /* Normalized response of seis. at dMSPer */
/* Response of the old WC&ATWC VLP set-up */
   double  dVLPResp[20] = {0.4,2.1,5.4,12.0,18.,32.,43.,60.,78.,100.,
                           110.,109.,100.,92.,82.,72.,62.,54.,46.,41.};
   int     i;
   int     iMoment;

/* Set some local variables so they can be changed */
   dMwPerT = dMwPer;
   dDeltaT = dDelta;
   dAmp    = dMwAmp;
   
/* Force period from 10.0 seconds to 200.0 seconds */
   if ( dMwPer < 10. )       dMwPerT = 10.;
   else if ( dMwPer > 200. ) dMwPerT = 200.;
   dMwPerT /= 10.;
   
/* Make sure distance is reasonable */
   if ( dDelta <= 0.0 )  dDeltaT = 50.;
   if ( dDelta > 180.0 ) dDeltaT = 180.;

/* Use Brune's method if distance 15 degrees of greater */
   if ( dDeltaT >= 15. )
   {                                  /* Period is period*0.1 */
      if ( dGain > 1.E7 && dGain > 0. )  /* Digital broadband station */
         dResponse = 1.;                 /* Assume 1. for now */
      else if ( dGain > 0. )          /* Use WC&ATWC VLP recorder resp. */
         dResponse = dVLPResp[(int) (dMwPerT+0.5)-1] * 0.01;
      else
         dResponse = 1.;
		 
/* Compute distance corrected amplitude (20 to convert p-p mm to 0-p cm) */
      if ( dGain < 1.E7 && dGain > 0.)    /* Use analog or heli gain */
         dAmp = log10( (dMwAmp / ((dGain*1000.) * dResponse * 20.)) * 
                dBruneVal[(int) (dDeltaT+0.5)-1] );
      else if ( dGain > 0. )              /* Digital broadband station */
         dAmp = log10( ((dMwAmp*1000.) / (2.*PI*dGain*dResponse*20.)) * 
                dBruneVal[(int) (dDeltaT+0.5)-1] );
      else                                /* Incoming amp ground motion in mm */
         dAmp = (dMwAmp/20.) * dBruneVal[(int) (dDeltaT+0.5)-1];
      for ( i=0; i<50; i++ )
         if ( dAmp <= dBruneMag[i] ) break;                        
      i += 1;
	  
/* Compute magnitude Mm */
      dMagMm = (((double) i - 1.0)*0.1) + 5.0;
	
/* Convert this to moment and moment magnitude */
      iMoment = (int) ((dMagMm - 4.9) * 10. + 0.1);
      dBruneMoment = pow( 10., dBruneFactor[iMoment-1] );
      dMwMag = 0.66667 * log10( dBruneMoment ) - 10.7;	
   }
   else         /* Use Kanamori's method 2 w/ constant=7.9  (distance < 15) */
   {            /* Convert p-p mm to p-p cm on gram with gain of 0.1 */
      dAmp = dMwAmp / 10.;
      if ( dGain < 1.E7 && dGain > 0.)   /* Use analog or heli gain */
         dAmp = dAmp*0.1 / (dGain*1000.);
      else if ( dGain > 0. )             /* Digital broadband gain */
         dAmp = (dAmp*1000.) * 0.1 / (2.*PI*dGain/dMwPerT);  /* ??? */
/* Compute magnitude */
      dAmp *= pow( dDeltaT, 0.6 );
      dMwMag = 0.66667 * log10( dAmp ) + 7.9;
   }
return( dMwMag );
}

      /******************************************************************
       *                     ComputeMwpMag()                            *
       *                                                                *
       * This function computes moment magnitudes based on an integrated*
       * p-wave displacement seismogram (Mwp).  The technique was       *
       * developed by Tsuboi, et al., 1995, BSSA.  In short, we assume  *
       * the P-wave is recorded on a seismometer with a velocity        *
       * response flat over the period of the P-wave.  This signal is   *
       * integrated to get a displacement seismogram and then integrated*
       * again to get an approximation of the moment rate function (the *
       * integrated displacement seismogram). The maximum of this       *
       * integrated displacement seismogram is taken and adjusted by a  *
       * factor determined by simplifying source, path, and receiver    *
       * functions.  An accurate moment magnitude is determined here    *
       * only when the azimuthal coverage of the epicenter is good.  An *
       * average is then taken and 0.2 added to the average to account  *
       * for the radiation pattern.  Some problems with this technique  *
       * are noted here: 1.) The technique works well for quakes in the *
       * range 6-7.5.  Above this we sometimes underestimate the        *
       * magnitude.  2.) Seismometers with insufficient response        *
       * characteristics (such as the USNSN flat velocity response to   *
       * 30s) also underestimate the magnitude.  3.) If the DC is not   *
       * properly removed from the signal, the Mwp will be overestimated*
       * due to an integration of the DC component.  4.) The LP signal  *
       * must be above background levels otherwise the main component of*
       * the integrations will be on noise.  5.) The integration window *
       * must be lengthened in multiple shock quakes so that the entire *
       * P wave is within the window.                                   *
       *                                                                *
       * The integration window should be specified so that one cycle   *
       * of the P wave displacement seismogram is covered.  The         *
       * technique was developed for shallow, regional quakes is Japan. *
       * Studies at the WC&ATWC show that the technique also works for  *
       * deep and teleseismic quakes world-wide. This study along with  *
       * Tsuboi suggest that we should base the maximum integrated      *
       * displacement on the difference between the first and second    * 
       * peak of the integrated displacement when that difference is    * 
       * larger than the first peak alone.                              *
       *                                                                *
       * NOTE: The gain is not needed here as the maximum integrated    *
       * displacement is given in s*m.                                  *
       *                                                                *
       * Sept., 2008: Added distnace dependent correction based on      *
       *              Paul Huang's analysis of Mwp distance variations. *
       * March, 2002: Added magnitude dependent correction based on     *
       *              computed Mwp vs. Harvard Mw relation              *
       *              (Cor Mw=(Mw-1.02)/0.845).                         *
       *                                                                *
       *  Arguments:                                                    *
       *   dMaxIntDisp    Maximum (absolute) of integrated displacement *
       *                   in s*m.                                      *
       *   dDelta         Epicentral distance in degrees                *
       *                                                                *
       *  Return:                                                       *
       *   double - Mwp Magnitude with corrections                      *
       ******************************************************************/
	   
double ComputeMwpMag( double dMaxIntDisp, double dDelta )
{
   double  dDeltaT;            /* Temp variables */
   double  dMoment;            /* Seismic moment in N-m */
   double  dMwp;               /* Uncorrected Mwp magnitude */
   double V_p;

   dDeltaT = dDelta;
   
/* Prevent log (0) */
   if ( dDelta == 0.0 ) dDeltaT = 0.1;
   
   V_p = (160.0*dDeltaT + 7900.0);
   

   dMoment = 4*PI*3400*V_p*V_p*V_p* dDeltaT * 111194.9 * dMaxIntDisp;
   if ( dMoment > 0. )
   {
      dMwp = 1./1.5*(log10( dMoment ) - 9.1);
      //dMwp = (dMwp-1.03)/0.843;
      //logit( "t", "Distance=%lf, Original Mwp=%lf, Correction=%lf, Mwp=%lf\n",
      //       dDeltaT, dMwp, Mwp_adjustment( dDeltaT ),
      //       dMwp - Mwp_adjustment( dDeltaT ) );
/* Magnitude and distance-based corrections added below */
      return( dMwp ); 
   }
   else 
      return( 0.0 );
}

 /***********************************************************************
  *                           detrend()                                 *
  *                                                                     *
  *  This routine detrends the input data.  Currently can select linear,*
  *  quadratic or cubic detrending. Pass in order = 1, 2, 3 to select.  *
  *  Higher order will require a more general matrix technique          *
  *                                                                     *
  *  Arguments:                                                         *
  *     int_len          Maximum Window length (s)                      *
  *     dt               Time interval between samples (s)              *
  *     Z_in             Displacement signal                            *
  *     ncount           Number of samples of Z_in                      *
  *     prior_motion     RMS noise level (m/s)                          *
  *     vZ               Velocity signal (ms)                           *
  *     Z_out            Detrended Displacement signal                  *
  *     order            Polynomial order                               *
  *     S_to_N           Necessary S:N                                  *
  *                                                                     *
  *  Return:             -1 - poor S:N                                  *
  *                      -2 - moderate S:N                              *
  *                                                                     *
  ***********************************************************************/
int detrend( double int_len, double dt, double Z_in[], long ncount,
             double prior_motion, double vZ[], double Z_out[], int order,
             double S_to_N )
{
   double x, z, sum1 = 0., sum2 = 0., sum3 = 0.;
   double sum4 = 0., sum5 = 0., sum6 = 0.;
   double z1 = 0., z2 = 0., z3 = 0.;
   double x1, x2, x3;
   double slope, slope2, curv, cube, denom;
   double error_linear, error_quadratic, rms_signal_amp;
   long  n, ntop;
   int top, result = 0;
  
/* Initialize Z_out */
   for ( n=0; n<ncount; n++ )
      Z_out[n] = Z_in[n] - Z_in[0];
	
/* Avoid any trailing zeroes */
   ncount -= 2;

/* Initialize data */
   error_linear = 0.;
   error_quadratic = 0.;
   rms_signal_amp = 0.;
   slope = 0.;
   slope2 = 0.;
   x = 0.;
   ntop = (int) (int_len / dt);
   if (ntop > ncount) ntop = ncount;
   top = (int) (20. / dt);
   if (top > ntop) top = ntop;

/* Get the rms signal strength prior to detrending and test for SN ratio */
   for ( n=0; n<top; n++ )
      rms_signal_amp += (vZ[n] * vZ[n]);
   rms_signal_amp = sqrt( rms_signal_amp / (double) top );
  
   if ( (rms_signal_amp / prior_motion) < S_to_N )
      return (-1);

/************* detrend Z  ****************************/
/*   remove least squares linear component of Z(t)   */
   if ( order == 1 )
   {
      for ( n=0; n<ntop; n++ )
      {
         x = (double) n;
         sum1 += (Z_out[n] * x);
         sum2 += (x * x);
      }
      slope = sum1 / sum2;
      for ( n=0; n<ncount; n++ )
      {
         x = (double) n;
         Z_out[n] -= (slope * x); 
      }    
      result = 1;
      x = (double) ntop;
      error_linear = sqrt( slope*slope * x*x / 3. );
	
/* Compute a quadratic detrend term to check quality of linear fit */
      sum1 = 0.;
      sum2 = 0.;
      sum3 = 0.;
      sum4 = 0.;
      sum5 = 0.;
      for ( n=0; n<ntop; n++ )
      {
         x = (double) n;
         z = Z_in[n] - Z_in[0];
         sum1 += (z * x);
         sum2 += (x * x);
         sum3 += (x * x * x);
         sum4 += (z * x * x);
         sum5 += (x * x * x * x);
      }
      slope2 = (sum1 * sum5 - sum4 * sum3) / (sum2 * sum5 - sum3 * sum3);
      curv = (sum2 * sum4 - sum1 * sum3) / (sum2 * sum5 - sum3 * sum3);
      error_quadratic = slope2 * slope2 / 3.;
/*      error_quadratic = (slope2 -slope)*(slope2-slope) / 3.; ANALYZE version*/
      error_quadratic += (curv * slope2 * x / 2.);
/*      error_quadratic += (curv * (slope2-slope) * x / 2.); ANALZYE version */
      error_quadratic += (curv * curv * x * x / 5.);
      error_quadratic = sqrt(error_quadratic) * x;
	
/* Scale the errors to signal strength */
      error_linear = error_linear / rms_signal_amp;
      error_quadratic = error_quadratic / rms_signal_amp;

/* If S/N moderate or linear fit poor, return -2 */
  /* modified the IF below on 12-29-04  B. Knight  */
      if ( (rms_signal_amp/prior_motion) < 3.5 )
      {
        logit( "", "S/N marginal - %e or linear term too big - %e\n",
               rms_signal_amp / prior_motion, error_linear); 
        return (-2);
      } 
   }
	
/* Remove least squares quadratic component of Z(t) */
   if ( order == 2 )
   {
      for ( n=0; n<ntop; n++ )
      {
         x = (double) n;
         sum1 += (Z_out[n] * x);
         sum2 += (x * x);
         sum3 += (x * x * x);
         sum4 += (Z_out[n] * x * x);
         sum5 += (x * x * x * x);
      }
      slope = (sum1 * sum5 - sum4 * sum3) / (sum2 * sum5 - sum3 * sum3);
      curv = (sum2 * sum4 - sum1 * sum3) / (sum2 * sum5 - sum3 * sum3);

      for ( n=0; n<ncount; n++ )
      {
         x = (double) n;
         Z_out[n] -= ((slope + curv*x) * x); 
      }    
      result = 2;
   }

/* Remove least squares cubic component of Z(t) */
   if ( order == 3 )
   {
      for ( n=0; n<ntop; n++ )
      {
         x1 = (double) n;
         x2 = x1 * x1;
         x3 = x2 * x1;
         z1 += (Z_out[n] * x1);
         z2 += (Z_out[n] * x2);
         z3 += (Z_out[n] * x3);
         sum2 += x2;
         sum3 += x3;
         sum4 += (x2 * x2);
         sum5 += (x3 * x2);
         sum6 += (x3 * x3);
      }

      slope = (sum4*sum6 - sum5*sum5) * (sum4*z2 - sum5*z1);
      slope -= (sum3*sum5 - sum4*sum4) * (sum5*z3 - sum6*z2);
      denom = (sum4*sum6 - sum5*sum5) * (sum3*sum4 - sum2*sum5);
      denom -= (sum3*sum5 - sum4*sum4) * (sum5*sum4 - sum6*sum3);
      slope /= denom;
      curv = sum5*z1 - sum4*z2 - slope * (sum5*sum2 - sum3*sum4);
      curv /= (sum3*sum5 - sum4*sum4);
      cube = (z1 - slope*sum2 - curv*sum3) / sum4;

      for ( n=0; n<ncount; n++ )
      {
         x = (double) n;
         Z_out[n] -= ((slope + curv*x + cube*x*x) * x); 
      }
      result = 3;
   }
   return ( result );
}            

 /***********************************************************************
  *                            GetMbMl()                                *
  *      Compute magnitude parameters (period and amplitude) for mb     *
  *      and Ml computations.  Also, discriminate sine wave cals from   *
  *      P-picks. (Gains are no longer automatically updated.  This must*
  *      be done interactively.)                                        *
  *                                                                     *
  *  Arguments:                                                         *
  *     Sta              Pointer to station being processed             *
  *     iIndex           Present sample index                           *
  *     ucMyModID        Calling module ID                              *
  *     siPRegion        Calling module Pick Ring                       *
  *     ucEWHTypePickTWC Earthworm pick message number                  *
  *     ucEWHMyInstID    Earthworm Institute ID                         *
  *     iMbCycles        Number of cycles past which Ml is computed     *
  *     iRT              1->called from pick_wcatwc; 0->not real-time   *
  *                                                                     *
  *  Return:             -1 if cal over, 0 otherwise                    *
  *                                                                     *
  ***********************************************************************/
  
int GetMbMl( STATION *Sta, int iIndex, unsigned char ucMyModID,
             SHM_INFO siPRegion, unsigned char ucEWHTypePickTWC,
             unsigned char ucEWHMyInstID, int iMbCycles, int iRT )
{
   PPICK   PTemp;       /* Temp structure for pass to ReportPick */

/* Process period and amplitude if sensitivity is known */
   if ( Sta->dSens > 0. )
   {     /* Increment MbCycles counter */
      Sta->lCycCnt++;
      if ( Sta->lCycCnt >= 3 )
      {     /* Ignore first 2 cycles in sine-wave cal discrimation */
         Sta->lMagAmp += labs (Sta->lMDFRunning);  
         Sta->dAvAmp = (double) Sta->lMagAmp / (double) (Sta->lCycCnt - 2);

/* Check to see if amplitude constant (it will be for SW cal) */
         if ( (double) labs (Sta->lMDFRunning) < (Sta->dAvAmp+Sta->dAvAmp/5.) &&
              (double) labs (Sta->lMDFRunning) > (Sta->dAvAmp-Sta->dAvAmp/5.) &&
              Sta->dAvAmp > (2.*Sta->dAveMDF) &&
              Sta->dAvAmp < Sta->dClipLevel ) Sta->lSWSim++;
      }

/* Next, check to see if period is near 1s (as it will be for SW cal) */
      if ( (2*(Sta->lSampsPerCyc+1) <= (long) (Sta->dSampRate + 
           Sta->dSampRate/5.)) &&
           (2*(Sta->lSampsPerCyc+1) >= (long) (Sta->dSampRate - 
           Sta->dSampRate/5.)) ) Sta->lSWSim++;

/* If first 3.5 cyc + 3s look like cal, it probably is */
      if ( Sta->lCycCnt <= 13 && Sta->lSWSim >= 18 )
      {
         Sta->iCal = 1;
         logit( "t", "cal on %s\n", Sta->szStation );
      }

      if ( !Sta->iCal )
      {
/* Compute full period in seconds*10 */
         Sta->lPer = (long) ((double) (2 * (Sta->lSampsPerCyc + 1)) / 
                     Sta->dSampRate * 10. + 0.0001);
					 
/* Max period is 3s (due to response curves) */
         if ( Sta->lPer > 30 ) Sta->lPer = 30;
			   
/* Min period is 0.3s. (This is done for compatibility with previous
   magnitude computations performed at WC/ATWC. It doesn't make much sense,
   but magnitudes are more accurate and consistent with this edict.) */
         if ( Sta->lPer < 3 ) Sta->lPer = 3;
			   
/* Reset cycle counter for Ml period/amplitudes if Mb has passed */
         if ( Sta->lCycCnt == iMbCycles ) Sta->dMaxPk = 0.;   
		 
/* Compare present ground motion amplitude to maximum, update max and mag
   params if needed */
         if ( MbMlGroundMotion( Sta->szStation, Sta->dSens, Sta->lPer,
              labs( Sta->lMDFRunning ) ) > Sta->dMaxPk )
         {
            Sta->dMaxPk = MbMlGroundMotion( Sta->szStation, Sta->dSens,
                           Sta->lPer, labs( Sta->lMDFRunning ) );

/* If we are within the first MbCycles 1/2 cycles, get mb; else get Ml */
            if ( Sta->lCycCnt < iMbCycles )
            {
               Sta->lMbPer = Sta->lPer;
               Sta->dMbAmpGM = Sta->dMaxPk;
               Sta->dMbTime = Sta->dStartTime +
                              (double) iIndex/Sta->dSampRate;			   
/* Log pick */
               if ( Sta->iPickStatus == 3 && iRT == 1)
                  ReportPick( &PTemp, Sta, ucMyModID,
                   siPRegion, ucEWHTypePickTWC, ucEWHMyInstID, 4 );
            }

/* Fill Ml variables if past MbCycles */            
            else 
            {
               Sta->lMlPer = Sta->lPer;
               Sta->dMlAmpGM = Sta->dMaxPk;
               Sta->dMlTime = Sta->dStartTime +
                              (double) iIndex/Sta->dSampRate;			   
/* Log pick */
               if ( Sta->iPickStatus == 3 && iRT == 1 ) 
                  ReportPick( &PTemp, Sta, ucMyModID,
                   siPRegion, ucEWHTypePickTWC, ucEWHMyInstID, 4 );
            }       
         } 
      }
      else
      {
/* Reset calibration variables after 80 seconds */
         if ( Sta->dStartTime >= Sta->dTrigTime + 80. )
            {
               Sta->iPickStatus = 1;
               InitVar( Sta );
               return (-1);
            }
      }
   }
return (0);
}

  /******************************************************************
   *                         GetMDFFilt()                           *
   *                                                                *
   *  Determine average of modified differential function (MDF) on  *
   *  filtered signal.                                              *
   *  NOTE: DC offset is disregarded here since we are looking at   *
   *        differences.                                            *
   *                                                                *
   *  Arguments:                                                    *
   *    lBIndex     Index to start background computations at       *
   *    lBNum       Number of background samples to evaluate        *
   *    Sta         Station data array                              *
   *                                                                *
   ******************************************************************/

void GetMDFFilt( long lBIndex, long lBNum, STATION *Sta )
{
   long    i, lTemp;
   long    lMDFTotal;           /* Sum of MDFs */

/* Initialize some things */
   Sta->lMDFOld = 0;
   lMDFTotal = 0;
   Sta->lMDFRunning = 0;
   Sta->lCycCnt = 0;
   
/* Loop through each point */   
   for ( i=0; i<lBNum; i++ )
   {
      lTemp = i + lBIndex;
      if ( lTemp >= Sta->lRawCircSize ) lTemp -= Sta->lRawCircSize;
      Sta->lSampNew = Sta->plFiltCircBuff[lTemp];
      if ( i == 0 ) Sta->lSampOld = Sta->lSampNew;
      Sta->lMDFNew = Sta->lSampNew - Sta->lSampOld;
	  
/* Check for cycle changes */
      if ( i > 0 )
      {
         if ( (Sta->lMDFOld <  0 && Sta->lMDFNew <  0) ||
              (Sta->lMDFOld >= 0 && Sta->lMDFNew >= 0) )
/* No changes, continuing adding up MDF */
            Sta->lMDFRunning += Sta->lMDFNew;
         else  /* Cycle has changed sign, update and start anew */
         {
            Sta->lCycCnt++;
            lMDFTotal += labs( Sta->lMDFRunning );
            Sta->lMDFRunning = Sta->lMDFNew;
         }
      }
      Sta->lSampOld = Sta->lSampNew;
      Sta->lMDFOld = Sta->lMDFNew;
   }
   
/* Compute the average MDF */   
   if ( Sta->lCycCnt == 0 ) Sta->lCycCnt = 1;
   Sta->dAveMDF = (double) lMDFTotal / (double) Sta->lCycCnt;
}

  /******************************************************************
   *                           GetNoise()                           *
   *                                                                *
   *  Determine average of modified differential function (MDF)on   *
   *  raw, unfiltered signal.                                       *
   *  NOTE: DC offset is disregarded here since we are looking at   *
   *        differences.                                            *
   *                                                                *
   *  Arguments:                                                    *
   *    lBIndex     Index to start background computations at       *
   *    lBNum       Number of background samples to evaluate        *
   *    Sta         Station data array                              *
   *                                                                *
   ******************************************************************/

void GetNoise( long lBIndex, long lBNum, STATION *Sta )
{
   long    i, lTemp;
   long    lHigh, lLow;         /* Pre-event signal high and low */

/* Initialize some things */
   lHigh = -10000000;
   lLow = 10000000;
   
/* Loop through each point prior to Mwp start, find high and low. */   
   for ( i=0; i<lBNum; i++ )
   {
      lTemp = i + lBIndex;
      if ( lTemp >= Sta->lRawCircSize ) lTemp -= Sta->lRawCircSize;   
      if (Sta->plRawCircBuff[lTemp] > lHigh) lHigh = Sta->plRawCircBuff[lTemp];
      if (Sta->plRawCircBuff[lTemp] < lLow) lLow = Sta->plRawCircBuff[lTemp];
   }   
   lTemp = lHigh - lLow;
   if (lTemp == 0) lTemp = 1;
   Sta->lRawNoiseOrig = lTemp;
}

      /******************************************************************
       *                      GetPreferredMag()                          *
       *                                                                *
       * This function determines which magnitude to assign to an       *
	   * earthquake out of those computed so far.                       *
       *                                                                *
       *  Arguments:                                                    *
       *   pHypo            Hypocenter parameter structure              *
       *                                                                *
       ******************************************************************/

void GetPreferredMag( HYPO *pHypo )
{
/* Determine which type of magnitude to assign to this quake.
   The top of the if is the most desirable magnitude, and so on... */   
   if ( pHypo->iNumMw >= 6 && pHypo->dMwAvg > 6.6 &&
       (pHypo->dMlAvg == 0. || pHypo->dMlAvg > 5.0) )
   {
      pHypo->iNumPMags = pHypo->iNumMw;
      strcpy( pHypo->szPMagType, "w" );
      pHypo->dPreferredMag = pHypo->dMwAvg;
   }
   else if ( pHypo->iNumMwp >= 3 && pHypo->dMwpAvg > 5.5 && pHypo->dMwpAvg < 10. ) 
   {
      pHypo->iNumPMags = pHypo->iNumMwp;
      strcpy( pHypo->szPMagType, "wp" );
      pHypo->dPreferredMag = pHypo->dMwpAvg;
   }
   else if ( pHypo->iNumMwp >= 3 && pHypo->dMwpAvg > 0. && pHypo->dMwpAvg < 10. &&
       (pHypo->dMlAvg == 0. || pHypo->dMlAvg > 5.0) )/*No Mwp for small locals*/
   {
      pHypo->iNumPMags = pHypo->iNumMwp;
      strcpy( pHypo->szPMagType, "wp" );
      pHypo->dPreferredMag = pHypo->dMwpAvg;
   }
   else if ( pHypo->iNumMS && pHypo->dMSAvg > 5.5 )
   {
      pHypo->iNumPMags = pHypo->iNumMS;
      strcpy( pHypo->szPMagType, "S" );
      pHypo->dPreferredMag = pHypo->dMSAvg;
   }
   else if ( (pHypo->iNumMb && pHypo->dMlAvg == 0.) ||
             (pHypo->iNumMb > 2) )
   {
      pHypo->iNumPMags = pHypo->iNumMb;
      strcpy( pHypo->szPMagType, "b" );
      pHypo->dPreferredMag = pHypo->dMbAvg;
   }
   else if ( pHypo->iNumMl )
   {
      pHypo->iNumPMags = pHypo->iNumMl;
      strcpy( pHypo->szPMagType, "l" );
      pHypo->dPreferredMag = pHypo->dMlAvg;
   }
   else
   {
      pHypo->iNumPMags = 0;
      strcpy( pHypo->szPMagType, "X" );
      pHypo->dPreferredMag = 0.;
   }
}   

 /***********************************************************************
  *                           integrate()                               *
  *                                                                     *
  *   This routine scans the Zt time series and picks out local extrema *
  *                                                                     *
  *  Arguments:                                                         *
  *     height1          Amplitude of first extrema                     *
  *     height2          Amplitude of second extrema                    *
  *     n1               Sample # of first extrema                      *
  *     n2               Sample # of second extrema                     *
  *     ncount           Number of samples of Z_in                      *
  *     Z_in             Displacement signal                            *
  *     dt               Time interval between samples (s)              *
  *     int_len          Window length (s)                              *
  *                                                                     *
  *  Return:             < 0 -> error.                                  *
  *                                                                     *
  ***********************************************************************/
  
int integrate( double *height1, double *height2, int *n1, int *n2, 
               long ncount, double Z_in[], double dt, double int_len )
{
   double  max1, max2, extrema[2][1000];
   double  test, zT[8000];
   int     n_max, n, i, array_top, n_start; 

/* Initialize some data */
   n_max = 0;
   n_start = (int) (2.5/dt);  /* ignore any extrema within 2.5 s of pick time */
   array_top = (int) (int_len / dt);
   if ( array_top > ncount ) array_top = (int) ncount;
   *n1 = 0;
   *n2 = 0;
   *height1 = 0.;
   *height2 = 0.;
     
/* Integrate displacement */
   zT[0] = 0.;
   for ( n=1; n<array_top; n++ )
      zT[n] = zT[n-1] + 0.5*dt*(Z_in[n]+Z_in[n-1]);
  
/* Find all local extrema in the data set */
   for ( n=n_start; n<(array_top-1); n++)    /* changed 3-14-04 */
/*   for ( n=1; n<(array_top-1); n++ )     */
   {
      if ( zT[n] > zT[n-1] && zT[n] > zT[n+1] && zT[n] > 0.0 )
      {
         extrema[0][n_max] = zT[n] * zT[n];
         extrema[1][n_max] = (float) n;
         n_max += 1;
      }
      else if ( zT[n] < zT[n-1] && zT[n] < zT[n+1] && zT[n] < 0.0 )
      {
         extrema[0][n_max] = zT[n] * zT[n];
         extrema[1][n_max] = (float) n;
         n_max += 1;
      }
      if ( n_max > 999 )
      {
         logit( "", "Found too many extrema in integrate! %ld \n", n_max);
         return( -1 );
      }
   }

/* Consider two special cases first */
   if ( n_max < 1 )  
   {
      logit( "", "Found too few extrema in integrate! %ld \n", n_max );
      return( -1 );
   }
   else if ( n_max == 1 )
   {
      *n1 = (int) (.01 + extrema[1][0]);
      *height1 = zT[*n1];
      return( 1 );
   }

/* Sort through list and select the largest |extremum| */
   max1 = -1.e+10;
   max2 = -1.e+10;
   for ( n=0; n<n_max; n++ )
      if ( extrema[0][n] > max1 )
      {
         max1 = extrema[0][n];
         *n1 = (int) (.01 + extrema[1][n]);
         *height1 = zT[*n1];
      }

/* Now look for the next largest extremal location */
   for ( n=0; n<n_max; n++ )
   {
      i = (int) (.01 + extrema[1][n]);
      if ( extrema[0][n] > max2 && i != *n1 )
      {
         test = zT[i] / zT[*n1];
         if ( test < 0.0 ) /* Accept second maximum if the sign of zT has changed */
         {
            max2 = extrema[0][n];
            *n2 = i;
         }
      }
   }
   if ( *n2 > 0 )
   {
      *height2 = zT[*n2];
      return ( 1 );
   }                                            
 
/* nmakereturn (1); */
   for ( n=0; n<n_max; n++ ) 
   {
      i = (int) (0.01 + extrema[1][n]);
      if ( extrema[0][n] > max2 && i != *n1 )
      {
         test = zT[i] / zT[*n1];
         if (test > 0.2)   /* Accept second maximum if it's significant relative to the first */
         {
            max2 = extrema[0][n];
            *n2 = i;
         }
      }
   }
   *height2 = zT[*n2];
  
   if ( *n1 > *n2 )
   {
      *height1 = zT[*n2];
      *n1 = *n2;
   }
   *height2 = 0.0;
   *n2 = 0;

   return( 1 );
}

      /******************************************************************
       *                         LoadBVals()                            *
       *                                                                *
       * This function loads Richter B-values from file specified into  *
       * an integer array.                                              *
       *                                                                *
       *  Arguments:                                                    *
       *   pszBValFile      File containing Richter mb b-values         *
       *                                                                *
       *  Return:                                                       *
       *   -1 if problem, 0 if read is ok                               *
       ******************************************************************/
	   
int LoadBVals( char *pszBValFile )
{
   FILE    *hFile;        /* B-value file handle */
   int     i;

/* If file can't be opened, return -1 */
   if ( (hFile = fopen( pszBValFile, "r" )) == NULL )  
   {
      logit( "t", "B-value file not opened - %s\n", pszBValFile );
      return -1;
   }

/* Get the b-values */
   for ( i=0; i<2500; i++ )
      if ( fscanf( hFile, "%2d", &iBVal[i]) == EOF )  /* File not complete */
      {
         fclose( hFile );
         logit( "t", "B-value file too short; stop at %ld\n", i );
         return -1;
      }
   fclose( hFile );
   return 0;
}                

 /***********************************************************************
  *                       MbMlCtsFromGM()                               *
  *                                                                     *
  *      Convert amplitude from ground motion to counts.  This is       *
  *      inverse routine from MbMlGroundMotion below. See that function *
  *      for further information.                                       *
  *                                                                     *
  *  Arguments:                                                         *
  *     pszChannel       Channel                                        *
  *     dSens            Station sensitivity                            *
  *     lPer             Cycle full period (seconds*10)                 *
  *     dAmp             Ground motion in nm                            *
  *                                                                     *
  *  Return:             Converted ground motion in counts              *
  *                                                                     *
  ***********************************************************************/
  
long MbMlCtsFromGM( char *pszChannel, double dSens, long lPer, double dAmp )
{
   double dResponse;             /* Reseponse factor to apply */   
   double dSPResp[] = {0.4,2.66,3.58,3.02,2.61,2.05,1.75,1.46,1.2,1.0,
        .8,.64,.53,.43,.36,.31,.27,.21,.19,.16,.13,.11,.1,.09,
        .08,.072,.063,.056,.048,.046,.043,.039,.036,.032,.030,
        .027,.025,.0225,.021,.020 };  /* WC/ATWC Short period, hi-gain resp. */
   double dSPFResp[] = {0.0833,0.7420,0.9924,1.0076,1.0076,1.0076,1.0076,1.0076,
        1.0076,1.0076,1.0038,1.0000,1.0000,1.0000,0.9621,0.9242,0.8864,0.8409,
        0.7803,0.7121,0.6515,0.5833,0.5227,0.4621,0.4167,0.3712,0.3333,0.2954,
        0.2727,0.2424,0.2200,0.1970,0.1818,0.1667,0.1515,0.1363,0.1250,0.1136,
        0.1023,0.0909};	              /* In-house (5Hz-2.0s) filter response */
   double dSPLResp[] = {.08,.18,.40,.70,1.01,1.26,1.33,1.3,1.16,1.0,.88,
        .76,.64,.52,.39,.35,.31,.26,.21,.16,.14,.125,.112,.10,.091,
        .082,.073,.064,.055,.047};    /* Short period low-gain resp. */
   long   lAmp;                  /* Ground motion in counts */		

/* Get out of here if sensitivity not known */
   if ( dSens == 0.) return (0);

/* Check period */
   if ( lPer < 1 || lPer > 40 ) return (0);

/* Determine which response curve to use. First option is WC&ATWC high
   and mid-gain short period response */
   if ( !strcmp (pszChannel, "SHZ") || !strcmp (pszChannel, "SMZ") )	
      dResponse = dSPResp[lPer-1];
	  
/* Next possibility is WC&ATWC low-gain, short-period response */
   else if ( !strcmp (pszChannel, "SLZ") )	
      dResponse = dSPLResp[lPer-1];
	  
/* Broadband, so assume flat response (except for SP filter) */
   else if ( !strncmp (pszChannel, "BH", 2) ||
             !strncmp (pszChannel, "HH", 2) ||
             !strncmp (pszChannel, "EH", 2) )	
      dResponse = dSPFResp[lPer-1];
	
/* Otherwise, assume short period filter reponse */	
   else	
      dResponse = dSPFResp[lPer-1];
	  
/* Convert seismometer amplitude to ground displacement in nanometers*/
   if ( dSens < 1.E7 )   /* Assume SP sensitivity */
      lAmp = (long) ((dAmp*dSens*dResponse) / 1000.0);
   else   /* Assume BB sensitivity (2PIf to convert velocity to displacement) */
      lAmp = (long) ((dAmp*2.*PI*dSens*dResponse*1./((double)lPer/10.0)) / 1.E9);
   return( lAmp );	
}

 /***********************************************************************
  *                       MbMlGroundMotion()                            *
  *                                                                     *
  *      Convert amplitude from counts to actual ground motion.  The    *
  *      counts are proportional to ground velocity and must be changed *
  *      to displacement.  Two types of sensitivities can be given in   *
  *      the station file.  The first is for digital broadband stations.*
  *      These sensitivities are given in counts/m/s.  Since the        *
  *      broadband response is flat over the frequency range of interest*
  *      in Mb/Ml computations, we can just use this one sensitivity    *
  *      for all periods.  We still have to account for the short period*
  *      filter applied to the data during the p picking.  This response*
  *      is accounted for by application of dSPFResp.                   *
  *                                                                     *
  *      The second type of sensitivity is the standard helicorder gain *
  *      of (e.g.) about 1K.  This type of gain is used when the input  *
  *      signal is from a wc/atwc analog station.  The real-time analog *
  *      gains are set by assuming each computer unit is 1mm.  Then the *
  *      calibration pulses are used to determine the factor which the  *
  *      signal is multiplied by.  The gain is given in station file in *
  *      K.  The response for this type of station is accounted for by  *
  *      application of dSPResp.  Here, we assume that no short period  *
  *      filtering is applied to the signal and that the response of the*
  *      seismometer is the normal wc/atwc short period response.       *
  *                                                                     *
  *  Arguments:                                                         *
  *     pszChannel       Channel                                        *
  *     dSens            Station sensitivity                            *
  *     lPer             Cycle full period (seconds*10)                 *
  *     lAmp             Amplitude in counts                            *
  *                                                                     *
  *  Return:             Converted ground motion in nm                  *
  *                                                                     *
  ***********************************************************************/
  
double MbMlGroundMotion( char *pszChannel, double dSens, long lPer, long lAmp )
{
   double dAmp;                  /* Ground motion in nanometers */
   double dResponse;             /* Reseponse factor to apply */   
   double dSPResp[] = {0.4,2.66,3.58,3.02,2.61,2.05,1.75,1.46,1.2,1.0,
        .8,.64,.53,.43,.36,.31,.27,.21,.19,.16,.13,.11,.1,.09,
        .08,.072,.063,.056,.048,.046,.043,.039,.036,.032,.030,
        .027,.025,.0225,.021,.020 };  /* WC/ATWC Short period, hi-gain resp. */
   double dSPFResp[] = {0.0833,0.7420,0.9924,1.0076,1.0076,1.0076,1.0076,1.0076,
        1.0076,1.0076,1.0038,1.0000,1.0000,1.0000,0.9621,0.9242,0.8864,0.8409,
        0.7803,0.7121,0.6515,0.5833,0.5227,0.4621,0.4167,0.3712,0.3333,0.2954,
        0.2727,0.2424,0.2200,0.1970,0.1818,0.1667,0.1515,0.1363,0.1250,0.1136,
        0.1023,0.0909};	              /* In-house (5Hz-2.0s) filter response */
   double dSPLResp[] = {.08,.18,.40,.70,1.01,1.26,1.33,1.3,1.16,1.0,.88,
        .76,.64,.52,.39,.35,.31,.26,.21,.16,.14,.125,.112,.10,.091,
        .082,.073,.064,.055,.047};    /* Short period low-gain resp. */
		

/* Get out of here if sensitivity not known */
   if ( dSens == 0.) return (0.);

/* Check period */
   if ( lPer < 1 || lPer > 40 ) return (0.);

/* Determine which response curve to use. First option is WC&ATWC high
   and mid-gain short period response */
   if ( !strcmp (pszChannel, "SHZ") || !strcmp (pszChannel, "SMZ") )	
      dResponse = dSPResp[lPer-1];
	  
/* Next possibility is WC&ATWC low-gain, short-period response */
   else if ( !strcmp (pszChannel, "SLZ") )	
      dResponse = dSPLResp[lPer-1];
	  
/* Broadband, so assume flat response (except for SP filter) */
   else if ( !strncmp (pszChannel, "BH", 2) ||
             !strncmp (pszChannel, "HH", 2) ||
             !strncmp (pszChannel, "EH", 2) )	
      dResponse = dSPFResp[lPer-1];
	
/* Otherwise, assume short period filter reponse */	
   else	
      dResponse = dSPFResp[lPer-1];
	  
/* Convert seismometer amplitude to ground displacement in nanometers*/
   if ( dSens < 1.E7 )   /* Assume SP sensitivity */
      dAmp = ((double) lAmp*1000.0) / (dSens*dResponse);
   else   /* Assume BB sensitivity (2PIf to convert velocity to displacement) */
      dAmp = ((double) lAmp*1.e9) / (2.*PI*dSens*dResponse*
             (1./((double)lPer/10.0)));
   return( dAmp );	
}

 /***********************************************************************
  *                         MsCtsFromGM()                               *
  *                                                                     *
  *      Convert amplitude from ground motion to counts.  This is       *
  *      inverse routine from MsGroundMotion below. See that function   *
  *      for further information.                                       *
  *                                                                     *
  *  Arguments:                                                         *
  *     pszChannel       Channel                                        *
  *     dSens            Station sensitivity                            *
  *     lPer             Cycle full period (seconds)                    *
  *     dAmp             Ground motion in um                            *
  *                                                                     *
  *  Return:             Converted ground motion in counts              *
  *                                                                     *
  ***********************************************************************/
  
long MsCtsFromGM( char *pszChannel, double dSens, long lPer, double dAmp )
{
   double dResponse;             /* Reseponse factor to apply */   
   double dLPResp[] = {.18,.31,.42,.52,.64,.75,.86,.91,1.1,1.13,1.15,1.17,
        1.18,1.19,1.2,1.16,1.12,1.08,1.04,1.0,.95,.90,.85,.80,.75,
        .71,.67,.63,.59,.55 };   /* WC&ATWC Long period seis. response */
/* Response of the basic long period filter (14s-28s) used in filters.c */
   double dLPFResp[] = {.001,.001,.001,.001,.001,.013,.023,.033,0.082,0.105,
        0.197,0.303,0.513,0.724,0.855,1.000,1.013,1.026,1.026,1.026,1.020,
        1.013,1.020,1.026,0.974,0.921,0.816,0.724,0.605,0.500};
   long   lAmp;                  /* Ground motion in counts */		

/* Get out of here if sensitivity not known */
   if ( dSens == 0.) return (0);

/* Check period */
   if ( lPer < 1 || lPer > 30 ) return (0);

/* Determine which response curve to use. First option is WC&ATWC long
   period response. */
   if ( !strcmp( pszChannel, "LHZ" ) || !strcmp( pszChannel, "LLZ" ) )	
      dResponse = dLPResp[lPer-1];
	  
/* Otherwise, assume broadband and flat response (except for LP filter) */
   else dResponse = dLPFResp[lPer-1];
	  
/* Convert seismometer amplitude to ground displacement in micrometers*/
   if ( dSens < 1.E7 )        /* Assume LP sensitivity */
      lAmp = (long) (dAmp*dSens*dResponse);
   else   /* Assume BB sensitivity (2PIf to convert velocity to displacement) */
      lAmp = (long) ((dAmp*2.*PI*dSens*dResponse*(1./(double) lPer)) / 1.e6);
   return( lAmp );	
}

 /***********************************************************************
  *                        MsGroundMotion()                             *
  *                                                                     *
  *      Convert amplitude from counts to actual ground motion.  The    *
  *      counts are proportional to ground velocity and must be changed *
  *      to displacement.  Two types of sensitivities can be given in   *
  *      the station file.  The first is for digital broadband stations.*
  *      These sensitivities are given in counts/m/s.  Since the        *
  *      broadband response is flat over the frequency range of interest*
  *      in Ms computations, we can just use this one sensitivity       *
  *      for all periods.  We still have to account for the long period * 
  *      filter applied to the data during the p picking.  This response*
  *      is accounted for by application of dLPFResp.                   *
  *                                                                     *
  *      The second type of sensitivity is the standard helicorder gain *
  *      of (e.g.) about 1K.  This type of gain is used when the input  *
  *      signal is from a wc/atwc analog station.  The real-time analog *
  *      gains are set by assuming each computer unit is 1mm.  Then the *
  *      calibration pulses are used to determine the factor which the  *
  *      signal is multiplied by.  The gain is given in station file in *
  *      K.  The response for this type of station is accounted for by  *
  *      application of dLPResp.  Here, we assume that no long period   *
  *      filtering is applied to the signal and that the response of the*
  *      seismometer is the normal wc/atwc long period response.        *
  *                                                                     *
  *  Arguments:                                                         *
  *     pszChannel       Channel                                        *
  *     dSens            Station sensitivity                            *
  *     lPer             Cycle full period (seconds)                    *
  *     lAmp             Amplitude in counts                            *
  *                                                                     *
  *  Return:             Converted ground motion in um                  *
  *                                                                     *
  ***********************************************************************/
  
double MsGroundMotion( char *pszChannel, double dSens, long lPer, long lAmp )
{
   double dAmp;                  /* Ground motion in nanometers */
   double dResponse;             /* Reseponse factor to apply */   
   double dLPResp[] = {.18,.31,.42,.52,.64,.75,.86,.91,1.1,1.13,1.15,1.17,
        1.18,1.19,1.2,1.16,1.12,1.08,1.04,1.0,.95,.90,.85,.80,.75,
        .71,.67,.63,.59,.55 };   /* WC&ATWC Long period seis. response */
/* Response of the basic long period filter (14s-28s) used in filters.c */
   double dLPFResp[] = {.001,.001,.001,.001,.001,.013,.023,.033,0.082,0.105,
        0.197,0.303,0.513,0.724,0.855,1.000,1.013,1.026,1.026,1.026,1.020,
        1.013,1.020,1.026,0.974,0.921,0.816,0.724,0.605,0.500};

/* Get out of here if sensitivity not known */
   if ( dSens == 0.) return (0.);

/* Check period */
   if ( lPer < 1 || lPer > 30 ) return (0.);

/* Determine which response curve to use. First option is WC&ATWC long
   period response. */
   if ( !strcmp( pszChannel, "LHZ" ) || !strcmp( pszChannel, "LLZ" ) )	
      dResponse = dLPResp[lPer-1];
	  
/* Otherwise, assume broadband and flat response (except for LP filter) */
   else dResponse = dLPFResp[lPer-1];
	  
/* Convert seismometer amplitude to ground displacement in micrometers*/
   if ( dSens < 1.E7 )        /* Assume LP sensitivity */
      dAmp = ((double) lAmp) / (dSens*dResponse);
   else                       /* Assume BB sensitivity (2PIf to convert */
                              /*  velocity to displacement) */
      dAmp = ((double) lAmp*1.e6) / (2.*PI*dSens*dResponse*(1./(double) lPer));
   return( dAmp );	
}

 /***********************************************************************
  *                         Mwp_adjustment()                            *
  *                                                                     *
  * This function corrects the computed Mwp based on epicentral         *
  * distance.  The correction values are based on Paul Huang's study of *
  * Mwp versus Global CMT over all distance ranges.                     *
  *                                                                     *
  *  Arguments:                                                         *
  *     dDistance        Station epicentral distance in degrees         *
  *                                                                     *
  *  Return:             Distance correction value to subtract from Mwp *
  *                                                                     *
  ***********************************************************************/
double Mwp_adjustment( double dDistance )
{
   double  dMwp_distance_correction[50] = {
    0.280154895, 0.248159091, 0.192827333, 0.113355523, 0.134518163,
    0.142274094, 0.141015269, 0.225545292, 0.270142832, 0.292807679, 
    0.279098314, 0.083464936, 0.032658602, 0.017405582, 0.03411125, 
   -0.01300572, -0.087751188,-0.105951149,-0.159169491,-0.109741881,
   -0.040873702, -0.127091897,-0.0291207,-0.051879539,-0.063962226,
    0.000433825, 0.020766625,-0.036926385,-0.042950165,-0.006198259,
    0.013929801, 0.007052744,0.087026743,-0.007465525,-0.021245992,
    0.039889117, 0.052830981,-0.005707771,-0.022900097,-0.009290172,
   -0.028246945, -0.032688366,-0.020939191,0.096496123,-0.025355755,
   -0.04532547, -0.048598167,-0.105345188,-0.309972643,-0.299542571 };
   int     iIndex;         /* Index of above array based on distance */

   iIndex = (int)(dDistance/2);

   if ( dDistance <= 0.0 ) return 0.0;

   if ( iIndex < 0 || iIndex >= 50 ) return 0.0;
  
   return dMwp_distance_correction[iIndex];
}

 /***********************************************************************
  *                         wavelet_decomp()                            *
  *                                                                     *
  *      Discrete wavelet decomposition of displacement signal.         *
  *                                                                     *
  *  Arguments:                                                         *
  *     int_len          Maximum signal length in seconds               *
  *     z_in             Displacement signal                            *
  *     ncount           Number of samples of z_in                      *
  *     dt               Time interval between samples (s)              *
  *                                                                     *
  *  Return:             Window length in seconds.                      *
  *                                                                     *
  ***********************************************************************/

double wavelet_decomp( double int_len, double Z_in[ ], long ncount, double dt )						
						
{
   int     i, k, l, m, n, ntop, n_remaining;
   static  double  x[MAXMWPARRAY], x_next[MAXMWPARRAY], w[MAXMWPARRAY];
   int     ic, index, f_index = 0, mult = 1, division_counter;
   double  y, z, smooth[4], nonsmooth[4];
   double  smooth2[6], nonsmooth2[6];
   double  avg_count = 0., avg = 0., two = 2.;
   double  average[100], xform[2][100], error[2][500];
   double  xmin, tau, a, fc, wi;
   double  sum0, sum1, sum2, sum3;
   double  z1 = 0., z2 = 0., z3 = 0.;
   static  double  decomp_mag[MAXMWPARRAY];
   double  weight[100];
    
/* Avoid any trailing zeroes */
   ncount -= 2;
   ntop = (int) (int_len / dt);
   if (ntop > (ncount-2)) ntop = ncount - 2;
   
   for ( n=0; n<100; n++ )
     weight[n] = 1.;
   for ( n=1; n<20; n++ )
     weight[n] = 2. * weight[n-1];
  
/* initialize wavelet arrays */
   for ( n=0; n<ntop; n++ )
   {
     w[n] = 0.0;
     x[n] = Z_in[n];
     x_next[n] = Z_in[n];
   }
  
/* Daubechies order 4 wavelet coeffs */
   smooth[0] = (1. + sqrt(3.0)) / (4. * sqrt(2.));
   smooth[1] = (3. + sqrt(3.0)) / (4. * sqrt(2.));
   smooth[2] = (3. - sqrt(3.0)) / (4. * sqrt(2.));
   smooth[3] = (1. - sqrt(3.0)) / (4. * sqrt(2.));
   
/* Daubechies order 6 wavelet coeffs */
   smooth2[0] = .332671;
   smooth2[1] = .806891;
   smooth2[2] = .459877;
   smooth2[3] = -.135011;
   smooth2[4] = -.085441;
   smooth2[5] = .035226;
  
   z = 1.;
   for (n = 0; n < 6; n++)
   {
      nonsmooth2[n] = z * smooth2[5 - n];
      if (n < 4) nonsmooth[n] = z * smooth[3 - n];
      z *= -1.;
   }

/* Start Wavelet decomposition */
   n_remaining = ntop;
   m = 0;
   division_counter = 0;
   while ( n_remaining >= 5 )
   {
      k = 0;
/* filter data into smooth and non-smooth vectors */
      for ( n=0; n<n_remaining; n+=2 )
      {
         if ( (n + 5) < n_remaining )
         {
            x_next[k] = 0.0;
            w[m] = 0.0;
            for ( l=0; l<6; l++ )
            {
               x_next[k] += smooth2[l] * x[n + l];
               w[m] += nonsmooth2[l] * x[n + l];
            }
            avg += (w[m] * w[m]);
            decomp_mag[k] = w[m] * w[m];
            avg_count += 1.;
		
            m += 1;
            k += 1;
         }                                        
         else
         {
            x_next[k] = 0.0;
            w[m] = 0.0;
            for ( l=0; l<6; l++ )
            {
               if ( (n + l)<n_remaining ) 
               {
                  x_next[k] += smooth2[l] * x[n + l];
                  w[m] += nonsmooth2[l] * x[n + l];
               }
               else
               {
                  x_next[k] += smooth2[l] * x[n + l - n_remaining];
                  w[m] += nonsmooth2[l] * x[n + l - n_remaining];
               }		  
            }
            avg += (w[m] * w[m]);
            decomp_mag[k] = w[m] * w[m];
            avg_count += 1.;		
            m += 1;
            k += 1;
         }
      }
	
      if ( avg_count > 0.5 )
         average[division_counter] = sqrt(avg / avg_count);
      else
         average[division_counter] = 0.0;
	 
/* Increase time scale by factor of 2 */
      n_remaining  = k;
      avg = 0.;
      avg_count = 0.;
      division_counter += 1;
      for ( n=0; n<k; n++ ) x[n] = x_next[n];
   }
   
   if (average[division_counter-1] < 1.e-50) division_counter -= 1;

/* Write out some data */
   for ( n=0; n<ntop; n++ )
      if ( w[n] < 0.0 ) w[n] = -w[n];

/* Find min frequency */
   y = 1. / (two * dt);
   for ( n=1; n<division_counter; n++ )
   y /= two;

/* Write out spectral data */
   for ( n=(division_counter-1); n>=0; n-- )
   {
      xform[0][division_counter-n-1] = y;
      xform[1][n] = average[division_counter-n-1];
      y *= two;
   }

/* Evaluate test function by first creating an error table */
   for ( index=0; index<500; index++ )
   {
/* Get fc with the minimum error and corresponding ic */
      fc = .005 + .001*(double) index;
      ic = 0;
      while ( xform[0][ic]<fc && ic<division_counter ) ic += 1;

/* Eval error for this fc */
      sum0 = 0.;
      sum1 = 0.;
      sum2 = 0.;
      sum3 = 0.;
      if ( ic>0 )
         for (i = 0; i < ic; i++)
         {
            sum0 += weight[i];
            sum1 += weight[i] * xform[1][i];
         }
      for ( i=ic; i<(division_counter-1); i++ )
      {
         wi = xform[0][i];
         sum2 += weight[i] * xform[1][i] / wi;
         sum3 += weight[i] / (wi * wi);
      }
      a = (sum1 + fc*sum2) / (sum0 + fc*fc*sum3);
      if (a < 0.0)
         error[1][index] = 2.e+20;
      else if (fc <= xform[0][0])
         error[1][index] = 2.e+20; 
      else
      {
         z = 0.;
         if ( ic>0 )
            for ( i=0; i<ic; i++ )
               z += weight[i] * ((xform[1][i] - a) * (xform[1][i] - a));
         for ( i=ic; i<(division_counter-1); i++ )
         {
            wi = xform[0][i];
            y = xform[1][i] - a*fc/wi;
            z += weight[i] * (y*y);
         }
         error[0][index] = a;
         error[1][index] = z;
      }
   }

/* Find smallest error */
   xmin = 1.e+50;
   for ( i=0; i<500; i++ )
      if ( error[1][i] < xmin )
      {
         ic = i;
         xmin = error[1][i];
      }

   fc = .005 + .001 * (double) ic;
   a = error[0][ic];

   for ( i=0; i<division_counter; i++ )
   {
      wi = xform[0][i];
      if ( wi < fc )
         z = a;
      else
         z = a * fc * fc / (wi * wi); 
/*         z = a * fc / wi;    ANALYZE version */
   }
   /* revert tau multiplier to 5.0 on 2-18-04 since weighting 
        opened the window */
   tau = 2.5 / fc; /* multiplier was 7.5, then 9, 8 looks good    on 2-4-04 */
   // testing coeff 2.5 instead of 5.0  3-10-04
   if ( tau < 5. ) tau = 5.;
   else if ( tau > 200. ) tau = 200.;
   if ( tau > (dt * (double) ncount) ) tau = dt * (double) ncount;

/* Finish out subroutine */
   return( tau );
}

      /******************************************************************
       *                       ZeroMagnitudes()                         *
       *                                                                *
       * Initialize the magnitude variables of the P Pick structure.    *
       *                                                                *
       *  Arguments:                                                    *
       *   P                One of the P Buffer structures              *
       *   iPNum            Number of Ps in this structure              *
       *                                                                *
       ******************************************************************/
       
void ZeroMagnitudes( PPICK *P, int iNum )
{
   int     i;

   for ( i=0; i<iNum; i++ )
   {
      P[i].dMbMag  = 0.;
      P[i].dMlMag  = 0.;
      P[i].dMSMag  = 0.;
      P[i].dMwpMag = 0.;
      P[i].dMwMag  = 0.;
      P[i].iMbClip = 0;
      P[i].iMlClip = 0;
      P[i].iMSClip = 0;
      P[i].iMwClip = 0;
   }
}
