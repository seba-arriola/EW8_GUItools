/************************************************************************
  * LOCQUAKE.C                                                           *
  ************************************************************************/
  
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <earthworm.h>
#include <transport.h>
#include "earlybirdlib.h"
#include "iaspei91.h"          /* Tau/P travel times (from iaspei91 model) */

void ComputeTimeWindows( double dOTime, int iDepthDum, STATION StaArray[],
                         int iNumStas, LATLON *pLLepi, PHASE_WINDOWS pPW[] )
{
   AZIDELT Azi;
   double  dDist2, dOver, dTemp1, dTemp2, dFracDep; 
   int     i, iTab, iDep;
   int     iDepth;                               
   int     iRTravTime[181] = {0,32,63,95,127,159,190,222,254,285,317,
            349,381,412,441,475,507,539,571,603,634,            
            661,688,715,742,769,796,823,850,877,900,            
            930,960,990,1020,1050,1080,1110,1140,1170,1200,     
            1230,1260,1290,1320,1350,1380,1410,1440,1470,1500, 
            1530,1560,1590,1620,1650,1680,1710,1740,1770,1800, 
            1819,1838,1858,1877,1896,1915,1934,1953,1972,1992, 
            2021,2049,2078,2106,2135,2163,2192,2220,2248,2277, 
            2305,2334,2362,2391,2419,2448,2476,2505,2533,2562, 
            2590,2618,2648,2675,2704,2732,2761,2789,2818,2846, 
            2875,2903,2932,2960,2988,3017,3045,3074,3102,3131, 
            3159,3188,3216,3245,3273,3302,3330,3358,3387,3415, 
            3444,3472,3501,3529,3558,3586,3615,3643,3672,3700, 
            3728,3757,3785,3814,3842,3871,3899,3928,3956,3985, 
            4013,4042,4070,4098,4127,4155,4184,4212,4241,4269, 
            4298,4326,4355,4383,4412,4440,4468,4497,4525,4554, 
            4582,4611,4639,4668,4696,4725,4753,4782,4810,4838, 
            4867,4895,4924,4952,4981,5009,5038,5066,5095,5123};
   LATLON  ll;
   float  *pfIAT;              
        
   for ( i=0; i<iNumStas; i++ )
   {
      ll.dLat = StaArray[i].dLat;
      ll.dLon = StaArray[i].dLon;
      GeoCent( &ll );
      GetLatLonTrig( &ll );
      Azi = GetDistanceAz( pLLepi, &ll );
      StaArray[i].dDelta = Azi.dDelta;
      StaArray[i].dAzimuth = Azi.dAzimuth;
   }
   iDepth = iDepthDum;
   
   if ( iDepth < 0 ) iDepth = 0;
   if ( iDepth >= (int) ((DEPTH_LEVELS_IASP-1)*IASP_DEPTH_INC) )  
      iDepth = (int) ((DEPTH_LEVELS_IASP-1)*IASP_DEPTH_INC);
   iDep = (int) ((double) iDepth/IASP_DEPTH_INC) + 1;
   dFracDep = (double) iDepth/IASP_DEPTH_INC - 
              floor( (double) iDepth/IASP_DEPTH_INC );

   for ( i=0; i<iNumStas; i++ )
   {
      dOver = StaArray[i].dDelta*(1./IASP_DIST_INC) - 
              floor( StaArray[i].dDelta * (1./IASP_DIST_INC) );
      iTab = (int) (StaArray[i].dDelta * (1./IASP_DIST_INC));

      pfIAT = fPP + iDep*IASP_NUM_PER_DEP + iTab;
      dTemp1 = *pfIAT + (*(pfIAT+1) - *pfIAT) * dOver;
      dTemp2 = *(pfIAT-IASP_NUM_PER_DEP) + 
               (*(pfIAT-IASP_NUM_PER_DEP+1) - *(pfIAT-IASP_NUM_PER_DEP))*dOver;
      pPW[i].dPTravTime = dTemp1*dFracDep + dTemp2*(1.-dFracDep);
   }

   for ( i=0; i<iNumStas; i++ )
   {
      dDist2 = StaArray[i].dDelta;
      dOver = dDist2 - floor( dDist2 );
      pPW[i].dRTravTime = (double) iRTravTime[(int) dDist2] +
       ((double) (iRTravTime[(int) dDist2+1] - iRTravTime[(int) dDist2]) * dOver);
   }

   for ( i=0; i<iNumStas; i++ ) 
   {
      pPW[i].dPStart = dOTime + pPW[i].dPTravTime;
      pPW[i].dPEnd = dOTime + pPW[i].dPTravTime + MB_TIME;
   }

   for ( i=0; i<iNumStas; i++ )
   {
      pPW[i].dRStart = dOTime + pPW[i].dRTravTime - 10.;
      pPW[i].dREnd = dOTime + pPW[i].dRTravTime + MS_TIME;
   }
}

void FindBadPs( int iPNum, int iArea, PPICK P[], HYPO *pHypo, double dLatUser,
                double dLonUser, int iMinPs, EQDEPTHDATA pEqDep[] ) 
{
   double  dMinResid;     
   int     i, j, k, iTemp;
   int     iCnt;          
   int     iLocator;      
   int     iMinIndex1, iMinIndex2, iMinIndex3;  
   
   if ( MAX_TO_KO < 1 ) return;

   pHypo->dDepth = DEPTHKM;         
   pHypo->iDepthControl = 3;        
   iLocator = 1;
   iMinIndex1 = 0;
   iMinIndex2 = 0;
   iMinIndex3 = 0;

   iCnt = 0;
   for ( i=0; i<iPNum; i++ )
      if ( P[i].iUseMe > 0 && P[i].dPTime > 0. ) iCnt++;

   if ( iCnt < 5 ) 
   {
      InitHypo( pHypo );		
      InitialLocator( iPNum, iArea, 1, P, pHypo, dLatUser, dLonUser );
      QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );
      IsItGoodSoln( iPNum, P, pHypo, 6 );

      if (pHypo->iGoodSoln != 3 && iArea == 3)
      {
         InitHypo (pHypo);		
         InitialLocator (iPNum, iArea, 2, P, pHypo, dLatUser, dLonUser);
         QuakeSolveIasp (iPNum, P, pHypo, pEqDep, 1);
      }
      return;
   }

   dMinResid = 1.e20;                     
   for ( i=0; i<iPNum; i++ )
      if ( P[i].iUseMe == 1 && P[i].dPTime > 0. )
      {
         P[i].iUseMe = 0;
         iTemp = 1;
         InitHypo( pHypo );		
         InitialLocator( iPNum, iArea, iTemp, P, pHypo, dLatUser, dLonUser );
         QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );
         IsItGoodSoln( iPNum, P, pHypo, iMinPs );
         if ( pHypo->iGoodSoln != 3 && iArea == 3 )
         {            
            iTemp = 2;
            InitHypo( pHypo );		
            InitialLocator( iPNum, iArea, iTemp, P, pHypo, dLatUser, dLonUser );
            QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );
         }
         if ( pHypo->dAvgRes < dMinResid ) 
         {            
            dMinResid = pHypo->dAvgRes;
            iMinIndex1 = i;
            iLocator = iTemp;
         }
         P[i].iUseMe = 1;
      }
      
   P[iMinIndex1].iUseMe = 0;
   InitHypo( pHypo );		
   InitialLocator( iPNum, iArea, iLocator, P, pHypo, dLatUser, dLonUser );
   QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );

   if ( iCnt <= 6 ) return;

   IsItGoodSoln( iPNum, P, pHypo, iMinPs );
   if ( pHypo->iGoodSoln == 3 ) return;
   P[iMinIndex1].iUseMe = 1;
   
   if ( MAX_TO_KO < 2 ) return;
   dMinResid = 1.e20;                     
   for ( i=0; i<iPNum-1; i++ )
      if ( P[i].iUseMe == 1 && P[i].dPTime > 0. )
         for ( j=i+1; j<iPNum; j++ )
    	    if ( P[j].iUseMe == 1 && P[j].dPTime > 0. )
            {
               P[i].iUseMe = 0;          
               P[j].iUseMe = 0;
               iTemp = 1;
               InitHypo( pHypo );		
               InitialLocator( iPNum, iArea, iTemp, P, pHypo, dLatUser,
                               dLonUser );
               QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );
               IsItGoodSoln( iPNum, P, pHypo, iMinPs );
               if ( pHypo->iGoodSoln != 3 && iArea == 3 )
               {        
                  iTemp = 2;
                  InitHypo( pHypo );		
                  InitialLocator( iPNum, iArea, iTemp, P, pHypo, dLatUser,
                                  dLonUser );
                  QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );
               }
               if ( pHypo->dAvgRes < dMinResid ) 
               {        
                  dMinResid = pHypo->dAvgRes;
                  iMinIndex1 = i;
                  iMinIndex2 = j;
                  iLocator = iTemp;
               }
               P[i].iUseMe = 1;
               P[j].iUseMe = 1;
            }
	   
   P[iMinIndex1].iUseMe = 0;
   P[iMinIndex2].iUseMe = 0;
   InitHypo( pHypo );		
   InitialLocator( iPNum, iArea, iLocator, P, pHypo, dLatUser, dLonUser );
   QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );
	
   if ( iCnt <= 10 || iCnt > 20 ) return;

   IsItGoodSoln( iPNum, P, pHypo, iMinPs );
   if ( pHypo->iGoodSoln == 3 ) return;
   P[iMinIndex1].iUseMe = 1;
   P[iMinIndex2].iUseMe = 1;

   if ( MAX_TO_KO < 3 ) return;
   dMinResid = 1.e20;                     
   for ( i=0; i<iPNum-2; i++ )
      if ( P[i].iUseMe == 1 && P[i].dPTime > 0. )
         for ( j=i+1; j<iPNum-1; j++ )
            if ( P[j].iUseMe == 1 && P[j].dPTime > 0. )
               for ( k=j+1; k<iPNum; k++ )
                  if ( P[k].iUseMe == 1 && P[k].dPTime > 0. )
                  {
                     P[i].iUseMe = 0;       
                     P[j].iUseMe = 0;
                     P[k].iUseMe = 0;
                     iTemp = 1;
                     InitHypo( pHypo );			
                     InitialLocator( iPNum, iArea, iTemp, P, pHypo, 
                                     dLatUser, dLonUser);
                     QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );
                     IsItGoodSoln( iPNum, P, pHypo, iMinPs );
                     if ( pHypo->iGoodSoln != 3 && iArea == 3 )
                     {  
                        iTemp = 2;
                        InitHypo( pHypo );		
                        InitialLocator( iPNum, iArea, iTemp, P, pHypo, 
                                        dLatUser, dLonUser );
                        QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );
                     }
                     if ( pHypo->dAvgRes < dMinResid )  
                     {  
                        dMinResid = pHypo->dAvgRes;
                        iMinIndex1 = i;
                        iMinIndex2 = j;
                        iMinIndex3 = k;
                        iLocator = iTemp;
                     }
                     P[i].iUseMe = 1;
                     P[j].iUseMe = 1;
                     P[k].iUseMe = 1;
                  }
		  
   P[iMinIndex1].iUseMe = 0;
   P[iMinIndex2].iUseMe = 0;
   P[iMinIndex3].iUseMe = 0;
   InitHypo( pHypo );		
   InitialLocator( iPNum, iArea, iLocator, P, pHypo, dLatUser, dLonUser );
   QuakeSolveIasp( iPNum, P, pHypo, pEqDep, 1 );
}
       
int FindDepth( double dLatpass, double dLonpass, EQDEPTHDATA pEqDep[] )
{
   int i, iRndLat, iRndLon;
   
   iRndLon = Round( dLonpass );
   iRndLat = Round( dLatpass );
 
   for ( i=0; i<EQSIZE; i++ ) 
      if ( pEqDep[i].iLat == iRndLat && pEqDep[i].iLon == iRndLon )
         return (i);
   return (-1);
}

void ForceLocIasp( int iPNum, PPICK P[], HYPO *pHypo )
{
   double  fih, fh, ptt1, ptt2, ptt;       
   int     i, iCnt, ih, itab;              
   float  *pfIA;                           

   pHypo->iNumPs    = 0;
   pHypo->iNumBadPs = 0;
   for ( i=0; i<iPNum; i++ )
      if ( P[i].iUseMe && P[i].dPTime > 0. ) pHypo->iNumPs++;
      else                                   pHypo->iNumBadPs++;

   GetEpiAzDelta( iPNum, P, pHypo );

   pHypo->dAvgRes = 0.0;
   fih = pHypo->dDepth/IASP_DEPTH_INC;  
   if ( fih <= 1./IASP_DEPTH_INC )      
   {
      fih = 1./IASP_DEPTH_INC;					
      pHypo->dDepth = 1.;
   }
   ih = (int) (fih + 1.0);
   fh = fih - floor( fih );
   if ( ih >= DEPTH_LEVELS_IASP - 1 )   
   {
      ih = DEPTH_LEVELS_IASP - 1;
      pHypo->dDepth = 750.;
      fh = 0.0;
   }

   iCnt = 0;	
   for ( i=0; i<iPNum; i++ )
      if ( P[i].dPTime > 0. )
      {	
         itab = (int) (P[i].dDelta*(1./IASP_DIST_INC));
         if ( P[i].dFracDelta - IASP_DIST_INC >= 0.0 ) 
            P[i].dFracDelta -= IASP_DIST_INC;
         pfIA = fPP + ih*IASP_NUM_PER_DEP + itab;	

         ptt1 = *pfIA + (*(pfIA+1)-*pfIA)*P[i].dFracDelta/IASP_DIST_INC;
         ptt2 = *(pfIA-IASP_NUM_PER_DEP) + (*(pfIA-IASP_NUM_PER_DEP+1)-
                *(pfIA-IASP_NUM_PER_DEP))*P[i].dFracDelta/IASP_DIST_INC;
         ptt = ptt1*fh + ptt2*(1.-fh);
         P[i].dRes = P[i].dPTime - ((pHypo->dOriginTime) + ptt) -
                     P[i].dElevation/EARTHRAD;    
         if ( P[i].iUseMe )			
            pHypo->dAvgRes += fabs (P[i].dRes);
         iCnt++;
      }
   pHypo->dAvgRes = pHypo->dAvgRes/(double) iCnt;
}
       
void GetEpiAzDelta( int iPNum, PPICK *P, HYPO *pHypo )
{
   double  dDeltaCosine, dDeltaSine;
   double  dTempDelta, dTempAz;
   int     i;

   GetLatLonTrig( (LATLON *) pHypo );
   if ( pHypo->dSinlat == 0.0 ) pHypo->dSinlat = 0.01;

   for ( i=0; i<iPNum; i++ )
   {
      dDeltaCosine = pHypo->dCoslat*P[i].dCoslat + pHypo->dSinlat*
                     P[i].dSinlat*(pHypo->dCoslon*P[i].dCoslon +
                     pHypo->dSinlon*P[i].dSinlon);
      dDeltaSine = sqrt( 1. - dDeltaCosine*dDeltaCosine );
      if ( dDeltaSine == 0.0 )   dDeltaSine = 0.01;
      if ( dDeltaCosine == 0.0 ) dDeltaCosine = 0.01;
      dTempDelta =  atan( dDeltaSine / dDeltaCosine );
      P[i].dCooze = (P[i].dCoslat - pHypo->dCoslat * dDeltaCosine) /
                    (pHypo->dSinlat * dDeltaSine);
      if ( P[i].dCooze == 0.0 ) P[i].dCooze = 0.0001;
      P[i].dSnooze = P[i].dSinlat * (pHypo->dCoslon * P[i].dSinlon -
                     pHypo->dSinlon * P[i].dCoslon) / dDeltaSine;
      dTempAz = atan( P[i].dSnooze / P[i].dCooze );
      while ( dTempAz < 0.0 )    dTempAz += PI;
      if ( P[i].dSnooze <= 0. )  dTempAz += PI;
      while ( dTempDelta < 0.0 ) dTempDelta += PI;
      dTempDelta *= DEG;
      P[i].dFracDelta = dTempDelta - floor( dTempDelta );
      P[i].dDelta = dTempDelta;
      P[i].dAz = dTempAz * DEG;
   }
}
	   
void GetPTimes( int iPNum, PPICK *P, HYPO *pHypo )
{
   AZIDELT azidelt;        
   double  dCorr;          
   int     i;
   static  int  iDLev;     
   int     iDepth;         
   
   iDepth = (int) pHypo->dDepth;

   if (iDepth >= (int) ((DEPTH_LEVELS_IASP-1)*IASP_DEPTH_INC)) 
       iDepth = (int) ((DEPTH_LEVELS_IASP-1)*IASP_DEPTH_INC);
   iDLev = (int) ( (double) iDepth /
                 (double) IASP_DEPTH_INC) * IASP_NUM_PER_DEP;

   for ( i=0; i<iPNum; i++ )
   {
      /* FIX 64 BITS: Casting from struct containing strings to LATLON is undefined */
      LATLON llH;
      llH.dLat = pHypo->dLat; llH.dLon = pHypo->dLon;
      llH.dCoslat = pHypo->dCoslat; llH.dSinlat = pHypo->dSinlat;
      llH.dCoslon = pHypo->dCoslon; llH.dSinlon = pHypo->dSinlon;
      
      LATLON llP;
      llP.dLat = P[i].dLat; llP.dLon = P[i].dLon;
      llP.dCoslat = P[i].dCoslat; llP.dSinlat = P[i].dSinlat;
      llP.dCoslon = P[i].dCoslon; llP.dSinlon = P[i].dSinlon;

      azidelt = GetDistanceAz( &llH, &llP );
      
      P[i].dDelta = azidelt.dDelta;
      P[i].dAz = azidelt.dAzimuth;
      dCorr = azidelt.dDelta*(1./IASP_DIST_INC) - 
              floor( azidelt.dDelta*(1./IASP_DIST_INC) );
      P[i].dExpectedPTime = fPP[(int) (azidelt.dDelta*(1./IASP_DIST_INC))+
              iDLev] + dCorr*(fPP[(int) (azidelt.dDelta*
              (1./IASP_DIST_INC))+1+iDLev] - fPP[(int)
              (azidelt.dDelta*(1./IASP_DIST_INC))+iDLev]) + pHypo->dOriginTime;				
   }
}
	   
void InitialLocator( int iPNum, int iArea, int iTry, PPICK *P, HYPO *pHypo,
                     double dLatUser, double dLonUser )
{
   static AZIDELT azidelt[MAX_STATION_DATA];  
   AZIDELT azidelt01, azidelt02, azidelt12;
   AZIDELT azideltAB, azideltAC;
   AZIDELT azideltB, azideltC;  
   AZIDELT azideltTemp;	        
   double  dAng[3];             
   double  dAngIncident;        
   double  dAngle[3];           
   double  dApVel;              
   double  dAzEpiAB, dAzEpiAC;  
   double  dAz;                 
   double  dCorrB, dCorrC;      
   double  dDelta = 0.0;        
   double  dHyp;                
   double  dTdAB, dTdAC;        
   double  dTdABm, dTdACm;      
   double  dTemp1, dTemp2, dTempLon;
   double  dx, dy;              
   
   /* FIX 64 BITS: Init firmly to 0 */
   int i1=0, i2=0, i3=0, i=0, j=0, k=0, iCnt=0, l=0, n=0, ia=0, ib=0, ic=0; 
         
   static int iDelt[] = {520,506,506,502,502,492,496,487,487,483,476,469,
            460,451,447,442,434,425,395,365,346,328,322,316,310,305,
            300,295,291,287,287,287,282,277,275,273,271,270,268,267,
            265,263,261,260,258,256,254,253,249,246,242,239,237,236,
            233,229,226,222,220,219,215,212,209,206,204,202,201,199,
            196,193,189,186,184,182,181,179,176,173,171,170,166,163,
            161,160,157,154,152,150,148,147,145,144,142,141,141,141,
            141,141,142,144,142,141,139,138};
   static  int  iDLev;             
   int     iIndices[4] = {0, 0, 0, 0}; 
   LATLON  llAKArray;           
   LATLON  llCen1;              
   LATLON  llCen2;              
   LATLON  llEpi;               
   
   logit("e", "DEBUG LQ 3.1: Entramos a InitialLocator con exito\n");

   pHypo->iNumPs = 0;
   pHypo->iNumBadPs = 0;
   for ( i=0; i<iPNum; i++ )    
      if ( P[i].iUseMe > 0 && P[i].dPTime > 0. ) pHypo->iNumPs++;
      else                                       pHypo->iNumBadPs++;
   dTempLon = PI / 2.;
   pHypo->dLat = 0.;
   pHypo->dLon = 0.;
   llAKArray.dLat = 0.47;       
   llAKArray.dLon = 3.65;

   iDLev = (int) ((double) (DEPTHKM+0.01) / IASP_DEPTH_INC)*IASP_NUM_PER_DEP;
	    
   if ( iArea == 2 )  
   {
      pHypo->dLat = dLatUser;
      pHypo->dLon = dLonUser;
      GeoCent ((LATLON *) pHypo);
   }
   
   else if ( iArea == 1 || (iArea == 3 && iTry == 1) )
   {
      dTemp1 = 1.e20;
      l = 0;
      for ( i=0; i<iPNum; i++ )
         if ( P[i].dPTime > 0. && P[i].iUseMe > 0 && P[i].dPTime < dTemp1 )
         {
            dTemp1 = P[i].dPTime;
            l = i;
         }
      pHypo->dLat = P[l].dLat + 0.02;
      pHypo->dLon = P[l].dLon + 0.02;
   }
   
   else if ( iArea == 0 || (iArea == 3 && iTry > 1) )
   {

      for ( i=0; i<iPNum; i++ ) {
         LATLON llP;
         llP.dLat = P[i].dLat; llP.dLon = P[i].dLon;
         llP.dCoslat = P[i].dCoslat; llP.dSinlat = P[i].dSinlat;
         llP.dCoslon = P[i].dCoslon; llP.dSinlon = P[i].dSinlon;
         azidelt[i] = GetDistanceAz( &llP, &llAKArray );
      }
      
      k = 0;
      for ( i=0; i<iPNum; i++ )
         if ( P[i].iUseMe > 0 && P[i].dPTime > 0. )
         {
            iCnt = 0;
            for ( j=0; j<iPNum; j++ )
               if ( azidelt[i].dDelta < azidelt[j].dDelta &&
                    P[j].iUseMe > 0 && P[j].dPTime > 0. ) iCnt++;
            if ( iCnt >= (pHypo->iNumPs-4) )
            {
               /* FIX 64 BITS: Impedir desbordamiento del indice estricto de array */
               if (k < 4) {
                  iIndices[k] = i;
                  k++;
               }
            }
         }
      if ( k < 3 )                 
      {
         pHypo->dLat = 63.0;       
         pHypo->dLon = -152.0;
         GeoCent( (LATLON *) pHypo );
         GetLatLonTrig( (LATLON *) pHypo );
         return;
      }
   
      for ( i3=0; i3<4; i3++ )
      {
         j = iIndices[i3];
         i1 = i3 + 1;
         if ( i3 == 3 ) i1 = 0;
         k = iIndices[i1];
         i2 = i3 + 2;
         if ( i3 == 2 )      i2 = 0;
         else if ( i3 == 3 ) i2 = 1;
         l = iIndices[i2];
         
         LATLON llJ, llK, llL, llIA, llIB, llIC;
         llJ.dLat = P[j].dLat; llJ.dLon = P[j].dLon;
         llJ.dCoslat = P[j].dCoslat; llJ.dSinlat = P[j].dSinlat;
         llJ.dCoslon = P[j].dCoslon; llJ.dSinlon = P[j].dSinlon;

         llK.dLat = P[k].dLat; llK.dLon = P[k].dLon;
         llK.dCoslat = P[k].dCoslat; llK.dSinlat = P[k].dSinlat;
         llK.dCoslon = P[k].dCoslon; llK.dSinlon = P[k].dSinlon;
         
         llL.dLat = P[l].dLat; llL.dLon = P[l].dLon;
         llL.dCoslat = P[l].dCoslat; llL.dSinlat = P[l].dSinlat;
         llL.dCoslon = P[l].dCoslon; llL.dSinlon = P[l].dSinlon;

         azidelt01 = GetDistanceAz( &llJ, &llK );
         azidelt02 = GetDistanceAz( &llJ, &llL );
         azidelt12 = GetDistanceAz( &llK, &llL );

         dTemp1 = 0.5*(azidelt01.dDelta + azidelt02.dDelta + azidelt12.dDelta);
         dTemp2 = sqrt( (sin ((dTemp1 - azidelt01.dDelta)*RAD) *
                         sin ((dTemp1 - azidelt02.dDelta)*RAD) *
                         sin ((dTemp1 - azidelt12.dDelta)*RAD)) / 
                         sin (dTemp1*RAD) );
         dAng[0] = 2.*atan( dTemp2/sin( (dTemp1-azidelt01.dDelta)*RAD ) )*DEG;
         dAng[1] = 2.*atan( dTemp2/sin( (dTemp1-azidelt02.dDelta)*RAD ) )*DEG;
         dAng[2] = 2.*atan( dTemp2/sin( (dTemp1-azidelt12.dDelta)*RAD ) )*DEG;

         llCen1.dLat = (min( P[j].dLat, min (P[k].dLat, P[l].dLat) ) + 
                        max( P[j].dLat, max (P[k].dLat, P[l].dLat) )) / 2.0;
         llCen1.dLon = (min( P[j].dLon, min (P[k].dLon, P[l].dLon) ) + 
                        max( P[j].dLon, max (P[k].dLon, P[l].dLon) )) / 2.0;

         for ( i=0; i<3; i++ ) dAngle[i] = fabs( dAng[i] - 60.0 );
         dTemp1 = 10000;
         ia = j; ib = k; ic = l; 
         for ( i=0; i<3; i++ )
            if ( dAngle[i] < dTemp1 )     
            {
               dTemp1 = dAngle[i];
               if ( i == 0 ) { ia=j; ib=k; ic=l; }
               if ( i == 1 ) { ia=k; ib=l; ic=j; }
               if ( i == 2 ) { ia=l; ib=j; ic=k; }
            }
            
         llIA.dLat = P[ia].dLat; llIA.dLon = P[ia].dLon;
         llIA.dCoslat = P[ia].dCoslat; llIA.dSinlat = P[ia].dSinlat;
         llIA.dCoslon = P[ia].dCoslon; llIA.dSinlon = P[ia].dSinlon;
         
         llIB.dLat = P[ib].dLat; llIB.dLon = P[ib].dLon;
         llIB.dCoslat = P[ib].dCoslat; llIB.dSinlat = P[ib].dSinlat;
         llIB.dCoslon = P[ib].dCoslon; llIB.dSinlon = P[ib].dSinlon;

         llIC.dLat = P[ic].dLat; llIC.dLon = P[ic].dLon;
         llIC.dCoslat = P[ic].dCoslat; llIC.dSinlat = P[ic].dSinlat;
         llIC.dCoslon = P[ic].dCoslon; llIC.dSinlon = P[ic].dSinlon;

         azideltB = GetDistanceAz( &llIA, &llIB );
         azideltC = GetDistanceAz( &llIA, &llIC );

         if (azideltC.dAzimuth <= azideltB.dAzimuth)
         {
            i = ib;
            ib = ic;
            ic = i;
            
            llIB.dLat = P[ib].dLat; llIB.dLon = P[ib].dLon;
            llIB.dCoslat = P[ib].dCoslat; llIB.dSinlat = P[ib].dSinlat;
            llIB.dCoslon = P[ib].dCoslon; llIB.dSinlon = P[ib].dSinlon;

            llIC.dLat = P[ic].dLat; llIC.dLon = P[ic].dLon;
            llIC.dCoslat = P[ic].dCoslat; llIC.dSinlat = P[ic].dSinlat;
            llIC.dCoslon = P[ic].dCoslon; llIC.dSinlon = P[ic].dSinlon;
            
            azideltB = GetDistanceAz (&llIA, &llIB);
            azideltC = GetDistanceAz (&llIA, &llIC);
         }

         if ( (azideltC.dAzimuth - azideltB.dAzimuth) > 180. )
         {                               
            azideltAB.dAzimuth = azideltB.dAzimuth + 90.;
            azideltAC.dAzimuth = azideltC.dAzimuth - 90.;
         }
         else
         {
            azideltAB.dAzimuth = azideltB.dAzimuth - 90.;
            azideltAC.dAzimuth = azideltC.dAzimuth + 90.;
         }
         if ( azideltAB.dAzimuth < 0.0 ) azideltAB.dAzimuth += 360.;
         if ( azideltAC.dAzimuth < 0.0 ) azideltAC.dAzimuth += 360.;

         dTdAB = P[ib].dPTime - P[ia].dPTime;
         dTdAC = P[ic].dPTime - P[ia].dPTime;
         if ( dTdAB == 0.0 ) dTdAB = 0.01;        
         if ( dTdAC == 0.0 ) dTdAC = 0.01;

         i = (int) ((azideltB.dDelta)*(1./IASP_DIST_INC));
         dCorrB = azideltB.dDelta*(1./IASP_DIST_INC) - 
                  floor( azideltB.dDelta*(1./IASP_DIST_INC) );
         n = (int) ((azideltC.dDelta)*(1./IASP_DIST_INC)); 
         dCorrC = azideltC.dDelta*(1./IASP_DIST_INC) - 
                  floor( azideltC.dDelta*(1./IASP_DIST_INC) );

         dTdABm = fPP[i+iDLev] + dCorrB*(fPP[(i+1)+iDLev] - fPP[i+iDLev]);
         dTdACm = fPP[n+iDLev] + dCorrC*(fPP[(n+1)+iDLev] - fPP[n+iDLev]);

         if ( dTdAB/dTdABm < 0.0 ) azideltAC.dAzimuth += 180.;
         if ( dTdAC/dTdACm < 0.0 ) azideltAB.dAzimuth += 180.;
         if ( azideltAB.dAzimuth >= 360. ) azideltAB.dAzimuth -= 360.;
         if ( azideltAC.dAzimuth >= 360. ) azideltAC.dAzimuth -= 360.;
         dx = fabs( dTdAB/dTdABm ) * sin( azideltAC.dAzimuth * RAD ) +
              fabs( dTdAC/dTdACm ) * sin( azideltAB.dAzimuth * RAD );
         dy = fabs( dTdAB/dTdABm ) * cos( azideltAC.dAzimuth * RAD ) +
              fabs( dTdAC/dTdACm ) * cos( azideltAB.dAzimuth * RAD );
         dHyp = sqrt( dx*dx + dy*dy );
         if ( dx > 0.0 )
         {
            if ( dy > 0.0 ) dAz = acos( dy/dHyp ) * DEG;
            else            dAz = acos( dx/dHyp ) * DEG + 90.;
         }
         else
         {
            if ( dy <= 0.0 ) dAz = acos( fabs( dy )/dHyp) * DEG + 180.;
            else             dAz = asin( dy/dHyp ) * DEG + 270.;
         }

         dAzEpiAB = fabs( dAz - azideltB.dAzimuth );
         dAzEpiAC = fabs( dAz - azideltC.dAzimuth );
         if ( dAzEpiAB > 180. ) dAzEpiAB -= 180.;
         if ( dAzEpiAC > 180. ) dAzEpiAC -= 180.;

         if ( fabs( cos( dAzEpiAB*RAD ) ) >= fabs( cos( dAzEpiAC*RAD ) ) )
         {
            dApVel = azideltB.dDelta*111./fabs( dTdAB );
            dTemp2 = fabs( cos( dAzEpiAB*RAD ) );
            if ( dTemp2 == 0.0 ) dTemp2 = 0.05; 
            dTemp1 = 6. / (dApVel*dTemp2);
            if ( dTemp1 >= 1.0 ) dTemp1 = 0.99; 
            dAngIncident = asin( dTemp1 ) * DEG;
            llCen2.dLat = (P[ia].dLat+P[ib].dLat) / 2.0;
            llCen2.dLon = (P[ia].dLon+P[ib].dLon) / 2.0;
         }		
         else
         {
            dApVel = azideltC.dDelta*111./fabs( dTdAC );
            dTemp2 = fabs( cos( dAzEpiAC*RAD ) );
            if ( dTemp2 == 0.0 ) dTemp2 = 0.05; 
            dTemp1 = 6. / (dApVel*dTemp2);
            if ( dTemp1 >= 1.0 ) dTemp1 = 0.99; 
            dAngIncident = asin( dTemp1 ) * DEG;
            llCen2.dLat = (P[ia].dLat+P[ic].dLat) / 2.0;
            llCen2.dLon = (P[ia].dLon+P[ic].dLon) / 2.0;
         }

         for ( i=0; i<100; i++ )
            if ( (iDelt[i]*.1) < dAngIncident )
            {
               dDelta = (double) (i+1);
               break;
            }

         if ( llCen2.dLat != llCen1.dLat || llCen1.dLon != llCen2.dLon )
         {
            azideltTemp = GetDistanceAz( &llCen1, &llCen2 );
            if ( dDelta >= azideltTemp.dDelta )
               dDelta += azideltTemp.dDelta * cos( fabs(
	                 dAz-azideltTemp.dAzimuth ) * RAD );
         }

         azideltTemp.dDelta = dDelta * RAD;
         azideltTemp.dAzimuth = dAz * RAD;
         llEpi = PointToEpi( &llCen1, &azideltTemp );
         if ( pHypo->iNumPs > 3 )  
         {                         
            pHypo->dLat += llEpi.dLat * 0.25;
            if ( dTempLon < 0.8 && llEpi.dLon > 5.2 )
               llEpi.dLon -= TWOPI; 
            else if ( dTempLon > 5.2 && llEpi.dLon < 0.8 ) 
               llEpi.dLon += TWOPI;
            pHypo->dLon += .25 * llEpi.dLon;
            dTempLon = pHypo->dLon * 4.0 / (double) (i3+1);
         }
         else                      
         {
            pHypo->dLat = llEpi.dLat;
            pHypo->dLon = llEpi.dLon;
            break;                 
         }
      }
      while ( pHypo->dLon >= TWOPI ) pHypo->dLon -= TWOPI; 
      while ( pHypo->dLon < 0.0 )    pHypo->dLon += TWOPI;
      while ( pHypo->dLat >= PI )    pHypo->dLat -= PI;		
      while ( pHypo->dLon < 0.0 )    pHypo->dLon += PI;
   }
   else                      
   {
      pHypo->dLat = 63.0;    
      pHypo->dLon = -152.0;
      GeoCent( (LATLON *) pHypo );
   }
   GetLatLonTrig( (LATLON *) pHypo );
}
       
void IsItGoodSoln( int iPNum, PPICK *P, HYPO *pHypo, int iMinPs )
{
   double  dMin;
   int     i;
   int     iAnyOut;	    
   int     iAnyWayOut;      
   int     iIndex = 0;      
   int     iOddLooking;     
   int     iPUsed;          

   iAnyOut    = 0;
   iAnyWayOut = 0;	
   iPUsed = 0;
   for ( i=0; i<iPNum; i++ )
      if ( P[i].iUseMe > 0 && P[i].dPTime > 0. )
      {
         iPUsed++;
         if ( fabs( P[i].dRes ) > 10. ) iAnyWayOut++;
         if ( fabs( P[i].dRes ) > 5. ) iAnyOut++;
      }
     
   dMin = 360.;
   iOddLooking = 0;
   for ( i=0; i<iPNum; i++ )
      if ( P[i].iUseMe > 0 && P[i].dPTime > 0. )
         if ( P[i].dDelta < dMin )
         {
            dMin = P[i].dDelta;
            iIndex = i;
         }
   if ( P[iIndex].dFreq > FREQ_MIN && P[iIndex].dDelta > DELTA_TELE )
      iOddLooking = 1;
	
   if ( (iPUsed > iMinPs && pHypo->dAvgRes < 1.5 && 
         iPUsed/10 >= iAnyOut && pHypo->dNearestDist < 45. && 
        (iOddLooking == 0 || (iOddLooking == 1 && iPNum > 15)) &&
        (pHypo->dNearestDist < 10. || pHypo->iAzm > 180.)) ) 
      pHypo->iGoodSoln = 3;			
   else if ( iPUsed >= iMinPs && pHypo->dAvgRes < 2.5 &&
             iPUsed/10 >= iAnyWayOut && pHypo->dNearestDist < 90. &&
            (iOddLooking == 0 || (iOddLooking == 1 && iPNum > 15)) ) 
      pHypo->iGoodSoln = 2;			
   else if ( pHypo->dAvgRes < 4. ) 
      pHypo->iGoodSoln = 1;			
   else
      pHypo->iGoodSoln = 0;			
}
       
int IsItSameQuake( HYPO *pHypo1, HYPO *pHypo2 )
{
   AZIDELT azidelt;         
   LATLON ll1, ll2;
   
   ll1.dLat = pHypo1->dLat; ll1.dLon = pHypo1->dLon;
   ll1.dCoslat = pHypo1->dCoslat; ll1.dSinlat = pHypo1->dSinlat;
   ll1.dCoslon = pHypo1->dCoslon; ll1.dSinlon = pHypo1->dSinlon;
   
   ll2.dLat = pHypo2->dLat; ll2.dLon = pHypo2->dLon;
   ll2.dCoslat = pHypo2->dCoslat; ll2.dSinlat = pHypo2->dSinlat;
   ll2.dCoslon = pHypo2->dCoslon; ll2.dSinlon = pHypo2->dSinlon;

   azidelt = GetDistanceAz( &ll1, &ll2 );
                        
   if ( (pHypo1->dOriginTime > pHypo2->dOriginTime-60.  &&
         pHypo1->dOriginTime < pHypo2->dOriginTime+60.) &&
         azidelt.dDelta < 10. ) return 1;
   return 0;
}
       
int LoadEQData( char *pszFile, int iNumDep, EQDEPTHDATA *pEqDepth )
{
   int   i;             
   FILE *hFile;         

   i = 0;
   if ( (hFile = fopen( pszFile, "r" )) == NULL )
   {
      logit( "", "Error opening depth data file %s.\n", pszFile );
      return( -1 );
   }
   while ( !feof( hFile ) )   
   {
      fscanf( hFile, "%d %d %d %d",  &pEqDepth[i].iLat, &pEqDepth[i].iLon,
              &pEqDepth[i].iAveDepth, &pEqDepth[i].iMaxDepth );
      i++; 
      if ( i > iNumDep )    
      {
         logit( "", "Too many values in %s, %ld values\n", pszFile, i );
         break;
      }
   }
   fclose( hFile );
   return (1);
} 
       
int QuakeAzimuthSort( int iPNum, PPICK *P )
{
   double  dAz[MAX_STATION_DATA];  
   double  dAzGap;
   double  dTemp, dTempAz;
   int     i, ii, j;
   int     iGoodPicks;             

   ii = 0;
   for ( i=0; i<iPNum; i++ )
      if ( P[i].iUseMe > 0 && P[i].dPTime > 0. )
      {
         dAz[ii] = P[i].dAz;
         ii++;
      }
   iGoodPicks = ii;

   for ( i=0; i<iGoodPicks-1; i++ )
   {
      ii = i+1;
      for ( j=ii; j<iGoodPicks; j++ )
         if ( dAz[i] > dAz[j] )
         {
            dTemp = dAz[i];
            dAz[i] = dAz[j];
            dAz[j] = dTemp;
         }
   }
   
   dAzGap = 0.0;
   ii = 0;
   for ( i=0; i<iGoodPicks-1; i++ )
   {
      dTempAz = dAz[i+1] - dAz[i];
      if ( dTempAz > dAzGap ) dAzGap = dTempAz;
   }
   
   dTempAz = 360. - (dAz[iGoodPicks-1] - dAz[0]);
   if (dTempAz > dAzGap) return( (int) (360.0 - dTempAz) );
   else                  return( (int) (360.0 - dAzGap) );
}
       
double QuakeDeta( int iDNum, double dAdet[][4] )
{
   double  dProd, dSave[4];
   int     k, kk, i1, k1, k2, i, j, iCnt;

   dProd = 1.0;
   for ( k=0; k<(iDNum-1); k++ )
   {
      iCnt = 0;
      kk = k+1;
      while ( dAdet[k][k] == 0.0 )
      {
         if ( iCnt-(iDNum-1)+k+1 > 0 ) return 0.0;
         else
         {
            for ( i1=k; i1<iDNum; i1++ ) dSave[i1] = dAdet[i1][k];
            for ( k1=k; k1<iDNum; k1++ )
               for ( k2=k; k2<(iDNum-1); k2++ ) 
                  dAdet[k1][k2] = dAdet[k1][k2+1];
            for ( i1=k; i1<iDNum; i1++ ) dAdet[i1][k] = dSave[i1];
            iCnt++;
            dProd *= ((((iDNum-1)-k) % 2) ? -1.0 : 1.0);
         }
      }
      for ( i=kk; i<iDNum; i++ )
         for ( j=kk; j<iDNum; j++ )
            dAdet[j][i] -= dAdet[j][k] * dAdet[k][i] / dAdet[k][k];
      dProd *= dAdet[k][k];
   }
   dProd *= dAdet[iDNum-1][iDNum-1];
   return dProd;
}
       
void QuakeDets( int iDNum, double dAdet[][4], double dBdet[], double dXdet[] )
{
   double  dA1det[4][4], dDenom, dAnum;
   int     i, j, k;

   for ( i=0; i<iDNum; i++ )
      for ( j=0; j<iDNum; j++ ) dA1det[i][j] = dAdet[i][j];

   dDenom = QuakeDeta( iDNum, dAdet );
   if ( dDenom < 1.E-13 )
   {
      dXdet[0] = 111.;
      return;
   }
   for ( k=0; k<iDNum; k++ )
   {
      for ( i=0; i<iDNum; i++ )
         for ( j=0; j<iDNum ; j++ ) dAdet[i][j] = dA1det[i][j];
      for ( i=0; i<iDNum; i++ ) dAdet[i][k] = dBdet[i];
      dAnum = QuakeDeta( iDNum, dAdet );
      dXdet[k] = dAnum / dDenom;
   }
}
       
void QuakeSolveIasp( int iPNum, PPICK *P, HYPO *pHypo, EQDEPTHDATA pEqDep[],
                     int iFindDepth )
{
   static double  dAdet[4][4];  
   static double  dBdet[4];
   static double  dCoef[4*COEFFSIZE+MAX_STATION_DATA+10];
   double  dMin;
   static double  dXdet[4];
   double  dPTimeMin, dDeltaMin;
   double  fih, fh, dadd, dadh, ptt1, ptt2, ptt; 
   int     i, j, k, l, mm, ih, itab, iIndex;     
   int     iIterCnt;                             
   int	   iMaxDepthLevel = 0;      
   int     iPCnt;               
   LATLON  LLIn, LLOut;	        
   float   *pfIA;               

   iIterCnt = 0;
   for ( i=0; i<QUAKE_ITER; i++ )
   {
      iPCnt = 0;
            
      GetEpiAzDelta( iPNum, P, pHypo );
      
      if ( pHypo->dOriginTime == 0.0 )
      {
         dMin = 1.e20;
         l = 0;
         for ( j=0; j<iPNum; j++ )
            if ( P[j].dPTime > 0. && P[j].iUseMe > 0 && P[j].dPTime < dMin ) 
            {
               l = j;
               dMin = P[j].dPTime;
            }
	    
         dPTimeMin = P[l].dPTime;	
         dDeltaMin = P[l].dDelta;	
	 
         if ( dDeltaMin <= 20.0 )
            pHypo->dOriginTime = dPTimeMin - 3.67489 - 14.1561*dDeltaMin -
             0.0189237*dDeltaMin*dDeltaMin + 0.00267753*dDeltaMin*dDeltaMin*
             dDeltaMin;
         else
            pHypo->dOriginTime = dPTimeMin - 61.1089 - 11.4192*dDeltaMin +
             0.0410401*dDeltaMin*dDeltaMin - 0.0000301625*dDeltaMin*dDeltaMin*
             dDeltaMin;
      }
      pHypo->dAvgRes = 0.0;
    
      if (iFindDepth == 1)
      {
         LLIn.dLat = pHypo->dLat;
         LLIn.dLon = pHypo->dLon;
         GeoGraphic (&LLOut, &LLIn);
         if (LLOut.dLon > 180.) LLOut.dLon -= 360.;
         if (pHypo->iDepthControl == 3)		
         {
            iIndex = FindDepth( LLOut.dLat, LLOut.dLon, pEqDep );
            if (iIndex == -1)
	    {
               pHypo->dDepth = DEFAULT_DEPTH;
	       iMaxDepthLevel = DEFAULT_MAXDEPTH / (int) IASP_DEPTH_INC;
	    }
	    else
            {
	       pHypo->dDepth = (double) pEqDep[iIndex].iAveDepth;
	       iMaxDepthLevel = (int) (pEqDep[iIndex].iMaxDepth /
                                (int) IASP_DEPTH_INC) + 5;
               if (iMaxDepthLevel > DEPTH_LEVELS_IASP)
                   iMaxDepthLevel = DEPTH_LEVELS_IASP;
	       }
	    }
         else if (pHypo->iDepthControl == 4)		
         {
            iIndex = FindDepth( LLOut.dLat, LLOut.dLon, pEqDep );
	    if (iIndex == -1)
	       iMaxDepthLevel = DEFAULT_MAXDEPTH / (int) IASP_DEPTH_INC;
	    else
	    {
	       iMaxDepthLevel = (int) (pEqDep[iIndex].iMaxDepth /
                                (int) IASP_DEPTH_INC) + 5;
               if (iMaxDepthLevel > DEPTH_LEVELS_IASP)
                   iMaxDepthLevel = DEPTH_LEVELS_IASP;
	    }
	 }
      }
      else
        iMaxDepthLevel = DEPTH_LEVELS_IASP;

      fih = pHypo->dDepth / IASP_DEPTH_INC;
      if ( fih <= 1./IASP_DEPTH_INC )      
      {
         fih = 1./IASP_DEPTH_INC;					
         pHypo->dDepth = 1.0;
      }
      ih = (int) (fih + 1.0);
      fh = fih - floor( fih );
      if (ih >= iMaxDepthLevel - 1)	
      {	  
         ih = iMaxDepthLevel - 1;
         pHypo->dDepth = (double) (iMaxDepthLevel*IASP_DEPTH_INC);
         fh = 0.0;
      }
      
      for ( j=0; j<iPNum; j++ )
      {
         if ( P[j].dPTime > 0. )
         {
            itab = (int) (P[j].dDelta*(1./IASP_DIST_INC));
            while ( P[j].dFracDelta - IASP_DIST_INC >= 0.0 )  
                    P[j].dFracDelta -= IASP_DIST_INC;

            pfIA = fPP + ih*IASP_NUM_PER_DEP + itab; 
            dadd = ((*(pfIA+1)-*pfIA)*fh + (*(pfIA-IASP_NUM_PER_DEP+1)-
                   *(pfIA-IASP_NUM_PER_DEP))*(1.-fh)) / IASP_DIST_INC;
            dadh = (*(pfIA-IASP_NUM_PER_DEP)-*pfIA) * (1.-P[j].dFracDelta/IASP_DIST_INC) + 
                   (*(pfIA-IASP_NUM_PER_DEP+1) - 
                   *(pfIA+1))*(P[j].dFracDelta/IASP_DIST_INC);
		   
            ptt1 = *pfIA + (*(pfIA+1)-*pfIA)*P[j].dFracDelta/IASP_DIST_INC;
            ptt2 = *(pfIA-IASP_NUM_PER_DEP) + (*(pfIA-IASP_NUM_PER_DEP+1)-
                   *(pfIA-IASP_NUM_PER_DEP))*P[j].dFracDelta/IASP_DIST_INC;
            ptt = ptt1*fh + ptt2*(1.-fh);            

            P[j].dRes = P[j].dPTime - (pHypo->dOriginTime + ptt) -
                        P[j].dElevation/EARTHRAD;    
            if ( P[j].iUseMe > 0 )                   
            {
               dCoef[COEFFSIZE+iPCnt] = dadd * P[j].dCooze;
               dCoef[2*COEFFSIZE+iPCnt] = dadd * P[j].dSnooze *
                                          pHypo->dSinlat * (-1.0);
               dCoef[3*COEFFSIZE+iPCnt] = -dadh * 0.1;  
               dCoef[4*COEFFSIZE+iPCnt] = P[j].dRes;
               dCoef[iPCnt] = 1.0;
               pHypo->dAvgRes += fabs( P[j].dRes );
               if ( pHypo->iDepthControl == 3 )
                  dCoef[3*COEFFSIZE+iPCnt] = dCoef[4*COEFFSIZE+iPCnt];
               iPCnt++;
            } 
         }
         else
            P[j].dRes = 0.;
      }
	
      if ( iIterCnt >= QUAKE_ITER-1 || pHypo->dAvgRes/(double) iPNum < 0.2 )
         goto FunctionEnd;

      for ( j=0; j<pHypo->iDepthControl; j++ )
         for ( l=0; l<pHypo->iDepthControl; l++ )
         {
            dAdet[j][l] = 0.0;
            for ( k=0; k<iPCnt; k++ )
               dAdet[j][l] += dCoef[k+j*COEFFSIZE] * dCoef[k+l*COEFFSIZE];
         }
      mm = pHypo->iDepthControl;
      for ( j=0; j<pHypo->iDepthControl; j++ )
      {
         dBdet[j] = 0.0;
         for ( k=0; k<iPCnt; k++ )
            dBdet[j] += dCoef[k+j*COEFFSIZE] * dCoef[k+mm*COEFFSIZE];
      }
      
      QuakeDets( pHypo->iDepthControl, dAdet, dBdet, dXdet );
      if ( dXdet[0] == 111. ) goto FunctionEnd;
      if ( pHypo->iDepthControl > 3 ) pHypo->dDepth += dXdet[3];
      pHypo->dOriginTime += dXdet[0];
      pHypo->dLat += dXdet[1]*RAD;
      pHypo->dLon += dXdet[2]*RAD;
      iIterCnt++;
   }
FunctionEnd:

   pHypo->dAvgRes = pHypo->dAvgRes/(double) iPNum;

   pHypo->iAzm = QuakeAzimuthSort( iPNum, P );
   pHypo->dNearestDist = 200.;
   for ( j=0; j<iPNum; j++ )
      if ( P[j].dPTime > 0. && P[j].iUseMe > 0 )
         if ( P[j].dDelta < pHypo->dNearestDist )
         {
            pHypo->dNearestDist = P[j].dDelta;
            pHypo->dFirstPTime = P[j].dPTime;
         }
	
   if (pHypo->dLat < 0.)
   {
      pHypo->dLat = fabs( pHypo->dLat );
      pHypo->dLon -= PI;
   }
   while ( pHypo->dLat > TWOPI ) pHypo->dLat -= TWOPI;
   if ( pHypo->dLat > PI && pHypo->dLat <= TWOPI )
   {
      pHypo->dLat = TWOPI - pHypo->dLat;
      pHypo->dLon -= PI;
   }
   while ( pHypo->dLon > TWOPI ) pHypo->dLon -= TWOPI;
   while ( pHypo->dLon < 0.0 ) pHypo->dLon += TWOPI;
}
       
int Round( double dInput )
{
   int     iRound;
   double  dDec;
   
   if ( dInput >= 0. )
   {
      if ( modf( dInput, &dDec ) >= 0.5 )
      {
         iRound = (int) ceil( dInput );
      }
      else
         iRound = (int) floor( dInput );
   }
   else
      if ( modf( dInput, &dDec ) <= -0.5 )
      {
         iRound = (int) floor( dInput );
      }
      else
         iRound = (int) ceil( dInput );
	
   return (iRound);	
}
	   
int SortAllByPTime( const void *pP1, const void *pP2 )
{
   PPICK   *pP1T, *pP2T;

   pP1T = (PPICK *) pP1;
   pP2T = (PPICK *) pP2;
   if ( pP1T->dPTime > pP2T->dPTime ) return 1;
   else if ( pP1T->dPTime < pP2T->dPTime ) return -1;
   else return 0;
}
	   
int SortAllByExpectedPTime( const void *pP1, const void *pP2 )
{
   PPICK   *pP1T, *pP2T;

   pP1T = (PPICK *) pP1;
   pP2T = (PPICK *) pP2;
   if ( pP1T->dExpectedPTime > pP2T->dExpectedPTime ) return 1;
   else if ( pP1T->dExpectedPTime < pP2T->dExpectedPTime ) return -1;
   else return 0;
}
