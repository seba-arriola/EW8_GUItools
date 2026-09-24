/************************************************************************
  * GET_PICK.C                                                           *
  * *
  * This is a group of functions which provide tools for                 *
  * making P-picks and determining magnitudes from the P data.           *
  * *
  * Made into earthworm module 3/2001.                                   *
  * *
  ************************************************************************/
  
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <earthworm.h>
#include <transport.h>
#include "earlybirdlib.h"

 /***********************************************************************
  * CheckForAlarm()                         *
  * *
  * Check for multi station regional alarm.  This logic was patterned  *
  * after a PTWC program which basically does the same thing. Send     *
  * alarm if necessary.                                                *
  * *
  * May, 2008: Split out to libsrc from pick_wcatwc.                   *
  * May, 2005: Add timeout feature to alarm (i.e., if pick is over x   *
  * minutes old, ignore it.                                 *
  * *
  * Arguments:                                                         *
  * Sta              Station data structure                         *
  * pAS              ALARMSTRUCT structure                          *
  * iNumReg          Number of Alarm regions                        *
  * ucMyModID        Calling module ID                              *
  * siAlarmRegion    Calling module Alarm Ring                      *
  * ucEWHTypeAlarm   Earthworm alarm message number                 *
  * ucEWHMyInstID    Earthworm Institute ID                         *
  * *
  ***********************************************************************/
void CheckForAlarm( STATION *Sta, ALARMSTRUCT pAS[], int iNumReg, 
                    unsigned char ucMyModID, SHM_INFO siAlarmRegion,
                    unsigned char ucEWHTypeAlarm, unsigned char ucEWHMyInstID )
{
   int   i, j, k;
   
   for ( i=0; i<iNumReg; i++ )
      for ( j=0; j<pAS[i].iNumStnInReg; j++ ) /* Is it an alarm station */
	     if ( !strcmp( pAS[i].szStation[j], Sta->szStation ) )
         {                                      /* Yes, it is */
            logit( "", "Alarm Stn found-%s in %s, SN=%lf\n", Sta->szStation, 
                   pAS[i].szRegionName, Sta->dPStrength );
            if ( Sta->dPStrength > pAS[i].dThresh )/* Is it strong enough? */
            {
               if ( (Sta->dTrigTime-Sta->dTimeCorrection)-pAS[i].dLastTime <
                  pAS[i].dMaxTime )                   /* Is it within time? */
               {  /* First, see if this station is already alarmed */
                  for ( k=0; k<pAS[i].iNumPicksCnt; k++ )
                     if ( !strcmp( Sta->szStation, pAS[i].szStnAlarm[k] ) )
                     {
                        logit( "", "%s already alarmed\n", Sta->szStation );
	                    goto LoopEnd;
                     }
                  strcpy( pAS[i].szStnAlarm[pAS[i].iNumPicksCnt], Sta->szStation );
                  pAS[i].iNumPicksCnt++;
                  logit( "", "%s has %ld alarm stations\n", pAS[i].szRegionName,
                         pAS[i].iNumPicksCnt );
                  if ( pAS[i].iNumPicksCnt >= pAS[i].iAlarmThresh )
                  {
                     ReportAlarm( Sta, ucMyModID, siAlarmRegion,
                      ucEWHTypeAlarm, ucEWHMyInstID, 2, pAS[i].szRegionName,
                      0 );
                     pAS[i].iNumPicksCnt = 0;
                     pAS[i].dLastTime = 0.;
                  }
               }
               else
               {
                  logit( "", "Reset alarm vars. in %s, %s\n", pAS[i].szRegionName,
                         Sta->szStation );
                  strcpy( pAS[i].szStnAlarm[0], Sta->szStation );
                  pAS[i].iNumPicksCnt = 1;
                  pAS[i].dLastTime = Sta->dTrigTime-Sta->dTimeCorrection;
               }
            }
         }
   LoopEnd:;
   return;
}

     /**************************************************************
      * CopyPBuf()                         *
      * *
      * Copy one PPICK structure to another.                       *
      * *
      * Arguments:                                                 *
      * PIn         Input PPICK structure                         *
      * POut        Output PPICK structure                        *
      * *
      **************************************************************/

void CopyPBuf( PPICK *PIn, PPICK *POut )
{
   strcpy (POut->szStation, PIn->szStation );
   strcpy (POut->szChannel, PIn->szChannel );
   strcpy (POut->szNetID,   PIn->szNetID );
   strcpy (POut->szPhase,   PIn->szPhase );
   POut->lPickIndex       = PIn->lPickIndex;
   POut->iBin             = PIn->iBin;
   POut->iUseMe           = PIn->iUseMe;
   POut->dPTime           = PIn->dPTime;
   POut->dExpectedPTime   = PIn->dExpectedPTime;
   POut->dPhaseTime       = PIn->dPhaseTime;
   POut->iHypoID          = PIn->iHypoID;
   POut->stPTime.wYear =         PIn->stPTime.wYear;
   POut->stPTime.wMonth =        PIn->stPTime.wMonth;
   POut->stPTime.wDayOfWeek =    PIn->stPTime.wDayOfWeek;
   POut->stPTime.wDay =          PIn->stPTime.wDay;
   POut->stPTime.wHour =         PIn->stPTime.wHour;
   POut->stPTime.wMinute =       PIn->stPTime.wMinute;
   POut->stPTime.wSecond =       PIn->stPTime.wSecond;
   POut->stPTime.wMilliseconds = PIn->stPTime.wMilliseconds;
   POut->cFirstMotion     = PIn->cFirstMotion;
   POut->dMbAmpGM         = PIn->dMbAmpGM;
   POut->dMbPer           = PIn->dMbPer;
   POut->dMbTime          = PIn->dMbTime;
   POut->dMbMag           = PIn->dMbMag;
   POut->iMbClip          = PIn->iMbClip;
   POut->dMlAmpGM         = PIn->dMlAmpGM;
   POut->dMlPer           = PIn->dMlPer;
   POut->dMlTime          = PIn->dMlTime;
   POut->dMlMag           = PIn->dMlMag;
   POut->iMlClip          = PIn->iMlClip;
   POut->dMSAmpGM         = PIn->dMSAmpGM;
   POut->dMSPer           = PIn->dMSPer;
   POut->dMSTime          = PIn->dMSTime;
   POut->dMSMag           = PIn->dMSMag;
   POut->iMSClip          = PIn->iMSClip;
   POut->dMwpIntDisp      = PIn->dMwpIntDisp;
   POut->dMwpTime         = PIn->dMwpTime;
   POut->dMwpMag          = PIn->dMwpMag;
   POut->dMwAmpGM         = PIn->dMwAmpGM;
   POut->dMwPer           = PIn->dMwPer;
   POut->dMwTime          = PIn->dMwTime;
   POut->dMwMag           = PIn->dMwMag;
   POut->iMwClip          = PIn->iMwClip;
   POut->dLat             = PIn->dLat;
   POut->dLon             = PIn->dLon;
   POut->dCoslat          = PIn->dCoslat;
   POut->dSinlat          = PIn->dSinlat;
   POut->dCoslon          = PIn->dCoslon;
   POut->dSinlon          = PIn->dSinlon;
   POut->dSens            = PIn->dSens;
   POut->dClipLevel       = PIn->dClipLevel;
   POut->dElevation       = PIn->dElevation;
   POut->dFreq            = PIn->dFreq;
   POut->dGainCalibration = PIn->dGainCalibration;
   POut->dPStrength       = PIn->dPStrength;
   POut->iStationType     = PIn->iStationType;
   POut->dRes             = PIn->dRes;
   POut->dDelta           = PIn->dDelta;
   POut->dAz              = PIn->dAz;
   POut->dFracDelta       = PIn->dFracDelta;
   POut->dSnooze          = PIn->dSnooze;
   POut->dCooze           = PIn->dCooze;
   POut->dTimeCor         = PIn->dTimeCor;
}

      /******************************************************************
       * InitP()                             *
       * *
       * This function initializes a PPICK structure.                  *
       * *
       * Arguments:                                                    *
       * pP                PPICK structure                            *
       * *
       ******************************************************************/
	   
void InitP( PPICK *pP )
{
   strcpy (pP->szStation, "     ");
   strcpy (pP->szChannel, "     ");
   strcpy (pP->szNetID, "  ");
   strcpy (pP->szPhase, "     ");
   pP->dLat = 0.;
   pP->dLon = 0.;
   pP->dElevation = 0.;
   pP->dSinlat = 0.;
   pP->dCoslat = 0.;
   pP->dSinlon = 0.;
   pP->dCoslon = 0.;
   pP->iStationType = 100;
   pP->lPickIndex = -1;
   pP->dPTime = 0.;
   pP->dSens = -1.;
   pP->dPhaseTime = 0.;
   pP->dExpectedPTime = 0.;
   pP->stPTime.wYear =      0;
   pP->stPTime.wMonth =     0;
   pP->stPTime.wDayOfWeek = 0;
   pP->stPTime.wDay =       0;
   pP->stPTime.wHour =      0;
   pP->stPTime.wMinute =    0;
   pP->stPTime.wSecond =    0;
   pP->stPTime.wMilliseconds = 0;   
   pP->dClipLevel = 0.;
   pP->dGainCalibration = -1.;
   pP->dTimeCor = 0.;
   pP->cFirstMotion = '?';
   pP->dMbAmpGM = 0.;
   pP->dMbPer = 0.;
   pP->dMbMag = 0.;
   pP->dMbTime = 0.;
   pP->iMbClip = 0;
   pP->dMlAmpGM = 0.;
   pP->dMlPer = 0.;
   pP->dMlTime = 0.;
   pP->dMlMag = 0.;
   pP->iMlClip = 0;
   pP->dMSAmpGM = 0.;
   pP->dMSPer = 0.;
   pP->dMSTime = 0.;
   pP->dMSMag = 0.;
   pP->iMSClip = 0;
   pP->dMwpIntDisp = 0.;
   pP->dMwpTime = 0.;
   pP->dMwpMag = 0.;
   pP->dMwAmpGM = 0.;
   pP->dMwPer = 0.;
   pP->dMwMag = 0.;
   pP->dMwTime = 0.;
   pP->iMwClip = 0;
   pP->dRes = 0.;
   pP->dDelta = 0.;
   pP->dAz = 0.;
   pP->dFracDelta = 0.;
   pP->dSnooze = 0.;
   pP->dCooze = 0.;
   pP->dPStrength = 0.;
   pP->dFreq = 0.;
   pP->iUseMe = 1;
   pP->iBin = 0;
   pP->iHypoID = -1;
}

  /******************************************************************
   * MovingAvg()                         *
   * *
   * Determine and update moving averages of absolute signal value *
   * (called LTA here) and differential function (called MDF). Peak*
   * noise (unfiltered) is also noted here for each LTASamps.      *
   * NOTE: Incoming data must be short-period or filtered.         *
   * *
   * March, 2004: Changed background noise computation to RMS for  *
   * Mwp computations                                 *
   * *
   * Arguments:                                                    *
   * LongSample  One waveform data sample                        *
   * Sta         Station data array                              *
   * lLTASamps   # of samples per moving avg block               *
   * RawSample   Un-filtered waveform data sample                *
   * lNumConsec  Maximum samples to add up MDF (based on MinFreq)*
   * *
   ******************************************************************/

void MovingAvg( long LongSample, STATION *Sta, long lLTASamps,  
                long RawSample, long lNumConsec )
{
   static long    lHigh, lLow; /* Peak/trough signal values for each interval */

/* Copy new sample to structure and compute DF */
   Sta->lSampNew = LongSample;
   Sta->lSampRaw = RawSample;
   Sta->lMDFNew = Sta->lSampNew - Sta->lSampOld;
   
/* Add last sample and MDF to running totals and noise levels */
   if ( Sta->lLTACtr < lLTASamps )
   {
      if ( Sta->iPickStatus == 1 )
      {
         Sta->dSumLDC    += (double) Sta->lSampOld;
         Sta->dSumLDCRaw += (double) Sta->lSampRaw;
         Sta->dSumLTA    += (double) (labs( Sta->lSampOld ));
         Sta->dSumRawNoise += (((double)Sta->lSampRaw/Sta->dSens) *
                               ((double)Sta->lSampRaw/Sta->dSens));
      }
      else
      {
         Sta->dSumLDC    += ((double) (Sta->lSampOld) - Sta->dAveLDC);
         Sta->dSumLDCRaw += ((double) (Sta->lSampRaw) - Sta->dAveLDCRaw);
         Sta->dSumLTA    += (fabs( (double) Sta->lSampOld-Sta->dAveLDC ) -
                             Sta->dAveLTA);
         Sta->dSumRawNoise += (((double) Sta->lSampRaw-Sta->dAveLDCRaw)/Sta->dSens *
                               ((double) Sta->lSampRaw-Sta->dAveLDCRaw)/Sta->dSens);
      }
      if ( Sta->lSampRaw > lHigh ) lHigh = Sta->lSampRaw;
      if ( Sta->lSampRaw < lLow )  lLow  = Sta->lSampRaw;
   }
   else               /* Compute new LTAs and noise level */
   {
      if ( Sta->iPickStatus == 1 )
      {
         Sta->dAveMDF    = Sta->dSumMDF / (double) Sta->lCycCntLTA;
         Sta->dAveLDC    = Sta->dSumLDC / (double) (lLTASamps-1);
         Sta->dAveLDCRaw = Sta->dSumLDCRaw / (double) (lLTASamps-1);
         Sta->dAveLTA    = Sta->dSumLTA / (double) (lLTASamps-1);
         Sta->dAveRawNoise = sqrt( Sta->dSumRawNoise /
                                  (double) (lLTASamps-1) );
         Sta->iPickStatus = 2;
      }
      else
      {
         Sta->dAveMDF    += (0.5*Sta->dSumMDF / (double) Sta->lCycCntLTA);
         Sta->dAveLDC    += (0.5*Sta->dSumLDC / (double) (lLTASamps));
         Sta->dAveLDCRaw += (0.5*Sta->dSumLDCRaw / (double) (lLTASamps));
         Sta->dAveLTA    += (0.5*Sta->dSumLTA / (double) (lLTASamps));
         Sta->dAveRawNoise = 0.9*Sta->dAveRawNoise +
                             0.1*sqrt( Sta->dSumRawNoise /
                                       (double) (lLTASamps) );
      }
      Sta->lRawNoise = lHigh - lLow;
      if ( Sta->lRawNoise == 0 ) Sta->lRawNoise = 1;
	  
/* Reset summation variables and compute thresholds */	  
      lHigh = -10000000;
      lLow  =  10000000;
      Sta->dSumMDF = 0.;
      Sta->dSumLDC = 0.;
      Sta->dSumLDCRaw = 0.;
      Sta->dSumRawNoise = 0.;
      Sta->dSumLTA = 0.;
      Sta->lLTACtr = 0;
      Sta->lCycCntLTA = 0;
      Sta->dMDFThresh = 0.5 * (double) Sta->iSignalToNoise * Sta->dAveMDF;
      Sta->dLTAThresh = 1.57 * (double) Sta->iSignalToNoise * Sta->dAveLTA;
   }   
   
/* Check for cycle changes (convert DF to MDF) */
   if ( (Sta->lMDFOld < 0 && Sta->lMDFNew < 0) ||
        (Sta->lMDFOld >= 0 && Sta->lMDFNew >= 0) )
   {
         /* No changes, continuing adding up MDF */
      Sta->lSampsPerCycLTA++;
      if ( Sta->lSampsPerCycLTA < lNumConsec )
         Sta->lMDFRunningLTA += Sta->lMDFNew;
      else
      {
         if ( Sta->iPickStatus == 1 )
            Sta->dSumMDF += (double) (labs( Sta->lMDFRunningLTA ));
         else
            Sta->dSumMDF += ((double) (labs( Sta->lMDFRunningLTA )) -
                             Sta->dAveMDF);
         Sta->lMDFRunningLTA = Sta->lMDFNew;
         Sta->lCycCntLTA++;
         Sta->lSampsPerCycLTA = 0;
      }
   }
   else  /* Cycle has changed sign, start anew */
   {
      if ( Sta->iPickStatus == 1 )
         Sta->dSumMDF += (double) (labs( Sta->lMDFRunningLTA ));
      else
         Sta->dSumMDF += ((double) (labs( Sta->lMDFRunningLTA )) -
                          Sta->dAveMDF);
      Sta->lMDFRunningLTA = Sta->lMDFNew;
      Sta->lCycCntLTA++;
      Sta->lSampsPerCycLTA = 0;
   }   
   
/* Update old with new value and increment averages counter (MDF updated
   elsewhere) */   
   Sta->lSampOld = Sta->lSampNew;
   Sta->lLTACtr++;           // LTA counter
}

 /***********************************************************************
  * PickV()                                 *
  * *
  * Evaluate one demultiplexed message with the Veith P-picker      *
  * *
  * The P-pick detection algorithm used here was developed by Veith in *
  * 1978 and is described in Technical Note 1/78, "Seismic Signal      *
  * Detection Algorithm" by Karl F. Veith, Teledyne GeoTech.           *
  * The signal must go through four processing stages before           *
  * a pick is declared.  The first stage is a simple test which looks  *
  * for higher than normal signal amplitudes.  Actually, it is not the *
  * amplitude that is tested but the accumulated difference between    *
  * samples (the MDF). This is compared to the background MDF every    *
  * sample.  The background MDF is computed with a moving averages     *
  * technique.  Every LTASECONDS the average MDF is computed and this  *
  * is averaged with the existing MDF to produce a new MDF.            *
  * *
  * The second phase of signal processing consists of two tests.  Test *
  * 1 checks that the MDF exceeds a trigger threshold for lNumConsec   *
  * samples after Phase 1 is passed twice, but before 3*lNumConsec     *
  * samples are processed. Test 2 states that the amplitude must exceed*
  * the signal-to-noise ratio times the LTA (*1.57) at some time during*
  * Phase 2.  If the Phase 1 trigger was not exceeded more than it was *
  * at any time during Phase 2, the phase fails.                       *
  * *
  * Phase 3 consists of three tests.  Test 1 requires MDF greater than *
  * the trigger threshold at least 6 times in opposing directions      *
  * (i.e. it must see three full cycles of signal).  Test 2 requires   *
  * the avg. frequency of the above oscillations to be greater than    *
  * FMINFREQ/2.  Test 3 requires the MDF to be above the trigger for at*
  * least half the time it takes to pass the above two tests.  If      *
  * these three Phases are passed, a P-pick is declared.               *
  * *
  * The last check was added in 2007.  If a pick is high frequency,    *
  * it also must be above a set signal-to-noise ratio.  If not, the    *
  * pick is rejected.  This helps prevent spurious noise picks.        *
  * *
  * At this point, the time that Phase 1 was passed is saved as the    *
  * event's P-time.  The picker continues to evaluate the signal for   *
  * magnitude data.  Mb, Mwp, and Ml's are computed here. After the    *
  * magnitude processing has finished, evaluation on this channel      *
  * ceases and all variables are reinitialized.                        *
  * *
  * The picker evaluates data at any sample rate. The P-picker works   *
  * best on the short period filtered data from broadband signal.  The *
  * broadband is used for Mwp processing.  This P-picker did not work  *
  * well when tried on broadband signal.                               *
  * *
  * Arguments:                                                         *
  * Sta              Pointer to station being processed             *
  * dStartTime       Start time (1/1/70 seconds) of the packet      *
  * iAlarmOn         1->Alarm function enabled, 0->Disabled         *
  * i2StnAlarmOn     1->multo station alarm on, 0->off              *
  * dAlarmTimeout    Time (sec) to re-start alarm after trig        *
  * dMinFreq         Minimum P frequency (hz) of interest           *
  * dLTASeconds      Moving average length of time (seconds)        *
  * iMwpSeconds      Max # seconds to evaluate P for Mwp            *
  * dMwpSigNoise     Auto-Mwp necessary signal-to-noise ratio       *
  * iLGSeconds       Seconds after P in which max LG can be         *
  * computed for Ml (excluding 1st MbCycles)       *
  * iMbCycles        # 1/2 cycles after P Mb can be computed        *
  * dSNLocal         S:N which must be exceeded for local P-picks   *
  * dMinFLoc         Frequency to identify potential local events   *
  * ucMyModID        Calling module ID                              *
  * siAlarmRegion    Calling module Alarm Ring                      *
  * siPRegion        Calling module P Ring                          *
  * ucEWHTypeAlarm   Earthworm alarm message number                 *
  * ucEWHTypePickTWC Earthworm pick message number                  *
  * ucEWHMyInstID    Earthworm Institute ID                         *
  * WaveRaw          Array of unfiltered signal                     *
  * WaveLong         Pointer to array of filtered data              *
  * pAS              ALARMSTRUCT array with regional alarm data     *
  * iNumReg          Number of regions in ALARMSTRUCT               *
  * iRT              1->called from pick_wcatwc; 0->not real-time   *
  * piSaveP          For Analyze; 1->Good pick; 0->No pick          *
  * *
  * May, 2008: Combined with pick software in Analyze.                 *
  * May, 2008: Removed sine wave cal discriminator.                    *
  * July, 2007: Added fourth phase which compares frequency and signal *
  * strength.                                              *
  * December, 2004: Added multi-station alarm and signal strength      *
  * determination.                                     *
  * *
  ***********************************************************************/

void PickV( STATION *Sta, double dStartTime, int iAlarmOn, int i2StnAlarmOn,
            double dAlarmTimeout, double dMinFreq, double dLTASeconds,
            int iMwpSeconds, double dMwpSigNoise, int iLGSeconds,
            int iMbCycles, double dSNLocal, double dMinFLoc, 
            unsigned char ucMyModID, SHM_INFO siAlarmRegion, SHM_INFO siPRegion,
            unsigned char ucEWHTypeAlarm, unsigned char ucEWHTypePickTWC,
            unsigned char ucEWHMyInstID, long WaveRaw [], long *WaveLong,
            ALARMSTRUCT pAS[], int iNumReg, int iRT, int *piSaveP )
{
   int     i, ii;
   long    lCnt;                /* Index counter for transfer of raw data 
                                   circular buffer to Mwp buffer */
   static  long lLTASamps;      /* # samps / moving avg block */
   long    lNumConsec;          /* Max. # samps. to evaluate in 1/2 cycle */
   long    lNumSamp;            /* Number of samples to evaluate */
   static  long lPickCounter=0; /* Pick Counter */
   time_t  now;                 /* Present 1/1/1970 time */
   PPICK   PBuf;                /* Temp buffer for compatibility with AutoMwp */
   PBuf.dDelta=0.0;
   *piSaveP = 0;

/* Compute number of samples */
   lNumSamp = (long) ((Sta->dDataEndTime-dStartTime)*Sta->dSampRate +
                       0.01) + 1;
/* Process data through alarm function if set in .sta file (and recent data) */
   time( &now );
   if ( iRT == 1 )
      if ( now-Sta->dDataEndTime < RECENT_ALARM_TIME )
         if ( iAlarmOn )
            if ( Sta->iAlarmStatus >= 1 )
               SeismicAlarm( Sta, lNumSamp, WaveLong, 1, ucMyModID, 
                siAlarmRegion, ucEWHTypeAlarm, ucEWHMyInstID, dAlarmTimeout,
                dStartTime );

/* Compute Maximum number of samples to allow per cycle */
   lNumConsec = (long) (Sta->dSampRate / (2.*dMinFreq) + 0.0001);

/* Compute number of samples in the moving average block */
   lLTASamps = (long) (Sta->dSampRate * dLTASeconds + 0.0001);

/* Loop over all samples in packet */
   for ( i=0; i<lNumSamp; i++ )
   {       
/* First, update averages */
      MovingAvg ( WaveLong[i], Sta, lLTASamps, WaveRaw[i], lNumConsec );

/* If Station past first initialization or just had pick, proceed with P-picker */
      if ( Sta->iPickStatus >= 2 )
      {
/* Fill raw data circular buffer (if Mwps are to be computed) */
         if ( Sta->dSens > 0.0 && Sta->iComputeMwp ) RawDataBuff( Sta );

/* Add to Mwp array if Phase 1 passed */  
         if ( Sta->lMwpCtr > 0 &&
              Sta->lMwpCtr <= (long) (Sta->dSampRate*iMwpSeconds+0.001) )
            Sta->lMwpCtr++;
         if ( Sta->dSens > 0.0 && Sta->iComputeMwp && Sta->lMwpCtr > 0 &&
              Sta->lMwpCtr <= (long) (Sta->dSampRate*iMwpSeconds+0.001) )
         {
            Sta->plRawData[Sta->lMwpCtr] = (long) ((double) Sta->lSampRaw -
                                           Sta->dAveLDCRawOrig);

/* Compute Mwp when array is full or every 20 seconds */
             if ( Sta->lMwpCtr == (long) (Sta->dSampRate*iMwpSeconds+0.1) ||
                 (Sta->lMwpCtr %  (long) (Sta->dSampRate*20.)) == 0 )
             {
                AutoMwp( Sta, &PBuf, dMwpSigNoise, iMwpSeconds, 0 ); 

/* If S:N was great enough to compute an Mwp, report the pick to PICK_RING */
                if ( PBuf.dMwpIntDisp > 0. )
                {	
                   Sta->dMwpIntDisp = PBuf.dMwpIntDisp;
                   Sta->dMwpTime = PBuf.dMwpTime;
                   if ( iRT == 1 )
                      ReportPick( &PBuf, Sta, ucMyModID, siPRegion,
                                  ucEWHTypePickTWC, ucEWHMyInstID, 4 );
                   else if ( iRT == 0 )
                   {
                      *piSaveP = 1;
                      if ( Sta->lMwpCtr ==
                          (long) (Sta->dSampRate*iMwpSeconds+0.1) ) return;
                   }
                }
             }
         }

/* If we are done with Lg and Mwp processing, restart computations with new
   averages */
         if ( Sta->iPickStatus == 3 && 
              dStartTime+(double)i/Sta->dSampRate-Sta->dTrigTime >=
               (double) iMwpSeconds &&
              dStartTime+(double)i/Sta->dSampRate-Sta->dTrigTime >=
               (double) iLGSeconds ) 
         {                            
            Sta->iPickStatus = 1;
            if ( iRT == 1 )
            {
               InitVar( Sta );
               Reset( Sta );
            }
            if ( iRT == 0 ) return;
            goto EndOfLoop;
         }

/* Back to the picker, first check for cycle changes */
         if ( (Sta->lMDFOld <  0 && Sta->lMDFNew <  0) ||
              (Sta->lMDFOld >= 0 && Sta->lMDFNew >= 0) )
         {     /* No changes, continuing adding up MDF */
            Sta->lSampsPerCyc++;
            if ( Sta->lSampsPerCyc < lNumConsec )
               Sta->lMDFRunning += Sta->lMDFNew;
            else
            {
               Sta->lMDFRunning = Sta->lMDFNew;
               Sta->lSampsPerCyc = 0;            
            }
         }
         else  /* Cycle has changed sign, get mags and start anew */
         {
            if ( Sta->lPhase1 == 1 )
            {
               Sta->lMDFTotal += labs( Sta->lMDFRunning );
               Sta->lMDFCnt++;
               if ( GetMbMl ( Sta, i, ucMyModID, siPRegion, ucEWHTypePickTWC,
                              ucEWHMyInstID, iMbCycles, iRT ) < 0 )
                  goto EndOfLoop;     /* Sine-wave cal must be over */
            }
            Sta->lMDFRunning = Sta->lMDFNew;
            Sta->lSampsPerCyc = 0;
         }
		 
/* If this is a wc/atwc sine wave cal, skip further processing */		 
         if ( Sta->iCal ) goto EndOfLoop;
		 
/* Check first motion if we are in first few samples of pick */
         if ( Sta->lFirstMotionCtr >= 1 &&
              Sta->lFirstMotionCtr < FIRST_MOTION_SAMPS )
         {
            if ( (Sta->lMDFRunning < 0 && Sta->lMDFOld < 0) ||
                 (Sta->lMDFRunning >= 0 && Sta->lMDFOld >= 0) )
               Sta->lFirstMotionCtr++;
            else         /* There was a reveral so 1st motion is questionable */
            {
               Sta->cFirstMotion = '?';
               Sta->lFirstMotionCtr = 0;
            }
         }         /* If we've checked enough samples, assume 1st mo. is good */
         if ( Sta->lFirstMotionCtr == FIRST_MOTION_SAMPS )
            Sta->lFirstMotionCtr = 0;

/* If the station is picked, no need to do anything more */
         if ( Sta->iPickStatus == 3 ) goto EndOfLoop;

/* If Phase3 has been passed, wait 3s (for sine cal discrimination) before
   declaring pick */
         if ( Sta->lPhase3 == 1 )
         {   
            Sta->l3sCnt++;
            if ( Sta->l3sCnt < (long) (Sta->dSampRate * 3. + 0.01))
                  goto EndOfLoop;
            else  goto PickCounter;
         }

/* Phase 1: Has MDF trigger threshold been surpassed? */
         if ( ( !Sta->lPhase1 && (double) (labs( Sta->lMDFRunning )) >=
                 Sta->dMDFThresh ) ||
              (  Sta->lPhase1 && (double) (labs( Sta->lMDFRunning )) >=
                 Sta->dMDFThreshOrig ) )
         {
            Sta->lTrigFlag = 1;
            if ( Sta->lPhase1 == 0 )
            {   /* Set phase1 passage here */  
               Sta->lPhase1 = 1;
			   
/* Save existing moving averages and thresholds */
               Sta->dMDFThreshOrig   = Sta->dMDFThresh;
               Sta->dLTAThreshOrig   = Sta->dLTAThresh;
               Sta->dAveLDCRawOrig   = Sta->dAveLDCRaw;
               Sta->dAveRawNoiseOrig = Sta->dAveRawNoise;
               Sta->dAveMDFOrig      = Sta->dAveMDF;
               Sta->lRawNoiseOrig    = Sta->lRawNoise;
			   
/* Look for first motion */                  
               Sta->lFirstMotionCtr = 1;
               if (Sta->lMDFRunning > 0) Sta->cFirstMotion = 'U';
               else                      Sta->cFirstMotion = 'D';

/* If this station is used for Mwp calculations, start Counter and fill buffer*/
               Sta->lMwpCtr = Sta->lSampsPerCyc;
               if ( Sta->lMwpCtr == 0 ) Sta->lMwpCtr = 1;
	       
               if ( Sta->dSens > 0.0 && Sta->iComputeMwp )
               {
                  lCnt = Sta->lRawTempCtr - Sta->lSampsPerCyc - 1;
                  if ( lCnt < 0 ) lCnt += Sta->lRawTempSize;
                  for ( ii=0; ii<Sta->lSampsPerCyc; ii++ )
                  {
                     Sta->plRawData[ii] = (long) ((double)
                      Sta->plRawTempBuff[lCnt] - Sta->dAveLDCRawOrig);
                     lCnt++;
                     if ( lCnt >= Sta->lRawTempSize ) lCnt -= Sta->lRawTempSize;
                  }		  
               }

/* Save P-time (# seconds since 1/1/1970) */
               Sta->dTrigTime = dStartTime +
                (double) (i-Sta->lSampsPerCyc)/Sta->dSampRate;
            }
         }
         if ( Sta->lPhase2 == 1 ) goto Phase3;
         if ( Sta->lPhase1 == 0 ) goto Phase4;

/* Phase 2: P-Phase processing */
         Sta->lPhase2Cnt++;
         if ( Sta->lPhase2Cnt > 3*lNumConsec ) goto Reset;

/* Count trigger passes versus misses */
         if ( Sta->lTrigFlag == 1 )
         {
            Sta->lHit++;
            if ( Sta->lHit == lNumConsec ) Sta->lTest1 = 1;
         }
         else
         {
            Sta->lMis++;

/* NOTE: By Veith's paper, this test should be performed after the passing 
   of tests 1 and 2.  The picks are much better, though, if it is done 
   sample-by-sample. */
            if ( Sta->lMis > Sta->lHit ) goto Reset; // Fail test 3
         }

/* Test2 in Phase2 - Must exceed following amp. sometime in phase2 */
         if ( fabs ((double) Sta->lSampNew-Sta->dAveLDC) > Sta->dLTAThreshOrig )
            Sta->lTest2 = 1;

/* See if Phase 2 has passed */
         if ( Sta->lTest1+Sta->lTest2 != 2 ) goto Phase4;

/* Otherwise, Phase 2 has passed. So, get ready for Phase 3 */
         Sta->lPhase2 = 1;
         Sta->lNumOsc = 0;
         Sta->lHit = 0;
         Sta->lMis = 0;
         Sta->lLastSign = 0;
         if ( Sta->lMDFRunning < 0 ) Sta->lLastSign = 1;

/* Phase 3: Look for oscillatory motion */
Phase3:  Sta->lPhase3Cnt++;

/* Below is time limit for passing Phase 3 (test 2) */
         if ( Sta->lPhase3Cnt > 12*lNumConsec ) goto Reset;

/* Check to see if trigger MDF was exceeded */
         if ( Sta->lTrigFlag == 1 ) Sta->lHit++;
         else                       Sta->lMis++;
         if ( Sta->lTrigFlag == 0 ) goto Phase4;

/* Next, check for oscillations */
         Sta->lCurSign = 0;
         if ( Sta->lMDFRunning < 0 ) Sta->lCurSign = 1;
         if ( Sta->lLastSign != Sta->lCurSign ) Sta->lNumOsc++;

/* Phase3, test3 is passed when 6 reverses are noted */
         if ( Sta->lNumOsc < 6 )
         {
            Sta->lLastSign = Sta->lCurSign;
            goto Phase4;
         }

/* If MDF < trigger value more than not and for more than 4s, fail test 3 */
         if (Sta->lMis > Sta->lHit && Sta->lMis > (long)(4.*Sta->dSampRate)) 
            goto Reset;
	   
/* Figure out the frequency of this signal */
         if ( Sta->lMwpCtr == 0 ) Sta->lMwpCtr = 1;
         Sta->dFreq = 1. / (((double) Sta->lMwpCtr/Sta->dSampRate) / 6.);

/* Otherwise, phase 3 was passed; don't declare pick yet, wait 3s for in case
   its cal */
         Sta->lPhase3 = 1;
         goto EndOfLoop;

/* Declare and report the pick */
PickCounter: 
         Sta->iPickStatus = 3;
         lPickCounter++;
         if ( lPickCounter >= 9999 ) lPickCounter = 1;
         Sta->lPickIndex = lPickCounter;    
         if ( fabs( Sta->dMDFThreshOrig ) < 0.0001 ) Sta->dMDFThreshOrig = 1.;
         Sta->dPStrength = ((double) Sta->lMDFTotal/(double) Sta->lMDFCnt) /
                                      Sta->dMDFThreshOrig;
				     
/* This is a fourth stage that the pick must pass to be declared.  It is there
   to filter out high frequency-low strength P-picks.  This was added by PH and
   PW in June, 2007 */				     
         if ( (Sta->dPStrength > dSNLocal || Sta->dFreq < dMinFLoc) &&
               iRT == 1 )
            ReportPick( &PBuf, Sta, ucMyModID, siPRegion, ucEWHTypePickTWC,
                         ucEWHMyInstID, 4 );
         else if ( (Sta->dPStrength > dSNLocal || Sta->dFreq < dMinFLoc) &&
                    iRT == 0 )
            *piSaveP = 1;
         else 
         {
            logit( "t", "4th pick rejection %s F-%lf SN-%lf\n", Sta->szStation,
                   Sta->dFreq, Sta->dPStrength );
	    goto Reset;                /* Did not pass this new phase 4 */
         }
         time( &now );	               /* See that its not old data */
         if ( now-Sta->dTrigTime < RECENT_ALARM_TIME )
             if ( i2StnAlarmOn == 1 && iRT == 1 )
                 CheckForAlarm( Sta, pAS, iNumReg, ucMyModID,
                                siAlarmRegion, ucEWHTypeAlarm,
                                ucEWHMyInstID );        /* Check for alarm */
         goto Phase4;

/* Reset some picker variables, one of the tests failed */
Reset:   Reset( Sta );
                        
/* Start of Phase 4 (Phase 4 is mainly skipped here.  Events are terminated 
   when magnitude information has been computed). */
Phase4:  Sta->lTrigFlag = 0;
EndOfLoop:;
      }
      Sta->lMDFOld = Sta->lMDFNew;
   }
}

     /**************************************************************
      * PPickStruct()                      *
      * *
      * Fill in PPICK structure from PickTWC message.              *
      * *
      * Arguments:                                                 *
      * PIn         PickTWC message from ring                     *
      * PPick       P-pick data structure                         *
      * TypePickTWC Earthworm message type expected               *
      * *
      * Return - 0 if OK, -1 if wrong message type                 *
      **************************************************************/

int PPickStruct( char *PIn, PPICK *PPick, unsigned char TypePickTWC )
{
   int      iMessageType, iModId, iInst;  /* Incoming logo */

/* Break up incoming message (PARCHADO A %d PARA VARIABLES INT EN 64 BITS)
   *************************/
   sscanf( PIn,    "%d %d %d %s %s %s %ld %d %lf %c %s %lf %lf %lf %lf %lf "
                   "%lf %lf %lf %lf %lE %lf %d %lf %lf",
           &iMessageType, &iModId, &iInst, PPick->szStation, PPick->szChannel,
            PPick->szNetID, &PPick->lPickIndex, &PPick->iUseMe, &PPick->dPTime,
           &PPick->cFirstMotion, PPick->szPhase,
           &PPick->dMbAmpGM, &PPick->dMbPer, &PPick->dMbTime,
           &PPick->dMlAmpGM, &PPick->dMlPer, &PPick->dMlTime,
           &PPick->dMSAmpGM, &PPick->dMSPer, &PPick->dMSTime,
           &PPick->dMwpIntDisp, &PPick->dMwpTime, &PPick->iHypoID,
           &PPick->dPStrength, &PPick->dFreq );

   if ( iMessageType == TypePickTWC )
      return 0;
   else
   {
      logit( "te", "Incoming message type %d; must be PickTWC\n",
               iMessageType );
      return -1;
   }
}

     /**************************************************************
      * PPickMatch()                       *
      * *
      * Fill in PPICK structure with data from StaDataFile.        *
      * *
      * Arguments:                                                 *
      * PPick       P-pick data structure                         *
      * StaArray    Station data array                            *
      * Nsta        Number of stations in array                   *
      * iGeo        1->STATION lat/lon in geographic coords.,     *
      * 2->STATION lat/lon in geocentric coords.      *
      * *
      * Return - 0 if OK, -1 if no match                           *
      **************************************************************/

int PPickMatch( PPICK *PPick, STATION *StaArray, int Nsta, int iGeo )
{
   int    i;

/* Search StaArray for SCN which has just arrived, then copy data
   **************************************************************/
   for ( i=0; i<Nsta; i++ )
      if ( !strcmp( PPick->szStation, StaArray[i].szStation ) &&
           !strncmp( PPick->szChannel, StaArray[i].szChannel, 3 ) &&
           !strcmp( PPick->szNetID,   StaArray[i].szNetID ) )
      {
         PPick->dLat             = StaArray[i].dLat;
         PPick->dLon             = StaArray[i].dLon;
         if ( iGeo == 1 )
         {
            GeoCent( (LATLON *) PPick );
            GetLatLonTrig( (LATLON *) PPick );
         }
         else if ( iGeo == 2 )
            GetLatLonTrig( (LATLON *) PPick );
         PPick->dSens            = StaArray[i].dSens;
         PPick->dClipLevel       = StaArray[i].dClipLevel;
         PPick->dElevation       = StaArray[i].dElevation;
         PPick->dGainCalibration = StaArray[i].dGainCalibration;
         PPick->iStationType     = StaArray[i].iStationType;
         return 0;
      }             
   logit( "te", "No match in StaDataFile for %s %s %s\n",
          PPick->szStation, PPick->szChannel, PPick->szNetID );
   return -1;
}

 /***********************************************************************
  * RawDataBuff()                           *
  * *
  * Fill raw data circular buffer.  This buffer is necessary so that   *
  * old data is available to fill Mwp raw data buffer after phase 1    *
  * is passed.  It only needs to be big enough to hold samples that    *
  * accumulate while Phase 1 has not yet passed.                       *
  * *
  * Arguments:                                                         *
  * Sta              Pointer to station being processed             *
  * *
  ***********************************************************************/
             
void RawDataBuff( STATION *Sta )
{
   Sta->plRawTempBuff[Sta->lRawTempCtr] = Sta->lSampRaw;
   Sta->lRawTempCtr++;
   if ( Sta->lRawTempCtr == Sta->lRawTempSize ) Sta->lRawTempCtr = 0;
}

 /***********************************************************************
  * Reset()                                *
  * Reset some picker variables when a phase/test has failed       *
  * *
  * Arguments:                                                         *
  * Sta              Pointer to station being processed             *
  * *
  ***********************************************************************/

void Reset( STATION *Sta )
{
   Sta->lPhase1 = 0;
   Sta->lPhase2 = 0;
   Sta->lPhase3 = 0;               
   Sta->lTest1 = 0;
   Sta->lTest2 = 0;
   Sta->lHit = 0;
   Sta->lMis = 0;
   Sta->dMaxPk = 0.;
   Sta->lCycCnt = 0;
   Sta->lPer = 0;
   Sta->lMlPer = 0;
   Sta->dMlTime = 0.;
   Sta->lMbPer = 0;
   Sta->dMbTime = 0.;
   Sta->dMlAmpGM = 0.;
   Sta->dMbAmpGM = 0.;
   Sta->lMwpCtr = 0;
   Sta->iCal = 0;
   Sta->cFirstMotion = '?';
   Sta->lFirstMotionCtr = 0;
   Sta->lMagAmp = 0;
   Sta->dAvAmp = 0.;
   Sta->lSWSim = 0;
   Sta->lPhase2Cnt = 0;
   Sta->lPhase3Cnt = 0;
   Sta->dTrigTime = 0.;
   Sta->dMDFThreshOrig = 0.;
   Sta->dLTAThreshOrig = 0.;
   Sta->dAveLDCRawOrig = 0.;
   Sta->dAveRawNoiseOrig = 0.;
   Sta->dAveMDFOrig = 0.;
   Sta->lRawNoiseOrig = 0;
   Sta->lMDFTotal = 0;
   Sta->lMDFCnt = 0;
   Sta->dPStrength = 0.;
   Sta->dFreq = 0.;
}

 /***********************************************************************
  * SeismicAlarm()                             *
  * *
  * Check signal for large, cyclical motion.  Send alarm if noted.     *
  * *
  * Arguments:                                                         *
  * Sta              Pointer to station being processed             *
  * lNumSamp         Number of samples to evaluate                  *
  * WaveLong         Pointer to array of filtered data              *
  * iType            Alarm Type - 1=SP Alarm, 3=LP Alarm            *
  * ucMyModID        Calling module ID                              *
  * siAlarmRegion    Calling module Alarm Ring                      *
  * ucEWHTypeAlarm   Earthworm alarm message number                 *
  * ucEWHMyInstID    Earthworm Institute ID                         *
  * dAlarmTimeout    Time till station can be re-alarmed (s)        *
  * dStartTime       Start time (1/1/70 seconds) of the packet      *
  * *
  ***********************************************************************/

void SeismicAlarm( STATION *Sta, long lNumSamp,
                   long *WaveLong, int iType, unsigned char ucMyModID,
                   SHM_INFO siAlarmRegion, unsigned char ucEWHTypeAlarm,
                   unsigned char ucEWHMyInstID, double dAlarmTimeout,
                   double dStartTime )
{
   int  i;
   long lNumConsecA;       /* Max # samples which can pass without an alarm
                              being declared. Reset variables when no alarm. */

/* Compute lNumConsecA */
   lNumConsecA = (long) (Sta->dSampRate / (2.*Sta->dAlarmMinFreq) + 0.0001);
   
/* Initialize SeismicAlarm variables if necessary */
   if ( Sta->iAlarmStatus == 1 )
   {
      Sta->lAlarmP1 = 0;
      Sta->lAlarmCycs = 0;
      Sta->lAlarmSamps = 0;		     
      Sta->iAlarmStatus = 2;
   }

/* Loop through each sample in data buffer */   
   if ( Sta->iAlarmStatus >= 2 )
      for ( i=0; i<lNumSamp; i++ )
      {       
	  
/* Reset Alarm status and variables if timeout has passed */
         if ( Sta->iAlarmStatus == 3 )
         {
            if ( (dStartTime + (double) i/Sta->dSampRate) >
                  Sta->dAlarmLastTriggerTime+dAlarmTimeout )
            {
               Sta->iAlarmStatus = 2;
               goto ResetA;
            }
            goto EndOfLoop;
         }
  	   
/* Convert SP (filtered) data to approximate value in m/s */	
         Sta->dAlarmSamp = (double) WaveLong[i] / Sta->dSens;
		
/* Does this value exceed the alarm amplitude threshold? */
         if ( (fabs( Sta->dAlarmSamp ) > Sta->dAlarmAmp) && !Sta->lAlarmP1 )
         {
            Sta->lAlarmP1 = 1;
            Sta->dAlarmLastSamp = Sta->dAlarmSamp;
            Sta->lAlarmCycs = 0;
            Sta->lAlarmSamps = 0;
         }
		
/* If Phase 1 is passed, look for strong, cyclical motion */		
         if ( Sta->lAlarmP1 )
         {
            Sta->lAlarmSamps++;
            Sta->lAlarmCycs++;
			
/* Signal has lNumConsecA samples to reverse and exceed the threshold, or
   processing will start over */			
            if ( Sta->lAlarmCycs > lNumConsecA ) goto ResetA;
			
/* Has the sample's sign changed and is it over the threshold */
            if ( Sta->dAlarmSamp*Sta->dAlarmLastSamp < 0. &&
                 fabs (Sta->dAlarmSamp) > Sta->dAlarmAmp )
            {
               Sta->lAlarmCycs = 0;
               Sta->dAlarmLastSamp = Sta->dAlarmSamp;
            }
	 		
/* Has the signal stayed in the threshold range for long enough
   without having a timeout to declare an alarm? */
            if ( (double) Sta->lAlarmSamps > Sta->dSampRate*Sta->dAlarmDur )
            {     /* If yes, report the alarm to ring */
               Sta->iAlarmStatus = 3;
               Sta->dAlarmLastTriggerTime = dStartTime +
                                            (double) i/Sta->dSampRate;
               ReportAlarm( Sta, ucMyModID, siAlarmRegion,
                ucEWHTypeAlarm, ucEWHMyInstID, iType, Sta->szStation, 0 );
            }
         }
      goto EndOfLoop;
	  
/* Reset variables when alarm threshold can not be sustained for duration */	  
ResetA:		      
      Sta->lAlarmP1 = 0;
      Sta->lAlarmCycs = 0;
      Sta->lAlarmSamps = 0;		     
EndOfLoop:;
      }
}  
     /**************************************************************
      * ShortCopyPBuf()                       *
      * *
      * Update magnitudes in main PBuffer.                         *
      * *
      * Arguments:                                                 *
      * PIn         Input PPICK structure                         *
      * POut        Output PPICK structure                        *
      * *
      **************************************************************/

void ShortCopyPBuf( PPICK *PIn, PPICK *POut )
{
   POut->dMbAmpGM         = PIn->dMbAmpGM;
   POut->dMbPer           = PIn->dMbPer;
   POut->dMbTime          = PIn->dMbTime;
   POut->dMbMag           = PIn->dMbMag;
   POut->iMbClip          = PIn->iMbClip;
   POut->dMlAmpGM         = PIn->dMlAmpGM;
   POut->dMlPer           = PIn->dMlPer;
   POut->dMlTime          = PIn->dMlTime;
   POut->dMlMag           = PIn->dMlMag;
   POut->iMlClip          = PIn->iMlClip;
   POut->dMSAmpGM         = PIn->dMSAmpGM;
   POut->dMSPer           = PIn->dMSPer;
   POut->dMSTime          = PIn->dMSTime;
   POut->dMSMag           = PIn->dMSMag;
   POut->iMSClip          = PIn->iMSClip;
   POut->dMwpIntDisp      = PIn->dMwpIntDisp;
   POut->dMwpTime         = PIn->dMwpTime;
   POut->dMwpMag          = PIn->dMwpMag;
   POut->dMwAmpGM         = PIn->dMwAmpGM;
   POut->dMwPer           = PIn->dMwPer;
   POut->dMwTime          = PIn->dMwTime;
   POut->dMwMag           = PIn->dMwMag;
   POut->iMwClip          = PIn->iMwClip;
   POut->dRes             = PIn->dRes;
   POut->dDelta           = PIn->dDelta;
   POut->dAz              = PIn->dAz;
   POut->dFracDelta       = PIn->dFracDelta;
   POut->dSnooze          = PIn->dSnooze;
   POut->dCooze           = PIn->dCooze;
}
