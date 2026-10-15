#ifndef K_READCFG_H
#define K_READCFG_H

#include "earthworm.h"
#include "kom.h"

extern int Nbuf;
// get the number of open configuration files
#define getNumOpen() Nbuf

typedef struct {
  char  filesOpen[KOM_MAXBUF][MAXFILENAMELEN]; /* Names of open files. */
  void *commandData;                           /* Command data or NULL if none */
  char* token;                                 /* Last token read from the configuration file. */
} READCFG_DATA;

typedef int (*processCommandType)(READCFG_DATA *data);

int k_closecfg(READCFG_DATA *data);
void k_initcfg(READCFG_DATA *data, void *commandData);
int k_opencfg(READCFG_DATA *data, char *filename);
int k_readcfg(READCFG_DATA *data, char *filename, processCommandType process);

#endif
