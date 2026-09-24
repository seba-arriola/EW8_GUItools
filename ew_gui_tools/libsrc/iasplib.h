/****************************************************************************
IASPLIB.H

These declarartions are to be included in any program calling the utility
functions in taulib or emiasp91.
---------------------------------------------------------------------------*/
#define PSZ char*
#include "ttlim.h"

	// Function declarations (from emiasp91)
void emdld (int *, double [], PSZ);
void emdlv (double, double *, double *);
void emiask (double, double *, double *, double *);

	// Function declarations (from taulib)
void bkin (FILE *, int, int, double [], FILE *);
void brnset (int, char [10][PHASE_LENGTH], int [], FILE *);
void depcor (int, FILE *, FILE *);
void depset (double, double [], FILE *, FILE *);
void findtt (int, double [], int *, double [], double [], 
             double [], double [], char [60][PHASE_LENGTH], FILE *);
void fitspl (int, int, double [JOUT][4], double, double, double [5][JOUT]);
int iupcor (PSZ, double, double *, double *);
void pdecu (int, int *, double, double, double, int, FILE *);
void r4sort (int, double [], int []);
void spfit (int, int, FILE *);
void tabin (FILE **, PSZ, FILE *);
void tauint (double, double, double, double, double, double *, double *, 
             FILE *);
void tauspl (int, int, double [], double [5][JOUT]);
void trtm (double, int *, double [], double [], double [], double [], 
           char [MAX][PHASE_LENGTH], FILE *hFile);
double umod (double, int *, int);
double zmod (double, int, int);
