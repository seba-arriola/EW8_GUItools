
/*
 *   THIS FILE IS UNDER RCS - DO NOT MODIFY UNLESS YOU HAVE
 *   CHECKED IT OUT USING THE COMMAND CHECKOUT.
 *
 *    $Id: read_arc.h 8304 2020-12-21 16:25:10Z quintiliani $
 *
 *    Revision history:
 *     $Log$
 *     Revision 1.11  2024/02/16 12:00:00  oavv
 *     Added fields 165 to 171 from Y2000 hypoinverse summary format
 *     to struct _ARCDATA
 *     Added field 111 (3-letter code of crust and delay model) from Y2000
 *     hypoinverse summary format to struct _ARCDATA
 *     Added corresponding extra fields to struct Hsum
 * 
 *     Revision 1.10  2004/05/28 22:22:42  dietz
 *     added loc field to struct Hpck
 *
 *     Revision 1.9  2002/12/06 22:31:26  dietz
 *      Added 3 fields (nphS,nphtot,nPfm) to struct Hsum
 *
 *     Revision 1.8  2002/11/03 00:15:28  lombard
 *     Protected from multiple includes.
 *
 *     Revision 1.7  2002/10/29 18:47:13  lucky
 *     Added origin version number
 *
 *     Revision 1.6  2000/12/06 17:47:07  lucky
 *     Removed Pph and Sph strings from Hpck, and added Plabel, Slabel, Ponset,
 *     and Sonset. We need these to properly track the onset.
 *
 *     Revision 1.5  2000/09/12 19:07:02  lucky
 *     Defined separate FM fields for S and P picks
 *
 *     Revision 1.4  2000/08/21 19:48:36  lucky
 *     Modified the Hpck structure to be able to handle both P and S
 *     picks at the same time, by specifying time, residual, quality, and phase
 *     remarks separately for P and S picks.
 *
 *     Revision 1.3  2000/06/12 21:34:00  lucky
 *     Changed NO_FLOAT_VAL, NO_INT_VAL, and NO_LONG_VAL to 0.
 *
 *     Revision 1.2  2000/03/30 15:35:30  davidk
 *     Added fields to Hsum and Hpck that contained information neccessary for
 *     orareport, so that the read_arc routines could be used by orareport.
 *
 *     Revision 1.1  2000/02/14 20:05:54  lucky
 *     Initial revision
 *
 *
 */

#ifndef READ_ARC_H
#define READ_ARC_H

#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 512
#endif

/* Some special unlikely values */
#define NO_FLOAT_VAL  0.0
#define NO_INT_VAL    0
#define NO_LONG_VAL   0

/* Structure of info read from TYPE_HYP2000ARC message
 **************************************************/
struct Hsum {
        long    qid;       /* event id from binder */
        double  ot;        /* origin time as sec since 1600   */
        float   lat;       /* latitude (North=positive)       */
        float   lon;       /* longitude(East=positive)        */
        float   z;         /* depth (down=positive)           */
        int     nph;       /* # phases (P&S) w/ weight >0.1   */
        int     nphS;      /* # S phases w/ weight >0.1       */
        int     nphtot;    /* # phases (P&S) w/ weight >0.0   */
        int     nPfm;      /* # P first motions               */
        int     gap;       /* maximum azimuthal gap           */
        int     dmin;      /* distance (km) to nearest station*/
        float   rms;       /* RMS travel time residual        */
        int     e0az;      /* azimuth of largest principal error */
        int     e0dp;      /* dip of largest principal error  */
        float   e0;        /* magnitude (km) of largest principal error */
        int     e1az;      /* azimuth of intermediate principal error */
        int     e1dp;      /* dip of intermediate principal error  */
        float   e1;        /* magnitude (km) of intermed principal error */
        float   e2;        /* magnitude (km) of smallest principal error */
        float   erh;       /* horizontal error (km) */
        float   erz;       /* vertical error (km) */
        float   Md;        /* duration magnitude */
        char    reg[4];    /* location region */
        char    cdate[20]; /* date character buffer */
        char    labelpref; /* character describing preferred magnitude */
        float   Mpref;     /* preferred magnitude */
        float   wtpref;    /* weight (~ # readings) of preferred Mag */
        char    mdtype;    /* Coda duration magnitude type code */
        float   mdmad;     /* Median-absolute-difference of duration mags */
        float   mdwt;      /* weight (~ # readings) of Md */
        long    version;   /* version number of the origin */

        /* Extra fields */
        float   amplitudeMagnitude;          /* col  37 3 F3.2 */
        float   numberOfAmpMagWeightCode;    /* col  97 4 F4.1 */
        float   medianAbsDiffAmpMag;         /* col 105 3 F3.2 */
        char    crustModel[4];               /* col 111 3 A3   */
        char    domainCode[3];               /* col 165 2 A2   */
        char    processingVersion[3];        /* col 167 2 A2   */
        char    depthType;                   /* col 169 1 A1 (M=model depth; G=geoid depth) */
        char    crustModelType;              /* col 170 1 A1 T=CRT, H=CRH, E=CRE, V=CRV, L=CRL */
        int     datumDepth;                  /* col 171 4 I4   */
};

/* Structure to hold raw pick info from a phase line & its shadow
 ****************************************************************/
struct Hpck {
        char    site[6];   /* site code, null terminated */
        char    net[3];    /* seismic network code, null terminated */
        char    comp[4];   /* component code, null terminated */
        char    loc[3];    /* location code, null terminated */
        char    Plabel;    /* P phase label */
        char    Slabel;    /* S phase label */
        char    Ponset;    /* P phase onset */
        char    Sonset;    /* S phase onset */
        double  Pat;       /* P-arrival-time as sec since 1600 */
        double  Sat;       /* S-arrival-time as sec since 1600 */
        float   Pres;      /* P travel time residual */  
        float   Sres;      /* S travel time residual */  
        int     Pqual;     /* Assigned P weight code */
        int     Squal;     /* Assigned S weight code */
        int     codalen;   /* Coda duration time */
        int     codawt;    /* Coda weight */
        char    Pfm;       /* P first motion */
        char    Sfm;       /* S first motion */
        char    cdate[18]; /* date character buffer */
        char    datasrc;   /* Data source code. */
        float   Md;        /* Station duration magnitude */
        int     azm;       /* azimuth */
        int     takeoff;   /* emergence angle at source */
        float   dist;      /* epicentral distance (km) */
        float   Pwt;       /* P weight actually used. */
        float   Swt;       /* S weight actually used. */
        int     pamp;      /* peak P-wave half amplitude */
        int     codalenObs;/* Coda duration time (Measured) */
        int     ccntr[6];  /* Window center from P time */
        int     caav[6];   /* Average Amplitude for ccntr[x] */

        /* Extra fields */
        float   amplitude;        /* col  55 7 F7.2 */
        int     ampUnitsCode;     /* col  62 2 I2 */
        int     ampType;          /* col 114 2 I2 */
        float   ampMag;           /* col  98 3 F3.2 */
        int     ampMagWeightCode; /* col  82 1 I1 */
        float   importanceP;      /* col 101 4 F4.3 */
        float   importanceS;      /* col 105 4 F4.3 */
};

#define MAX_PHASES 5000
typedef struct {
	struct Hsum sum;
	int num_phases;
	struct Hpck *phases[MAX_PHASES];
} HypoArc;

/* Layout of HypoARC archive message
 *********************************************/
typedef struct _ARCDATA {
    char originTime[16];	// YYYYMMDDHHMMSSSS	0
    char lat[7];		// DDaMMmm		16    was latitude
    char lon[8];		// DDDaMMmm		23    was longitude
    char z[5];			// DDDdd		31    was depth
    char amplitudeMagnitude[3];	// FFF  36
    char nph[3];		// III			39    was numPhases
    char gap[3];		// III			42    was azimuthalGap
    char dmin[3];		// DDD			45    was minDist
    char rms[4];		// Dddd			48
    char e0az[3];		// III			52
    char e0dp[2];		// II			55
    char e0[4];			// Dddd			57
    char e1az[3];		// III			61
    char e1dp[2];		// DD			64
    char e1[4];			// Dddd			66
    char Md[3];			// Mmm			70
    char reg[3];		// CCC			73
    char e2[4];			// Dddd			76
    char pad2[2];		//			80
    char nphS[3];		// III			82
    char erh[4];		// DDdd			85    was horzError
    char erz[4];		// DDdd			89    was vertError
    char nPfm[3];		// III			93
    char numberOfAmpMagWeightCode[4];// 96
    char mdwt[4];		// Dddd			100
    char medianAbsDiffAmpMag[3];		//			104
    char mdmad[3];		// Ddd			107
    char crModel[3];    // CCC          110
    char pad3c[4];      //              113
    char mdtype[1];		// C			117
    char nphtot[3];		// III			118
    char pad3d[1];		//			121
    char magCode[1];		// C			122
    char pad4[13];		//			123
    char qid[10];		// CCCCCCCCCC		136    was eventID 11
    char labelpref[1];		// C			146
    char Mpref[3];		// Ddd			147
    char wtpref[4];		// Dddd			150
    char pad6[8];		// 			154
    char version[1];	// C			162
    char pad7[1];       //              163   --------------------
    char domain[2];     // CC           164   these 6 fields where
    char procVersion[2];// CC           166   part of eventID[15]
    char depthType[1];  // C            168
    char crType[1];     // C            169
    char datumDepth[4]; // IIII         170   --------------------
    char eventID[4];		// CCCCC	174    was pad_eventid
    char eventVersion[19];	// CCCCCCCCCCCCCCCCCCC	178
    char eventFlags[5]; // CCCCC
    char break1;		// newline
    char shadow[1];
    char break2;		// newline
    char term;			// terminator
} ARCDATA;			//			206

/* Initialize the HypoARC archive message data */
#define ARCDATA_INIT(A)	memset( &(A), ' ', sizeof(A) ); (A).term = '\0'; (A).break1 = (A).break2 = '\n';
/* HypoARC archive message file size (skip the terminator) */
#define ARCFILE_SIZE	(sizeof(ARCDATA) - 1)
/* Get the pointer after the specified ARCDATA field */
#define ARC_AFTER(fld)	((char*)(fld)+sizeof(fld))
/** Reviewed flag for event */
#define ARC_EVENT_FLAGS_REVIEWED 1

int parse_arc(char *msg, HypoArc *arc);
int parse_arc_no_shdw(char *msg, HypoArc *arc);
int free_phases(HypoArc *arc);
int write_arc(char *msg, HypoArc *arc);

/* Function Prototypes
 *********************/
/**
 * Get the event flags.
 *      Inputs: ad - the ARCDATA
 *      Outputs: eventFlags - the event flags
 */
void arcGetEventFlags(ARCDATA ad, int* eventFlags);
/**
* Get the version.
*      Inputs: ad - the ARCDATA
*              len - the ARCDATA string  length
*      Outputs: version - the version
*/
void arcGetVersion(ARCDATA ad, int len, long* version);
int read_hyp( char *arcStrMsg, char *shdw, struct Hsum *sumP );
int read_phs( char *phsline, char *shdw, struct Hpck *pckP );
int read_phs_no_shdw( char *phsline, struct Hpck *pckP );

void write_hyp( char *sumcard, struct Hsum sumP );
void write_phs( char *phscard, struct Hpck pckP );
void write_term( char *termcard, long HypId );

char ComputeAverageQuality(float rms, float erh, float erz, float depth, float dmin, int no, int gap);
char *parse_arc_next_shadow(char *src);

#endif
